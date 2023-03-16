// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "video_core/rasterizer_cache/surface_params.h"
#include "video_core/rasterizer_cache/texture_codec.h"
#include "video_core/rasterizer_cache/utils.h"
#include "video_core/texture/texture_decode.h"

namespace VideoCore {

ClearValue MakeClearValue(SurfaceType type, PixelFormat format, const u8* fill_data) {
    ClearValue result{};
    switch (type) {
    case SurfaceType::Color:
    case SurfaceType::Texture:
    case SurfaceType::Fill: {
        Pica::Texture::TextureInfo tex_info{};
        tex_info.format = static_cast<Pica::TexturingRegs::TextureFormat>(format);
        const auto color = Pica::Texture::LookupTexture(fill_data, 0, 0, tex_info);
        result.color = color / 255.f;
        break;
    }
    case SurfaceType::Depth: {
        u32 depth_uint = 0;
        if (format == PixelFormat::D16) {
            std::memcpy(&depth_uint, fill_data, 2);
            result.depth = depth_uint / 65535.0f; // 2^16 - 1
        } else if (format == PixelFormat::D24) {
            std::memcpy(&depth_uint, fill_data, 3);
            result.depth = depth_uint / 16777215.0f; // 2^24 - 1
        }
        break;
    }
    case SurfaceType::DepthStencil: {
        u32 clear_value_uint;
        std::memcpy(&clear_value_uint, fill_data, sizeof(u32));
        result.depth = (clear_value_uint & 0xFFFFFF) / 16777215.0f; // 2^24 - 1
        result.stencil = (clear_value_uint >> 24);
        break;
    }
    default:
        UNREACHABLE_MSG("Invalid surface type!");
    }
    return result;
}

u32 MipLevels(u32 width, u32 height, u32 max_level) {
    u32 levels = 1;
    while (width > 8 && height > 8) {
        levels++;
        width >>= 1;
        height >>= 1;
    }

    return std::min(levels, max_level + 1);
}

void EncodeTexture(const SurfaceParams& surface_info, PAddr start_addr, PAddr end_addr,
                   std::span<u8> source, std::span<u8> dest, bool convert) {
    const PixelFormat format = surface_info.pixel_format;
    const u32 func_index = static_cast<u32>(format);

    if (surface_info.is_tiled) {
        const MortonFunc SwizzleImpl =
            (convert ? SWIZZLE_TABLE_CONVERTED : SWIZZLE_TABLE)[func_index];
        if (SwizzleImpl) {
            SwizzleImpl(surface_info.width, surface_info.height, start_addr - surface_info.addr,
                        end_addr - surface_info.addr, source, dest);
            return;
        }
    } else {
        const LinearFunc LinearEncodeImpl =
            (convert ? LINEAR_ENCODE_TABLE_CONVERTED : LINEAR_ENCODE_TABLE)[func_index];
        if (LinearEncodeImpl) {
            LinearEncodeImpl(source, dest);
            return;
        }
    }

    LOG_ERROR(HW_GPU, "Unimplemented texture encode function for pixel format = {}, tiled = {}",
              func_index, surface_info.is_tiled);
    UNREACHABLE();
}

void DecodeTexture(const SurfaceParams& surface_info, PAddr start_addr, PAddr end_addr,
                   std::span<u8> source, std::span<u8> dest, bool convert) {
    const PixelFormat format = surface_info.pixel_format;
    const u32 func_index = static_cast<u32>(format);

    if (surface_info.is_tiled) {
        const MortonFunc UnswizzleImpl =
            (convert ? UNSWIZZLE_TABLE_CONVERTED : UNSWIZZLE_TABLE)[func_index];
        if (UnswizzleImpl) {
            UnswizzleImpl(surface_info.width, surface_info.height, start_addr - surface_info.addr,
                          end_addr - surface_info.addr, dest, source);
            return;
        }
    } else {
        const LinearFunc LinearDecodeImpl =
            (convert ? LINEAR_DECODE_TABLE_CONVERTED : LINEAR_DECODE_TABLE)[func_index];
        if (LinearDecodeImpl) {
            LinearDecodeImpl(source, dest);
            return;
        }
    }

    LOG_ERROR(HW_GPU, "Unimplemented texture decode function for pixel format = {}, tiled = {}",
              func_index, surface_info.is_tiled);
    UNREACHABLE();
}

} // namespace VideoCore
