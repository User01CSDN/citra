// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/file_util.h"
#include "common/hash.h"
#include "common/texture.h"
#include "core/core.h"
#include "video_core/rasterizer_cache/custom_tex_manager.h"
#include "video_core/rasterizer_cache/surface_params.h"

namespace VideoCore {

namespace {

bool IsPow2(u32 value) {
    return value != 0 && (value & (value - 1)) == 0;
}

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
        return CustomPixelFormat::ASTC4;
    case DDSKTX_FORMAT_ASTC6x6:
        return CustomPixelFormat::ASTC6;
    case DDSKTX_FORMAT_ASTC8x6:
        return CustomPixelFormat::ASTC8;
    default:
        LOG_ERROR(Common, "Unknown dds/ktx pixel format {}", format);
        return CustomPixelFormat::RGBA8;
    }
}

} // Anonymous namespace

CustomTexManager::CustomTexManager(Core::System& system_)
    : system{system_}, image_interface{*system.GetImageInterface()} {}

CustomTexManager::~CustomTexManager() = default;

void CustomTexManager::FindCustomTextures() {
    if (textures_loaded) {
        return;
    }

    // If custom textures aren't enabled we don't want to create the thread pool.
    // So do it here instead.
    workers = std::make_unique<Common::ThreadWorker>(
        std::max(std::thread::hardware_concurrency(), 2U) - 1, "Custom textures");

    // Custom textures are currently stored as
    // [TitleID]/tex1_[width]x[height]_[64-bit hash]_[format].png
    const u64 program_id = system.Kernel().GetCurrentProcess()->codeset->program_id;
    const std::string load_path =
        fmt::format("{}textures/{:016X}/", GetUserPath(FileUtil::UserPath::LoadDir), program_id);

    // Create the directory if it did not exist
    if (!FileUtil::Exists(load_path)) {
        FileUtil::CreateFullPath(load_path);
    }

    FileUtil::FSTEntry texture_dir;
    std::vector<FileUtil::FSTEntry> textures;
    // 64 nested folders should be plenty for most cases
    FileUtil::ScanDirectoryTree(load_path, texture_dir, 64);
    FileUtil::GetAllFilesFromNestedEntries(texture_dir, textures);

    // Reserve space for all the textures in the folder
    const std::size_t num_textures = textures.size();
    custom_textures.resize(num_textures);

    u32 width{};
    u32 height{};
    u32 format{};
    unsigned long long hash{};
    char ext[3];
    for (std::size_t i = 0; i < textures.size(); i++) {
        const auto& file = textures[i];
        const std::string& path = file.physicalName;
        if (file.isDirectory || !file.virtualName.starts_with("tex1_")) {
            continue;
        }

        // Parse the texture filename. We only really care about the hash,
        // the rest will be queried from the file itself.
        if (std::sscanf(file.virtualName.c_str(), "tex1_%ux%u_%llX_%u.%s", &width, &height, &hash,
                        &format, ext) != 5) {
            continue;
        }

        custom_textures[i] = std::make_unique<CustomTexture>();
        CustomTexture& texture = *custom_textures[i];

        // Fill in relevant information
        texture.file_format = MakeFileFormat(ext);
        texture.hash = hash;
        texture.path = path;
    }

    // Assign each texture to the hash map
    for (const auto& texture : custom_textures) {
        if (!texture) {
            continue;
        }
        const unsigned long long hash = texture->hash;
        auto [it, new_texture] = custom_texture_map.try_emplace(hash);
        if (!new_texture) {
            LOG_ERROR(Render, "Textures {} and {} conflict, ignoring!",
                      custom_texture_map[hash]->path, texture->path);
            continue;
        }
        it->second = texture.get();
    }

    textures_loaded = true;
}

void CustomTexManager::PreloadTextures() {
    const std::size_t num_textures = custom_textures.size();
    const std::size_t num_workers{workers->NumWorkers()};
    const std::size_t bucket_size{num_textures / num_workers};

    for (std::size_t i = 0; i < num_workers; ++i) {
        const bool is_last_worker = i + 1 == num_workers;
        const std::size_t start{bucket_size * i};
        const std::size_t end{is_last_worker ? num_textures : start + bucket_size};
        workers->QueueWork([this, start, end]() {
            for (std::size_t i = start; i < end; i++) {
                if (!custom_textures[i]) {
                    continue;
                }
                LoadTexture(custom_textures[i].get());
            }
        });
    }
    workers->WaitForRequests();
}

u64 CustomTexManager::ComputeHash(const SurfaceParams& params, std::span<u8> data) {
    const u32 decoded_size = params.width * params.height * GetBytesPerPixel(params.pixel_format);
    if (temp_buffer.size() < decoded_size) {
        temp_buffer.resize(decoded_size);
    }

    // This is suboptimal as we could just hash the 3DS data instead.
    // However in the interest of compatibility with old texture packs
    // this must be done...
    const auto decoded = std::span{temp_buffer.data(), decoded_size};
    DecodeTexture(params, params.addr, params.end, data, decoded);
    return Common::ComputeHash64(decoded.data(), decoded_size);
}

void CustomTexManager::DumpTexture(const SurfaceParams& params, u32 level, std::span<u8> data) {
    const u64 data_hash = ComputeHash(params, data);
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
    std::vector<u8> pixels(data_size + decoded_size);
    std::memcpy(pixels.data(), data.data(), data_size);

    // Proceed with the dump.
    const u64 program_id = system.Kernel().GetCurrentProcess()->codeset->program_id;
    auto dump = [this, width, height, params, data_hash, format, data_size, decoded_size,
                 program_id, pixels = std::move(pixels)]() mutable {
        // Decode and convert to RGBA8
        const std::span encoded = std::span{pixels}.first(data_size);
        const std::span decoded = std::span{pixels}.last(decoded_size);
        DecodeTexture(params, params.addr, params.end, encoded, decoded,
                      params.type == SurfaceType::Color);

        std::string dump_path = fmt::format(
            "{}textures/{:016X}/", FileUtil::GetUserPath(FileUtil::UserPath::DumpDir), program_id);
        if (!FileUtil::CreateFullPath(dump_path)) {
            LOG_ERROR(Render, "Unable to create {}", dump_path);
            return;
        }

        dump_path += fmt::format("tex1_{}x{}_{:016X}_{}.png", width, height, data_hash, format);
        image_interface.EncodePNG(dump_path, decoded, width, height);
    };

    workers->QueueWork(std::move(dump));
    dumped_textures.insert(data_hash);
}

CustomTexture* CustomTexManager::GetTexture(u64 data_hash) {
    const auto it = custom_texture_map.find(data_hash);
    if (it == custom_texture_map.end()) {
        LOG_WARNING(Render, "Unable to find replacement for surface with hash {:016X}", data_hash);
        return nullptr;
    }

    CustomTexture* texture = it->second;
    LOG_DEBUG(Render, "Assigning {} to surface with hash {:016X}", texture->path, data_hash);

    return texture;
}

void CustomTexManager::QueueDecode(CustomTexture* texture) {
    // Don't queue another decode is one is pending.
    if (texture->IsPending()) {
        LOG_WARNING(Render, "Texture requested while pending decode!");
        texture->state.wait(DecodeState::Pending);
        return;
    }

    // Queue the decode
    texture->state = DecodeState::Pending;
    workers->QueueWork([this, texture] { LoadTexture(texture); });
}

void CustomTexManager::LoadTexture(CustomTexture* texture) {
    FileUtil::IOFile file{texture->path, "rb"};
    const size_t read_size = file.GetSize();
    std::vector<u8> in(read_size);
    if (file.ReadBytes(in.data(), read_size) != read_size) {
        LOG_CRITICAL(Frontend, "Failed to open {}", texture->path);
    }

    switch (texture->file_format) {
    case CustomFileFormat::PNG: {
        if (!image_interface.DecodePNG(in, texture->data, texture->width, texture->height)) {
            LOG_ERROR(Render, "Failed to decode png {}", texture->path);
        }
        if (flip_png_files) {
            Common::FlipRGBA8Texture(texture->data, texture->width, texture->height);
        }
        texture->format = CustomPixelFormat::RGBA8;
        break;
    }
    case CustomFileFormat::DDS:
    case CustomFileFormat::KTX: {
        // Compressed formats don't need CPU decoding and must be pre-flipped.
        ddsktx_format format{};
        image_interface.DecodeDDS(in, texture->data, texture->width, texture->height, format);
        texture->format = ToCustomPixelFormat(format);
        break;
    }
    default:
        LOG_ERROR(Render, "Unknown file format {}", texture->file_format);
        return;
    }
    texture->MarkDecoded();
}

} // namespace VideoCore
