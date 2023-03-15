// Copyright 2022 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/math_util.h"
#include "video_core/rasterizer_cache/rasterizer_cache_utils.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"

namespace OpenGL {

struct FormatTuple;
class CachedSurface;
struct CachedTextureCube;

/**
 * Provides texture manipulation functions to the rasterizer cache
 * Separating this into a class makes it easier to abstract graphics API code
 */
class TextureRuntime {
    friend class CachedSurface;
public:
    TextureRuntime();
    ~TextureRuntime() = default;

    /// Maps an internal staging buffer of the provided size of pixel uploads/downloads
    StagingData FindStaging(u32 size, bool upload);

    /// Fills the rectangle of the texture with the clear value provided
    bool ClearTexture(CachedSurface& surface, const TextureClear& clear);

    /// Copies a rectangle of source to another rectange of dest
    bool CopyTextures(CachedSurface& source, CachedSurface& dest, const TextureCopy& copy);

    /// Copies a rectangle of source to a face of dest cube
    bool CopyTextures(CachedSurface& source, CachedTextureCube& dest, const TextureCopy& copy);

    /// Blits a rectangle of source to another rectange of dest
    bool BlitTextures(CachedSurface& source, CachedSurface& dest, const TextureBlit& blit);

    /// Generates mipmaps for all the available levels of the texture
    void GenerateMipmaps(CachedSurface& surface, u32 max_level);

private:
    /// Copies the GPU pixel data to the provided pixel buffer
    void ReadTexture(OGLTexture& texture, Common::Rectangle<u32> rect, PixelFormat format,
                     GLint level, std::span<u8> pixels) const;

private:
    std::vector<u8> staging_buffer;
    OGLFramebuffer read_fbo, draw_fbo;
};

} // namespace OpenGL
