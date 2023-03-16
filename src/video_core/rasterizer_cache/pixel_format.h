// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <limits>
#include <string_view>
#include "core/hw/gpu.h"
#include "video_core/regs_framebuffer.h"
#include "video_core/regs_texturing.h"

namespace VideoCore {

enum class PixelFormat : u32 {
    RGBA8 = 0,
    RGB8 = 1,
    RGB5A1 = 2,
    RGB565 = 3,
    RGBA4 = 4,
    IA8 = 5,
    RG8 = 6,
    I8 = 7,
    A8 = 8,
    IA4 = 9,
    I4 = 10,
    A4 = 11,
    ETC1 = 12,
    ETC1A4 = 13,
    D16 = 14,
    D24 = 16,
    D24S8 = 17,
    MaxPixelFormat = 18,
    Invalid = std::numeric_limits<u32>::max(),
};
constexpr std::size_t PIXEL_FORMAT_COUNT = static_cast<std::size_t>(PixelFormat::MaxPixelFormat);

enum class SurfaceType : u32 {
    Color = 0,
    Texture = 1,
    Depth = 2,
    DepthStencil = 3,
    Fill = 4,
    Invalid = 5,
};

enum class TextureType : u32 {
    Texture2D = 0,
    CubeMap = 1,
};

struct PixelFormatInfo {
    SurfaceType type;
    std::string_view name;
    u32 bits_per_block;
    u32 bytes_per_pixel;
};

/**
 * Lookup table for querying pixel format properties (type, name, etc)
 * @note Modern GPUs require 4 byte alignment for D24
 * @note Texture formats are automatically converted to RGBA8
 **/
constexpr std::array<PixelFormatInfo, PIXEL_FORMAT_COUNT> FORMAT_MAP = {{
    {SurfaceType::Color, "RGBA8", 32, 4},  // RGBA8
    {SurfaceType::Color, "RGB8", 24, 3},   // RGB8
    {SurfaceType::Color, "RGB5A1", 16, 2}, // RGB5A1
    {SurfaceType::Color, "RGB565", 16, 2}, // RGB565
    {SurfaceType::Color, "RGBA4", 16, 2},  // RGBA4
    {SurfaceType::Texture, "IA8", 16, 4},  // IA8
    {SurfaceType::Texture, "RG8", 16, 4},  // RG8
    {SurfaceType::Texture, "I8", 8, 4},    // I8
    {SurfaceType::Texture, "A8", 8, 4},    // A8
    {SurfaceType::Texture, "IA4", 8, 4},   // IA4
    {SurfaceType::Texture, "I4", 4, 4},    // I4
    {SurfaceType::Texture, "A4", 4, 4},    // A4
    {SurfaceType::Texture, "ΕTC1", 4, 4},  // ETC1
    {SurfaceType::Texture, "A4", 8, 4},    // ETC1A4
    {SurfaceType::Depth, "D16", 16, 2},    // D16
    {SurfaceType::Invalid, "Invalid", 0},
    {SurfaceType::Depth, "D24", 24, 4},          // D24
    {SurfaceType::DepthStencil, "D24S8", 32, 4}, // D24S8
}};

constexpr u32 GetFormatBpp(PixelFormat format) {
    const std::size_t index = static_cast<std::size_t>(format);
    ASSERT(index < FORMAT_MAP.size());
    return FORMAT_MAP[index].bits_per_block;
}

constexpr u32 GetBytesPerPixel(PixelFormat format) {
    const std::size_t index = static_cast<std::size_t>(format);
    ASSERT(index < FORMAT_MAP.size());
    return FORMAT_MAP[index].bytes_per_pixel;
}

constexpr SurfaceType GetFormatType(PixelFormat format) {
    const std::size_t index = static_cast<std::size_t>(format);
    ASSERT(index < FORMAT_MAP.size());
    return FORMAT_MAP[index].type;
}

constexpr std::string_view GetFormatName(PixelFormat format) {
    const std::size_t index = static_cast<std::size_t>(format);
    ASSERT(index < FORMAT_MAP.size());
    return FORMAT_MAP[index].name;
}

bool CheckFormatsBlittable(PixelFormat source_format, PixelFormat dest_format);

PixelFormat PixelFormatFromTextureFormat(Pica::TexturingRegs::TextureFormat format);

PixelFormat PixelFormatFromColorFormat(Pica::FramebufferRegs::ColorFormat format);

PixelFormat PixelFormatFromDepthFormat(Pica::FramebufferRegs::DepthFormat format);

PixelFormat PixelFormatFromGPUPixelFormat(GPU::Regs::PixelFormat format);

} // namespace VideoCore
