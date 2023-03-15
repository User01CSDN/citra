// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "video_core/rasterizer_cache/pixel_format.h"

namespace OpenGL {

bool CheckFormatsBlittable(PixelFormat source_format, PixelFormat dest_format) {
    SurfaceType source_type = GetFormatType(source_format);
    SurfaceType dest_type = GetFormatType(dest_format);

    if ((source_type == SurfaceType::Color || source_type == SurfaceType::Texture) &&
        (dest_type == SurfaceType::Color || dest_type == SurfaceType::Texture)) {
        return true;
    }

    if (source_type == SurfaceType::Depth && dest_type == SurfaceType::Depth) {
        return true;
    }

    if (source_type == SurfaceType::DepthStencil && dest_type == SurfaceType::DepthStencil) {
        return true;
    }

    LOG_WARNING(HW_GPU, "Unblittable format pair detected {} and {}", GetFormatName(source_format),
                GetFormatName(dest_format));
    return false;
}

PixelFormat PixelFormatFromTextureFormat(Pica::TexturingRegs::TextureFormat format) {
    switch (format) {
    case Pica::TexturingRegs::TextureFormat::RGBA8:
        return PixelFormat::RGBA8;
    case Pica::TexturingRegs::TextureFormat::RGB8:
        return PixelFormat::RGB8;
    case Pica::TexturingRegs::TextureFormat::RGB5A1:
        return PixelFormat::RGB5A1;
    case Pica::TexturingRegs::TextureFormat::RGB565:
        return PixelFormat::RGB565;
    case Pica::TexturingRegs::TextureFormat::RGBA4:
        return PixelFormat::RGBA4;
    case Pica::TexturingRegs::TextureFormat::IA8:
        return PixelFormat::IA8;
    case Pica::TexturingRegs::TextureFormat::RG8:
        return PixelFormat::RG8;
    case Pica::TexturingRegs::TextureFormat::I8:
        return PixelFormat::I8;
    case Pica::TexturingRegs::TextureFormat::A8:
        return PixelFormat::A8;
    case Pica::TexturingRegs::TextureFormat::IA4:
        return PixelFormat::IA4;
    case Pica::TexturingRegs::TextureFormat::I4:
        return PixelFormat::I4;
    case Pica::TexturingRegs::TextureFormat::A4:
        return PixelFormat::A4;
    case Pica::TexturingRegs::TextureFormat::ETC1:
        return PixelFormat::ETC1;
    case Pica::TexturingRegs::TextureFormat::ETC1A4:
        return PixelFormat::ETC1A4;
    default:
        return PixelFormat::Invalid;
    }
}

PixelFormat PixelFormatFromColorFormat(Pica::FramebufferRegs::ColorFormat format) {
    switch (format) {
    case Pica::FramebufferRegs::ColorFormat::RGBA8:
        return PixelFormat::RGBA8;
    case Pica::FramebufferRegs::ColorFormat::RGB8:
        return PixelFormat::RGB8;
    case Pica::FramebufferRegs::ColorFormat::RGB5A1:
        return PixelFormat::RGB5A1;
    case Pica::FramebufferRegs::ColorFormat::RGB565:
        return PixelFormat::RGB565;
    case Pica::FramebufferRegs::ColorFormat::RGBA4:
        return PixelFormat::RGBA4;
    default:
        return PixelFormat::Invalid;
    }
}

PixelFormat PixelFormatFromDepthFormat(Pica::FramebufferRegs::DepthFormat format) {
    switch (format) {
    case Pica::FramebufferRegs::DepthFormat::D16:
        return PixelFormat::D16;
    case Pica::FramebufferRegs::DepthFormat::D24:
        return PixelFormat::D24;
    case Pica::FramebufferRegs::DepthFormat::D24S8:
        return PixelFormat::D24S8;
    default:
        return PixelFormat::Invalid;
    }
}

PixelFormat PixelFormatFromGPUPixelFormat(GPU::Regs::PixelFormat format) {
    switch (format) {
    case GPU::Regs::PixelFormat::RGBA8:
        return PixelFormat::RGBA8;
    case GPU::Regs::PixelFormat::RGB8:
        return PixelFormat::RGB8;
    case GPU::Regs::PixelFormat::RGB565:
        return PixelFormat::RGB565;
    case GPU::Regs::PixelFormat::RGB5A1:
        return PixelFormat::RGB5A1;
    case GPU::Regs::PixelFormat::RGBA4:
        return PixelFormat::RGBA4;
    default:
        return PixelFormat::Invalid;
    }
}

} // namespace OpenGL
