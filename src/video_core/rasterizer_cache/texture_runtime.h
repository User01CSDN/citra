// Copyright 2022 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/math_util.h"
#include "video_core/rasterizer_cache/rasterizer_cache_utils.h"
#include "video_core/rasterizer_cache/surface_base.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"

namespace OpenGL {

struct FormatTuple {
    GLint internal_format;
    GLenum format;
    GLenum type;

    bool operator==(const FormatTuple& other) const noexcept {
        return std::tie(internal_format, format, type) ==
               std::tie(other.internal_format, other.format, other.type);
    }
};

struct HostTextureTag {
    FormatTuple tuple{};
    TextureType type{};
    u32 width = 0;
    u32 height = 0;
    u32 levels = 1;

    bool operator==(const HostTextureTag& other) const noexcept {
        return std::tie(tuple, type, width, height, levels) ==
               std::tie(other.tuple, other.type, other.width, other.height, other.levels);
    }

    struct Hash {
        const u64 operator()(const HostTextureTag& tag) const {
            return Common::ComputeHash64(&tag, sizeof(HostTextureTag));
        }
    };
};

struct Allocation {
    OGLTexture texture;
    FormatTuple tuple;
    u32 width;
    u32 height;
    u32 levels;

    bool Matches(u32 width_, u32 height_, u32 levels_, const FormatTuple& tuple_) const {
        return std::tie(width, height, levels, tuple) == std::tie(width_, height_, levels_, tuple_);
    }
};

class Surface;
struct CachedTextureCube;

/**
 * Provides texture manipulation functions to the rasterizer cache
 * Separating this into a class makes it easier to abstract graphics API code
 */
class TextureRuntime {
    friend class Surface;

public:
    explicit TextureRuntime();
    ~TextureRuntime();

    /// Maps an internal staging buffer of the provided size of pixel uploads/downloads
    StagingData FindStaging(u32 size, bool upload);

    /// Returns the OpenGL format tuple associated with the provided pixel format
    static const FormatTuple& GetFormatTuple(PixelFormat pixel_format);

    /// Takes back ownership of the allocation for recycling
    void Recycle(const HostTextureTag tag, Allocation&& alloc);

    /// Allocates an OpenGL texture with the specified dimentions and format
    Allocation Allocate(u32 width, u32 height, u32 levels, const FormatTuple& tuple,
                        TextureType type);

    /// Fills the rectangle of the texture with the clear value provided
    bool ClearTexture(Surface& surface, const TextureClear& clear);

    /// Copies a rectangle of source to another rectange of dest
    bool CopyTextures(Surface& source, Surface& dest, const TextureCopy& copy);

    /// Copies a rectangle of source to a face of dest cube
    bool CopyTextures(Surface& source, CachedTextureCube& dest, const TextureCopy& copy);

    /// Blits a rectangle of source to another rectange of dest
    bool BlitTextures(Surface& source, Surface& dest, const TextureBlit& blit);

    /// Generates mipmaps for all the available levels of the texture
    void GenerateMipmaps(Surface& surface, u32 max_level);

private:
    /// Copies the GPU pixel data to the provided pixel buffer
    void ReadTexture(OGLTexture& texture, Common::Rectangle<u32> rect, PixelFormat format,
                     GLint level, std::span<u8> pixels) const;

private:
    std::vector<u8> staging_buffer;
    OGLFramebuffer read_fbo, draw_fbo;
    std::unordered_multimap<HostTextureTag, Allocation, HostTextureTag::Hash> texture_recycler;
};

class Surface : public SurfaceBase {
public:
    explicit Surface(TextureRuntime& runtime, const SurfaceParams& params);
    ~Surface();

    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    Surface(Surface&& o) noexcept = default;
    Surface& operator=(Surface&& o) noexcept = default;

    /// Returns the surface image handle
    GLuint Handle() const noexcept {
        return alloc.texture.handle;
    }

    /// Returns the surface texture
    OGLTexture& Texture() {
        return alloc.texture;
    }

    /// Uploads pixel data in staging to a rectangle region of the surface texture
    void Upload(const BufferTextureCopy& upload, const StagingData& staging);

    /// Downloads pixel data to staging from a rectangle region of the surface texture
    void Download(const BufferTextureCopy& download, const StagingData& staging);

    /// Returns the bpp of the internal surface format
    u32 GetInternalBytesPerPixel() const {
        return GetBytesPerPixel(pixel_format);
    }

private:
    TextureRuntime* runtime;
    Allocation alloc{};
};

} // namespace OpenGL
