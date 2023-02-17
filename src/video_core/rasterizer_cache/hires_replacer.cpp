// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/bit_util.h"
#include "common/file_util.h"
#include "common/scratch_buffer.h"
#include "common/image_util.h"
#include "core/core.h"
#include "video_core/rasterizer_cache/hires_replacer.h"
#include "video_core/rasterizer_cache/surface_base.h"

namespace VideoCore {

HiresReplacer::HiresReplacer() :
    workers{std::max(std::thread::hardware_concurrency(), 2U) - 1, "Hires processing"} {

}

void HiresReplacer::DumpSurface(const SurfaceBase& surface, std::span<const u8> data) {
    const u32 data_hash = Common::ComputeHash64(data.data(), data.size());
    const u32 width = surface.width;
    const u32 height = surface.height;
    const PixelFormat format = surface.pixel_format;

    // Check if it's been dumped already
    if (dumped_surfaces.contains(data_hash)) {
        return;
    }

    // If this is a partial update do not dump it, it's probably not a texture
    if (surface.BytesInPixels(width * height) != data.size()) {
        LOG_WARNING(Render, "Not dumping {:016X} because it's a partial texture update");
        return;
    }

    // Make sure the texture size is a power of 2.
    // If not, the surface is probably a framebuffer
    if (!Common::IsPow2(surface.width) || !Common::IsPow2(surface.height)) {
        LOG_WARNING(Render, "Not dumping {:016X} because size isn't a power of 2 ({}x{})",
                    data_hash, width, height);
        return;
    }

    // Allocate a temporary buffer for the thread to use
    Common::ScratchBuffer<u8> pixels(data.size());
    std::memcpy(pixels.Data(), data.data(), data.size());

    // Proceed with the dump. The texture should be already decoded
    const u64 program_id = Core::System::GetInstance().Kernel().GetCurrentProcess()->codeset->program_id;
    const auto dump = [width, height, data_hash, format, program_id, pixels = std::move(pixels)]() {
        std::string dump_path =
            fmt::format("{}textures/{:016X}/", FileUtil::GetUserPath(FileUtil::UserPath::DumpDir), program_id);
        if (!FileUtil::CreateFullPath(dump_path)) {
            LOG_ERROR(Render, "Unable to create {}", dump_path);
            return;
        }

        dump_path += fmt::format("tex1_{}x{}_{:016X}_{}.png", width, height, data_hash, format);
        Common::EncodePNG(pixels.Span(), dump_path, width, height, width, 0);
    };

    dump();
}

} // namespace VideoCore
