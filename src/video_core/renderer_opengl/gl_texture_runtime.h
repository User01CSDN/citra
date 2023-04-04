// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "video_core/rasterizer_cache/surface_base.h"
#include "video_core/renderer_opengl/gl_format_reinterpreter.h"
#include "video_core/renderer_opengl/texture_filters/texture_filterer.h"

namespace VideoCore {
class RendererBase;
}

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
    VideoCore::TextureType type{};
    u32 width = 0;
    u32 height = 0;
    u32 levels = 1;
    u32 res_scale = 1;

    bool operator==(const HostTextureTag& other) const noexcept {
        return std::tie(tuple, type, width, height, levels, res_scale) ==
               std::tie(other.tuple, other.type, other.width, other.height, other.levels,
                        other.res_scale);
    }

    struct Hash {
        const u64 operator()(const HostTextureTag& tag) const {
            return Common::ComputeHash64(&tag, sizeof(HostTextureTag));
        }
    };
};

struct Allocation {
    std::array<OGLTexture, 2> textures;
    std::array<GLuint, 2> handles;
    FormatTuple tuple;
    u32 width;
    u32 height;
    u32 levels;
    u32 res_scale;

    operator bool() const noexcept {
        return textures[0].handle;
    }

    bool Matches(u32 width_, u32 height_, u32 levels_, const FormatTuple& tuple_) const {
        return std::tie(width, height, levels, tuple) == std::tie(width_, height_, levels_, tuple_);
    }
};

class Surface;
class Driver;
struct CachedTextureCube;

/**
 * Provides texture manipulation functions to the rasterizer cache
 * Separating this into a class makes it easier to abstract graphics API code
 */
class TextureRuntime {
    friend class Surface;

public:
    explicit TextureRuntime(Driver& driver, VideoCore::RendererBase& renderer);
    ~TextureRuntime();

    /// Returns true if no texture filter is in use
    bool IsNullFilter() const noexcept {
        return filterer.IsNull();
    }

    /// Resets texture filtering settings to the global settings
    bool ResetFilter(u32 scale_factor);

    /// Maps an internal staging buffer of the provided size of pixel uploads/downloads
    VideoCore::StagingData FindStaging(u32 size, bool upload);

    /// Returns the OpenGL format tuple associated with the provided pixel format
    const FormatTuple& GetFormatTuple(VideoCore::PixelFormat pixel_format) const;

    /// Takes back ownership of the allocation for recycling
    void Recycle(const HostTextureTag tag, Allocation&& alloc);

    /// Allocates an OpenGL texture with the specified dimentions and format
    Allocation Allocate(const VideoCore::SurfaceParams& params);

    /// Fills the rectangle of the texture with the clear value provided
    bool ClearTexture(Surface& surface, const VideoCore::TextureClear& clear);

    /// Copies a rectangle of source to another rectange of dest
    bool CopyTextures(Surface& source, Surface& dest, const VideoCore::TextureCopy& copy);

    /// Copies a rectangle of source to a face of dest cube
    bool CopyTextures(Surface& source, CachedTextureCube& dest, const VideoCore::TextureCopy& copy);

    /// Blits a rectangle of source to another rectange of dest
    bool BlitTextures(Surface& source, Surface& dest, const VideoCore::TextureBlit& blit);

    /// Generates mipmaps for all the available levels of the texture
    void GenerateMipmaps(Surface& surface, u32 max_level);

    /// Returns all source formats that support reinterpretation to the dest format
    const ReinterpreterList& GetPossibleReinterpretations(VideoCore::PixelFormat dest_format) const;

private:
    /// Returns the OpenGL driver class
    const Driver& GetDriver() const {
        return driver;
    }

    /// Returns the class that handles texture filtering
    const TextureFilterer& GetFilterer() const {
        return filterer;
    }

private:
    Driver& driver;
    TextureFilterer filterer;
    std::vector<u8> staging_buffer;
    std::array<ReinterpreterList, VideoCore::PIXEL_FORMAT_COUNT> reinterpreters;
    std::unordered_multimap<HostTextureTag, Allocation, HostTextureTag::Hash> texture_recycler;
    std::array<OGLFramebuffer, 3> draw_fbos;
    std::array<OGLFramebuffer, 3> read_fbos;
};

class Surface : public VideoCore::SurfaceBase {
public:
    explicit Surface(TextureRuntime& runtime, const VideoCore::SurfaceParams& params);
    ~Surface();

    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    Surface(Surface&& o) noexcept = default;
    Surface& operator=(Surface&& o) noexcept = default;

    /// Returns the surface image handle
    GLuint Handle(bool scaled = true) const noexcept {
        return alloc.handles[static_cast<u32>(scaled)];
    }

    /// Uploads pixel data in staging to a rectangle region of the surface texture
    void Upload(const VideoCore::BufferTextureCopy& upload, const VideoCore::StagingData& staging);

    /// Downloads pixel data to staging from a rectangle region of the surface texture
    void Download(const VideoCore::BufferTextureCopy& download,
                  const VideoCore::StagingData& staging);

    /// Attaches a handle of surface to the specified framebuffer target
    void Attach(GLenum target, u32 level, u32 layer, bool scaled = true);

    /// Returns the bpp of the internal surface format
    u32 GetInternalBytesPerPixel() const {
        return GetFormatBytesPerPixel(pixel_format);
    }

private:
    /// Performs blit between the scaled/unscaled images
    void BlitScale(const VideoCore::TextureBlit& blit, bool up_scale);

    /// Attempts to download without using an fbo
    bool DownloadWithoutFbo(const VideoCore::BufferTextureCopy& download,
                            const VideoCore::StagingData& staging);

private:
    TextureRuntime* runtime;
    Driver* driver;
    Allocation alloc{};
};

} // namespace OpenGL
