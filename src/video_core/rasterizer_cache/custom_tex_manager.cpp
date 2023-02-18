// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/bit_util.h"
#include "common/file_util.h"
#include "common/hash.h"
#include "common/image_util.h"
#include "common/scratch_buffer.h"
#include "core/core.h"
#include "video_core/rasterizer_cache/custom_tex_manager.h"
#include "video_core/rasterizer_cache/surface_params.h"

namespace VideoCore {

namespace {

using namespace Common;

CustomFileFormat MakeFileFormat(std::string_view ext) {
    if (ext == "png") {
        return CustomFileFormat::PNG;
    } else if (ext == "dds") {
        return CustomFileFormat::DDS;
    } else if (ext == "ktx") {
        return CustomFileFormat::KTX;
    }
    LOG_ERROR(Render, "Unknown file extension {}", ext);
    return CustomFileFormat::PNG;
}

CustomPixelFormat ToCustomPixelFormat(ddsktx_format format) {
    switch (format) {
    case DDSKTX_FORMAT_RGBA8:
        return CustomPixelFormat::RGBA8;
    case DDSKTX_FORMAT_BC1:
        return CustomPixelFormat::BC1;
    case DDSKTX_FORMAT_BC3:
        return CustomPixelFormat::BC3;
    case DDSKTX_FORMAT_BC5:
        return CustomPixelFormat::BC5;
    case DDSKTX_FORMAT_BC7:
        return CustomPixelFormat::BC7;
    case DDSKTX_FORMAT_ASTC4x4:
        return CustomPixelFormat::ASTC;
    default:
        LOG_ERROR(Common, "Unknown dds/ktx pixel format {}", format);
        return CustomPixelFormat::RGBA8;
    }
}

} // Anonymous namespace

CustomTexManager::CustomTexManager(Core::System& system_)
    : system{system_}, workers{std::max(std::thread::hardware_concurrency(), 2U) - 1,
                               "Hires processing"} {}

CustomTexManager::~CustomTexManager() = default;

void CustomTexManager::FindCustomTextures() {
    if (textures_loaded) {
        return;
    }

    // Custom textures are currently stored as
    // [TitleID]/tex1_[width]x[height]_[64-bit hash]_[format].png
    using namespace FileUtil;

    const u64 program_id = system.Kernel().GetCurrentProcess()->codeset->program_id;
    const std::string load_path =
        fmt::format("{}textures/{:016X}/", GetUserPath(UserPath::LoadDir), program_id);

    // Create the directory if it did not exist
    if (!Exists(load_path)) {
        CreateFullPath(load_path);
    }

    FSTEntry texture_dir;
    std::vector<FSTEntry> textures;
    // 64 nested folders should be plenty for most cases
    ScanDirectoryTree(load_path, texture_dir, 64);
    GetAllFilesFromNestedEntries(texture_dir, textures);

    u32 width{};
    u32 height{};
    u64 hash{};
    u32 format{};
    std::string ext(3, ' ');

    for (const FSTEntry& file : textures) {
        const std::string& path = file.physicalName;
        if (file.isDirectory || !file.virtualName.starts_with("tex1_")) {
            continue;
        }

        // Parse the texture filename. We only really care about the hash,
        // the rest should be queried from the file itself.
        if (std::sscanf(file.virtualName.c_str(), "tex1_%ux%u_%lX_%u.%s", &width, &height, &hash,
                        &format, ext.data()) != 5) {
            continue;
        }

        auto [it, new_texture] = custom_textures.try_emplace(hash);
        if (!new_texture) {
            LOG_ERROR(Render, "Textures {} and {} conflict, ignoring!", custom_textures[hash].path,
                      path);
            continue;
        }

        auto& texture = it->second;
        texture.file_format = MakeFileFormat(ext);
        texture.path = path;

        // Query the required information from the file and load it.
        // Since this doesn't involve any decoding it shouldn't consume too much RAM.
        LoadTexture(texture);
    }

    textures_loaded = true;
}

void CustomTexManager::DumpTexture(const SurfaceParams& params, std::span<const u8> data) {
    const u64 data_hash = ComputeHash64(data.data(), data.size());
    const u32 data_size = static_cast<u32>(data.size());
    const u32 width = params.width;
    const u32 height = params.height;
    const PixelFormat format = params.pixel_format;

    // Check if it's been dumped already
    if (dumped_textures.contains(data_hash)) {
        return;
    }

    // Make sure the texture size is a power of 2.
    // If not, the surface is probably a framebuffer
    if (!IsPow2(width) || !IsPow2(height)) {
        LOG_WARNING(Render, "Not dumping {:016X} because size isn't a power of 2 ({}x{})",
                    data_hash, width, height);
        return;
    }

    // Allocate a temporary buffer for the thread to use
    const u32 decoded_size = width * height * 4;
    ScratchBuffer<u8> pixels(data_size + decoded_size);
    std::memcpy(pixels.Data(), data.data(), data_size);

    // Proceed with the dump.
    const u64 program_id = system.Kernel().GetCurrentProcess()->codeset->program_id;
    auto dump = [width, height, params, data_hash, format, data_size, program_id,
                 pixels = std::move(pixels)]() mutable {
        // Decode and convert to RGBA8
        const std::span encoded = pixels.Span().first(data_size);
        const std::span decoded = pixels.Span(data_size);
        DecodeTexture(params, params.addr, params.end, encoded, decoded,
                      params.type == SurfaceType::Color);

        std::string dump_path = fmt::format(
            "{}textures/{:016X}/", FileUtil::GetUserPath(FileUtil::UserPath::DumpDir), program_id);
        if (!FileUtil::CreateFullPath(dump_path)) {
            LOG_ERROR(Render, "Unable to create {}", dump_path);
            return;
        }

        dump_path += fmt::format("tex1_{}x{}_{:016X}_{}.png", width, height, data_hash, format);
        EncodePNG(dump_path, decoded, width, height);
    };

    workers.QueueWork(std::move(dump));
    dumped_textures.insert(data_hash);
}

const Texture& CustomTexManager::GetTexture(const SurfaceParams& params, std::span<u8> data) {
    u64 data_hash;
    if (compatibility_mode) {
        const u32 decoded_size =
            params.width * params.height * GetBytesPerPixel(params.pixel_format);
        ScratchBuffer<u8> decoded(decoded_size);
        DecodeTexture(params, params.addr, params.end, data, decoded.Span());
        data_hash = ComputeHash64(decoded.Data(), decoded_size);
    } else {
        data_hash = ComputeHash64(data.data(), data.size());
    }

    auto it = custom_textures.find(data_hash);
    if (it == custom_textures.end()) {
        LOG_WARNING(
            Render, "Unable to find replacement for {}x{} {} surface upload with hash {:016X}",
            params.width, params.height, PixelFormatAsString(params.pixel_format), data_hash);
        return dummy_texture;
    }

    LOG_DEBUG(Render, "Assigning {} to {}x{} {} surface with address {:#x} and hash {:016X}",
              it->second.path, params.width, params.height,
              PixelFormatAsString(params.pixel_format), params.addr, data_hash);

    return it->second;
}

void CustomTexManager::DecodeToStaging(const Texture& texture, const StagingData& staging) {
    switch (texture.file_format) {
    case CustomFileFormat::PNG:
        if (!DecodePNG(texture.data, staging.mapped)) {
            LOG_ERROR(Render, "Failed to decode png {}", texture.path);
        }
        if (compatibility_mode) {
            const u32 stride = texture.width * 4;
            // FlipTexture(staging.mapped, texture.width, texture.height, stride);
        }
        break;
    case CustomFileFormat::DDS:
    case CustomFileFormat::KTX:
        // Compressed formats don't need CPU decoding
        std::memcpy(staging.mapped.data(), texture.data.data(), texture.data.size());
        break;
    }
}

void CustomTexManager::LoadTexture(Texture& texture) {
    std::vector<u8>& data = texture.data;

    // Read the file
    auto file = FileUtil::IOFile(texture.path, "rb");
    data.resize(file.GetSize());
    file.ReadBytes(data.data(), file.GetSize());

    // Parse it based on the file extension
    switch (texture.file_format) {
    case CustomFileFormat::PNG:
        texture.format = CustomPixelFormat::RGBA8; // Check for other formats too?
        if (!ParsePNG(data, texture.staging_size, texture.width, texture.height)) {
            LOG_ERROR(Render, "Failed to parse png file {}", texture.path);
            return;
        }
        break;
    case CustomFileFormat::DDS:
    case CustomFileFormat::KTX:
        ddsktx_format format{};
        if (!ParseDDSKTX(data, texture.data, texture.width, texture.height, format)) {
            LOG_ERROR(Render, "Failed to parse dds/ktx file {}", texture.path);
            return;
        }
        texture.staging_size = texture.data.size();
        texture.format = ToCustomPixelFormat(format);
        break;
    }

    ASSERT_MSG(texture.width != 0 && texture.height != 0 && texture.staging_size != 0,
               "Invalid parameters read from {}", texture.path);
}

} // namespace VideoCore
