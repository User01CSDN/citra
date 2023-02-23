// Copyright 2022 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <set>
#include <span>
#include "video_core/rasterizer_cache/framebuffer_base.h"
#include "video_core/rasterizer_cache/rasterizer_cache_base.h"
#include "video_core/rasterizer_cache/surface_base.h"
#include "video_core/renderer_opengl/gl_format_reinterpreter.h"
#include "video_core/renderer_opengl/gl_stream_buffer.h"
#include "video_core/renderer_opengl/texture_filters/texture_filterer.h"

namespace VideoCore {
enum class CustomPixelFormat : u32;
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

struct HostTextureTag {
    FormatTuple tuple{};
    VideoCore::TextureType type{};
    u32 width = 0;
    u32 height = 0;
    u32 levels = 1;

    bool operator==(const HostTextureTag& other) const noexcept {
        return std::tie(tuple, type, width, height, levels) ==
               std::tie(other.tuple, other.type, other.width, other.height, other.levels);
    }

    const u64 Hash() const {
        return Common::ComputeHash64(this, sizeof(HostTextureTag));
    }
};

} // namespace OpenGL

namespace std {
template <>
struct hash<OpenGL::HostTextureTag> {
    std::size_t operator()(const OpenGL::HostTextureTag& tag) const noexcept {
        return tag.Hash();
    }
};
} // namespace std

namespace OpenGL {

class Driver;
class Surface;

/**
 * Provides texture manipulation functions to the rasterizer cache
 * Separating this into a class makes it easier to abstract graphics API code
 */
class TextureRuntime {
    friend class Surface;
    friend class Framebuffer;

public:
    explicit TextureRuntime(Driver& driver);
    ~TextureRuntime();

    /// Maps an internal staging buffer of the provided size of pixel uploads/downloads
    VideoCore::StagingData FindStaging(u32 size, bool upload);

    /// Returns the OpenGL format tuple associated with the provided pixel format
    const FormatTuple& GetFormatTuple(VideoCore::PixelFormat pixel_format);
    const FormatTuple& GetFormatTuple(VideoCore::CustomPixelFormat pixel_format);

    /// Causes a GPU command flush
    void Finish() const {}

    /// Destroys runtime cached resources
    void Clear();

    /// Takes back ownership of the allocation for recycling
    void Recycle(const HostTextureTag tag, Allocation&& alloc);

    /// Allocates an OpenGL texture with the specified dimentions and format
    Allocation Allocate(u32 width, u32 height, u32 levels, const FormatTuple& tuple,
                        VideoCore::TextureType type);

    /// Fills the rectangle of the texture with the clear value provided
    bool ClearTexture(Surface& surface, const VideoCore::TextureClear& clear);

    /// Copies a rectangle of src_tex to another rectange of dst_rect
    bool CopyTextures(Surface& source, Surface& dest, const VideoCore::TextureCopy& copy);

    /// Blits a rectangle of src_tex to another rectange of dst_rect
    bool BlitTextures(Surface& surface, Surface& dest, const VideoCore::TextureBlit& blit);

    /// Generates mipmaps for all the available levels of the texture
    void GenerateMipmaps(Surface& surface);

    /// Returns all source formats that support reinterpretation to the dest format
    [[nodiscard]] const ReinterpreterList& GetPossibleReinterpretations(
        VideoCore::PixelFormat dest_format) const;

    /// Returns true if the provided pixel format needs convertion
    [[nodiscard]] bool NeedsConvertion(VideoCore::PixelFormat format) const;

private:
    /// Returns the framebuffer used for texture downloads
    void BindFramebuffer(GLenum target, GLint level, GLenum textarget, VideoCore::SurfaceType type,
                         GLuint handle) const;

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
    std::array<ReinterpreterList, VideoCore::PIXEL_FORMAT_COUNT> reinterpreters;
    std::unordered_multimap<HostTextureTag, Allocation> texture_recycler;
    std::unordered_map<u64, OGLFramebuffer, Common::IdentityHash<u64>> framebuffer_cache;
    StreamBuffer upload_buffer;
    std::vector<u8> download_buffer;
    OGLFramebuffer read_fbo, draw_fbo;
};

class Surface : public VideoCore::SurfaceBase {
public:
    explicit Surface(VideoCore::SurfaceParams& params, TextureRuntime& runtime);
    ~Surface();

    /// Returns the surface image handle
    GLuint Handle() const noexcept {
        return alloc.texture.handle;
    }

    /// Uploads pixel data in staging to a rectangle region of the surface texture
    void Upload(const VideoCore::BufferTextureCopy& upload, const VideoCore::StagingData& staging);

    /// Downloads pixel data to staging from a rectangle region of the surface texture
    void Download(const VideoCore::BufferTextureCopy& download,
                  const VideoCore::StagingData& staging);

    /// Swaps the internal allocation to match the provided dimentions and format
    bool Swap(u32 width, u32 height, VideoCore::CustomPixelFormat format);

    /// Returns the bpp of the internal surface format
    u32 GetInternalBytesPerPixel() const {
        return VideoCore::GetBytesPerPixel(pixel_format);
    }

private:
    /// Uploads pixel data to scaled texture
    void ScaledUpload(const VideoCore::BufferTextureCopy& upload,
                      const VideoCore::StagingData& staging);

    /// Downloads scaled image by downscaling the requested rectangle
    void ScaledDownload(const VideoCore::BufferTextureCopy& download,
                        const VideoCore::StagingData& staging);

private:
    TextureRuntime& runtime;
    const Driver& driver;
    Allocation alloc;
};

class Framebuffer : public VideoCore::FramebufferBase {
public:
    explicit Framebuffer(TextureRuntime& runtime, Surface* const color,
                         Surface* const depth_stencil, const Pica::Regs& regs,
                         Common::Rectangle<u32> surfaces_rect);
    ~Framebuffer();

    [[nodiscard]] GLuint Handle() const noexcept {
        return handle;
    }

    [[nodiscard]] GLuint Attachment(VideoCore::SurfaceType type) const noexcept {
        return attachments[Index(type)];
    }

    bool HasAttachment(VideoCore::SurfaceType type) const noexcept {
        return static_cast<bool>(attachments[Index(type)]);
    }

private:
    std::array<GLuint, 2> attachments{};
    GLuint handle{};
};

class Sampler {
public:
    explicit Sampler(TextureRuntime& runtime, VideoCore::SamplerParams params);
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    Sampler(Sampler&&) = default;
    Sampler& operator=(Sampler&&) = default;

    [[nodiscard]] GLuint Handle() const noexcept {
        return sampler.handle;
    }

private:
    OGLSampler sampler;
};

struct Traits {
    using Runtime = OpenGL::TextureRuntime;
    using Surface = OpenGL::Surface;
    using Sampler = OpenGL::Sampler;
    using Framebuffer = OpenGL::Framebuffer;
};

using RasterizerCache = VideoCore::RasterizerCache<Traits>;

} // namespace OpenGL
