// Copyright 2022 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/scope_exit.h"
#include "common/settings.h"
#include "video_core/rasterizer_cache/utils.h"
#include "video_core/regs.h"
#include "video_core/renderer_opengl/gl_driver.h"
#include "video_core/renderer_opengl/gl_format_reinterpreter.h"
#include "video_core/renderer_opengl/gl_state.h"
#include "video_core/renderer_opengl/gl_texture_runtime.h"
#include "video_core/renderer_opengl/pica_to_gl.h"
#include "video_core/video_core.h"

namespace OpenGL {

using VideoCore::StagingData;
using VideoCore::TextureType;

constexpr FormatTuple DEFAULT_TUPLE = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};

static constexpr std::array DEPTH_TUPLES = {
    FormatTuple{GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT},              // D16
    FormatTuple{}, FormatTuple{GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT}, // D24
    FormatTuple{GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8},              // D24S8
};

static constexpr std::array COLOR_TUPLES = {
    FormatTuple{GL_RGBA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8},     // RGBA8
    FormatTuple{GL_RGB8, GL_BGR, GL_UNSIGNED_BYTE},              // RGB8
    FormatTuple{GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1}, // RGB5A1
    FormatTuple{GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5},     // RGB565
    FormatTuple{GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4},   // RGBA4
};

static constexpr std::array COLOR_TUPLES_OES = {
    FormatTuple{GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE},            // RGBA8
    FormatTuple{GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE},            // RGB8
    FormatTuple{GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1}, // RGB5A1
    FormatTuple{GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5},     // RGB565
    FormatTuple{GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4},   // RGBA4
};

static constexpr std::array CUSTOM_TUPLES = {
    DEFAULT_TUPLE,
    FormatTuple{GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
                GL_UNSIGNED_BYTE},
    FormatTuple{GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
                GL_UNSIGNED_BYTE},
    FormatTuple{GL_COMPRESSED_RG_RGTC2, GL_COMPRESSED_RG_RGTC2, GL_UNSIGNED_BYTE},
    FormatTuple{GL_COMPRESSED_RGBA_BPTC_UNORM_ARB, GL_COMPRESSED_RGBA_BPTC_UNORM_ARB,
                GL_UNSIGNED_BYTE},
    FormatTuple{GL_COMPRESSED_RGBA_ASTC_4x4, GL_COMPRESSED_RGBA_ASTC_4x4, GL_UNSIGNED_BYTE},
    FormatTuple{GL_COMPRESSED_RGBA_ASTC_6x6, GL_COMPRESSED_RGBA_ASTC_6x6, GL_UNSIGNED_BYTE},
    FormatTuple{GL_COMPRESSED_RGBA_ASTC_8x6, GL_COMPRESSED_RGBA_ASTC_8x6, GL_UNSIGNED_BYTE},
};

[[nodiscard]] GLbitfield MakeBufferMask(VideoCore::SurfaceType type) {
    switch (type) {
    case VideoCore::SurfaceType::Color:
    case VideoCore::SurfaceType::Texture:
    case VideoCore::SurfaceType::Fill:
        return GL_COLOR_BUFFER_BIT;
    case VideoCore::SurfaceType::Depth:
        return GL_DEPTH_BUFFER_BIT;
    case VideoCore::SurfaceType::DepthStencil:
        return GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    default:
        UNREACHABLE_MSG("Invalid surface type!");
    }

    return GL_COLOR_BUFFER_BIT;
}

TextureRuntime::TextureRuntime(Driver& driver)
    : driver{driver}, filterer{Settings::values.texture_filter_name.GetValue(),
                               VideoCore::GetResolutionScaleFactor()} {

    read_fbo.Create();
    draw_fbo.Create();

    auto Register = [this](VideoCore::PixelFormat dest,
                           std::unique_ptr<FormatReinterpreterBase>&& obj) {
        const u32 dst_index = static_cast<u32>(dest);
        return reinterpreters[dst_index].push_back(std::move(obj));
    };

    Register(VideoCore::PixelFormat::RGBA8, std::make_unique<D24S8toRGBA8>(!driver.IsOpenGLES()));
    Register(VideoCore::PixelFormat::RGB5A1, std::make_unique<RGBA4toRGB5A1>());
}

TextureRuntime::~TextureRuntime() = default;

void TextureRuntime::Clear() {
    framebuffer_cache.clear();
    texture_recycler.clear();
}

StagingData TextureRuntime::FindStaging(u32 size, bool upload) {
    if (size > staging_buffer.size()) {
        staging_buffer.resize(size);
    }
    return StagingData{
        .size = size,
        .mapped = std::span{staging_buffer.data(), size},
        .buffer_offset = 0,
    };
}

const FormatTuple& TextureRuntime::GetFormatTuple(VideoCore::PixelFormat pixel_format) {
    const auto type = GetFormatType(pixel_format);
    const std::size_t format_index = static_cast<std::size_t>(pixel_format);

    if (type == VideoCore::SurfaceType::Color) {
        ASSERT(format_index < COLOR_TUPLES.size());
        return (driver.IsOpenGLES() ? COLOR_TUPLES_OES : COLOR_TUPLES)[format_index];
    } else if (type == VideoCore::SurfaceType::Depth ||
               type == VideoCore::SurfaceType::DepthStencil) {
        const std::size_t tuple_idx = format_index - 14;
        ASSERT(tuple_idx < DEPTH_TUPLES.size());
        return DEPTH_TUPLES[tuple_idx];
    }

    return DEFAULT_TUPLE;
}

const FormatTuple& TextureRuntime::GetFormatTuple(VideoCore::CustomPixelFormat pixel_format) {
    const std::size_t format_index = static_cast<std::size_t>(pixel_format);
    return CUSTOM_TUPLES[format_index];
}

void TextureRuntime::Recycle(const HostTextureTag tag, Allocation&& alloc) {
    texture_recycler.emplace(tag, std::move(alloc));
}

Allocation TextureRuntime::Allocate(u32 width, u32 height, u32 levels, const FormatTuple& tuple,
                                    VideoCore::TextureType type) {
    const GLenum target =
        type == VideoCore::TextureType::CubeMap ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;

    const HostTextureTag key = {
        .tuple = tuple,
        .type = type,
        .width = width,
        .height = height,
        .levels = levels,
    };

    if (auto it = texture_recycler.find(key); it != texture_recycler.end()) {
        Allocation alloc = std::move(it->second);
        texture_recycler.erase(it);
        return alloc;
    }

    // Allocate new texture
    OGLTexture texture{};
    texture.Create();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(target, texture.handle);

    glTexStorage2D(target, levels, tuple.internal_format, width, height);

    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(target, OpenGLState::GetCurState().texture_units[0].texture_2d);

    return Allocation{
        .texture = std::move(texture),
        .tuple = tuple,
        .width = width,
        .height = height,
        .levels = levels,
    };
}

bool TextureRuntime::ClearTexture(Surface& surface, const VideoCore::TextureClear& clear) {
    OpenGLState prev_state = OpenGLState::GetCurState();
    SCOPE_EXIT({ prev_state.Apply(); });

    // Setup scissor rectangle according to the clear rectangle
    OpenGLState state{};
    state.scissor.enabled = true;
    state.scissor.x = clear.texture_rect.left;
    state.scissor.y = clear.texture_rect.bottom;
    state.scissor.width = clear.texture_rect.GetWidth();
    state.scissor.height = clear.texture_rect.GetHeight();
    state.draw.draw_framebuffer = draw_fbo.handle;
    state.Apply();

    switch (surface.type) {
    case VideoCore::SurfaceType::Color:
    case VideoCore::SurfaceType::Texture:
    case VideoCore::SurfaceType::Fill:
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               surface.Handle(), clear.texture_level);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0,
                               0);

        state.color_mask.red_enabled = true;
        state.color_mask.green_enabled = true;
        state.color_mask.blue_enabled = true;
        state.color_mask.alpha_enabled = true;
        state.Apply();

        glClearBufferfv(GL_COLOR, 0, clear.value.color.AsArray());
        break;
    case VideoCore::SurfaceType::Depth:
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                               surface.Handle(), clear.texture_level);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);

        state.depth.write_mask = GL_TRUE;
        state.Apply();

        glClearBufferfv(GL_DEPTH, 0, &clear.value.depth);
        break;
    case VideoCore::SurfaceType::DepthStencil:
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                               surface.Handle(), clear.texture_level);

        state.depth.write_mask = GL_TRUE;
        state.stencil.write_mask = -1;
        state.Apply();

        glClearBufferfi(GL_DEPTH_STENCIL, 0, clear.value.depth, clear.value.stencil);
        break;
    default:
        UNREACHABLE_MSG("Invalid surface type!");
    }

    return true;
}

bool TextureRuntime::CopyTextures(Surface& source, Surface& dest,
                                  const VideoCore::TextureCopy& copy) {
    const GLenum src_textarget =
        source.texture_type == TextureType::CubeMap ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    const GLenum dst_textarget =
        dest.texture_type == TextureType::CubeMap ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    glCopyImageSubData(source.Handle(), src_textarget, copy.src_level, copy.src_offset.x,
                       copy.src_offset.y, copy.src_layer, dest.Handle(), dst_textarget,
                       copy.dst_level, copy.dst_offset.x, copy.dst_offset.y, copy.dst_layer,
                       copy.extent.width, copy.extent.height, 1);
    return true;
}

bool TextureRuntime::BlitTextures(Surface& source, Surface& dest,
                                  const VideoCore::TextureBlit& blit) {
    OpenGLState prev_state = OpenGLState::GetCurState();
    SCOPE_EXIT({ prev_state.Apply(); });

    OpenGLState state{};
    state.draw.read_framebuffer = read_fbo.handle;
    state.draw.draw_framebuffer = draw_fbo.handle;
    state.Apply();

    const GLenum src_textarget = source.texture_type == TextureType::CubeMap
                                     ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + blit.src_layer
                                     : GL_TEXTURE_2D;
    BindFramebuffer(GL_READ_FRAMEBUFFER, blit.src_level, src_textarget, source.type,
                    source.Handle());

    const GLenum dst_textarget = dest.texture_type == TextureType::CubeMap
                                     ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + blit.dst_layer
                                     : GL_TEXTURE_2D;
    BindFramebuffer(GL_DRAW_FRAMEBUFFER, blit.dst_level, dst_textarget, dest.type, dest.Handle());

    // TODO (wwylele): use GL_NEAREST for shadow map texture
    // Note: shadow map is treated as RGBA8 format in PICA, as well as in the rasterizer cache, but
    // doing linear intepolation componentwise would cause incorrect value. However, for a
    // well-programmed game this code path should be rarely executed for shadow map with
    // inconsistent scale.
    const GLbitfield buffer_mask = MakeBufferMask(source.type);
    const GLenum filter = buffer_mask == GL_COLOR_BUFFER_BIT ? GL_LINEAR : GL_NEAREST;
    glBlitFramebuffer(blit.src_rect.left, blit.src_rect.bottom, blit.src_rect.right,
                      blit.src_rect.top, blit.dst_rect.left, blit.dst_rect.bottom,
                      blit.dst_rect.right, blit.dst_rect.top, buffer_mask, filter);

    return true;
}

void TextureRuntime::GenerateMipmaps(Surface& surface) {
    OpenGLState prev_state = OpenGLState::GetCurState();
    SCOPE_EXIT({ prev_state.Apply(); });

    OpenGLState state{};
    state.texture_units[0].texture_2d = surface.Handle();
    state.Apply();

    glActiveTexture(GL_TEXTURE0);
    glGenerateMipmap(GL_TEXTURE_2D);
}

const ReinterpreterList& TextureRuntime::GetPossibleReinterpretations(
    VideoCore::PixelFormat dest_format) const {
    return reinterpreters[static_cast<u32>(dest_format)];
}

bool TextureRuntime::NeedsConvertion(VideoCore::PixelFormat format) const {
    return driver.IsOpenGLES() &&
           (format == VideoCore::PixelFormat::RGB8 || format == VideoCore::PixelFormat::RGBA8);
}

void TextureRuntime::BindFramebuffer(GLenum target, GLint level, GLenum textarget,
                                     VideoCore::SurfaceType type, GLuint handle) const {
    const GLint framebuffer = target == GL_DRAW_FRAMEBUFFER ? draw_fbo.handle : read_fbo.handle;
    glBindFramebuffer(target, framebuffer);

    switch (type) {
    case VideoCore::SurfaceType::Color:
    case VideoCore::SurfaceType::Texture:
        glFramebufferTexture2D(target, GL_COLOR_ATTACHMENT0, textarget, handle, level);
        glFramebufferTexture2D(target, GL_DEPTH_STENCIL_ATTACHMENT, textarget, 0, 0);
        break;
    case VideoCore::SurfaceType::Depth:
        glFramebufferTexture2D(target, GL_COLOR_ATTACHMENT0, textarget, 0, 0);
        glFramebufferTexture2D(target, GL_DEPTH_ATTACHMENT, textarget, handle, level);
        glFramebufferTexture2D(target, GL_STENCIL_ATTACHMENT, textarget, 0, 0);
        break;
    case VideoCore::SurfaceType::DepthStencil:
        glFramebufferTexture2D(target, GL_COLOR_ATTACHMENT0, textarget, 0, 0);
        glFramebufferTexture2D(target, GL_DEPTH_STENCIL_ATTACHMENT, textarget, handle, level);
        break;
    default:
        UNREACHABLE_MSG("Invalid surface type!");
    }
}

Surface::Surface(TextureRuntime& runtime_, const VideoCore::SurfaceParams& params)
    : VideoCore::SurfaceBase{params}, runtime{&runtime_}, driver{&runtime_.GetDriver()} {
    if (pixel_format == VideoCore::PixelFormat::Invalid) {
        return;
    }

    const u32 scaled_width = GetScaledWidth();
    const u32 scaled_height = GetScaledHeight();
    const auto& tuple = runtime->GetFormatTuple(pixel_format);
    alloc = runtime->Allocate(scaled_width, scaled_height, levels, tuple, texture_type);

    const std::string name =
        fmt::format("Surface: {}x{} {} {} levels from {:#x} to {:#x}", scaled_width, scaled_height,
                    VideoCore::PixelFormatAsString(pixel_format), levels, addr, end);
    glObjectLabel(GL_TEXTURE, alloc.texture.handle, -1, name.c_str());
}

Surface::~Surface() {
    if (pixel_format == VideoCore::PixelFormat::Invalid || !Handle()) {
        return;
    }

    const HostTextureTag tag = {
        .tuple = alloc.tuple,
        .type = texture_type,
        .width = alloc.width,
        .height = alloc.height,
        .levels = alloc.levels,
    };
    runtime->Recycle(tag, std::move(alloc));
}

void Surface::Upload(const VideoCore::BufferTextureCopy& upload, const StagingData& staging) {
    // Ensure no bad interactions with GL_UNPACK_ALIGNMENT
    ASSERT(stride * GetBytesPerPixel(pixel_format) % 4 == 0);

    const bool is_scaled = res_scale != 1;
    if (is_scaled) {
        ScaledUpload(upload, staging);
    } else {
        const VideoCore::Rect2D rect = upload.texture_rect;
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(rect.GetWidth()));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, Handle());

        // Wait for the buffer if a decode is pending, this isn't very optimal
        // but this kind of threading is very hard in gl
        staging.Wait();

        const auto& tuple = alloc.tuple;
        if (is_custom && custom_format != VideoCore::CustomPixelFormat::RGBA8) {
            glCompressedTexSubImage2D(GL_TEXTURE_2D, upload.texture_level, rect.left, rect.bottom,
                                      rect.GetWidth(), rect.GetHeight(), tuple.format, staging.size,
                                      staging.mapped.data());
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, upload.texture_level, rect.left, rect.bottom,
                            rect.GetWidth(), rect.GetHeight(), tuple.format, tuple.type,
                            staging.mapped.data());
        }

        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

        // Restore old texture
        glBindTexture(GL_TEXTURE_2D, OpenGLState::GetCurState().texture_units[0].texture_2d);
    }
}

void Surface::Download(const VideoCore::BufferTextureCopy& download, const StagingData& staging) {
    // Ensure no bad interactions with GL_PACK_ALIGNMENT
    ASSERT(stride * GetBytesPerPixel(pixel_format) % 4 == 0);

    const bool is_scaled = res_scale != 1;
    if (is_scaled) {
        ScaledDownload(download, staging);
    } else {
        const VideoCore::Rect2D rect = download.texture_rect;
        glPixelStorei(GL_PACK_ROW_LENGTH, static_cast<GLint>(rect.GetWidth()));

        runtime->BindFramebuffer(GL_READ_FRAMEBUFFER, download.texture_level, GL_TEXTURE_2D, type,
                                 Handle());

        const auto& tuple = runtime->GetFormatTuple(pixel_format);
        glReadPixels(rect.left, rect.bottom, rect.GetWidth(), rect.GetHeight(), tuple.format,
                     tuple.type, staging.mapped.data());

        glPixelStorei(GL_PACK_ROW_LENGTH, 0);

        // Restore previous framebuffer
        glBindFramebuffer(GL_READ_FRAMEBUFFER, OpenGLState::GetCurState().draw.read_framebuffer);
    }
}

bool Surface::Swap(u32 width, u32 height, VideoCore::CustomPixelFormat format) {
    if (!driver->IsCustomFormatSupported(format)) {
        return false;
    }

    const auto& tuple = runtime->GetFormatTuple(format);
    if (alloc.Matches(width, height, levels, tuple)) {
        return true;
    }

    const HostTextureTag tag = {
        .tuple = alloc.tuple,
        .type = texture_type,
        .width = alloc.width,
        .height = alloc.height,
        .levels = alloc.levels,
    };
    runtime->Recycle(tag, std::move(alloc));

    is_custom = true;
    custom_format = format;
    alloc = runtime->Allocate(width, height, levels, tuple, texture_type);

    LOG_DEBUG(Render_OpenGL, "Swapped {}x{} {} surface at address {:#x} to {}x{} {}",
              GetScaledWidth(), GetScaledHeight(), VideoCore::PixelFormatAsString(pixel_format),
              addr, width, height, VideoCore::CustomPixelFormatAsString(format));

    return true;
}

void Surface::ScaledUpload(const VideoCore::BufferTextureCopy& upload, const StagingData& staging) {
    const u32 rect_width = upload.texture_rect.GetWidth();
    const u32 rect_height = upload.texture_rect.GetHeight();
    const auto scaled_rect = upload.texture_rect * res_scale;
    const auto unscaled_rect = VideoCore::Rect2D{0, rect_height, rect_width, 0};

    SurfaceParams unscaled_params = *this;
    unscaled_params.width = rect_width;
    unscaled_params.stride = rect_width;
    unscaled_params.height = rect_height;
    unscaled_params.res_scale = 1;
    Surface unscaled_surface{*runtime, unscaled_params};

    const VideoCore::BufferTextureCopy unscaled_upload = {
        .buffer_offset = upload.buffer_offset,
        .buffer_size = upload.buffer_size,
        .texture_rect = unscaled_rect,
    };
    unscaled_surface.Upload(unscaled_upload, staging);

    const auto& filterer = runtime->GetFilterer();
    if (!filterer.Filter(unscaled_surface.alloc.texture, unscaled_rect, alloc.texture, scaled_rect,
                         type)) {
        const VideoCore::TextureBlit blit = {
            .src_level = 0,
            .dst_level = upload.texture_level,
            .src_rect = unscaled_rect,
            .dst_rect = scaled_rect,
        };
        runtime->BlitTextures(unscaled_surface, *this, blit);
    }
}

void Surface::ScaledDownload(const VideoCore::BufferTextureCopy& download,
                             const StagingData& staging) {
    const u32 rect_width = download.texture_rect.GetWidth();
    const u32 rect_height = download.texture_rect.GetHeight();
    const VideoCore::Rect2D scaled_rect = download.texture_rect * res_scale;
    const VideoCore::Rect2D unscaled_rect = VideoCore::Rect2D{0, rect_height, rect_width, 0};

    // Allocate an unscaled texture that fits the download rectangle to use as a blit destination
    SurfaceParams unscaled_params = *this;
    unscaled_params.width = rect_width;
    unscaled_params.stride = rect_width;
    unscaled_params.height = rect_height;
    unscaled_params.res_scale = 1;
    Surface unscaled_surface{*runtime, unscaled_params};

    // Blit the scaled rectangle to the unscaled texture
    const VideoCore::TextureBlit blit = {
        .src_level = download.texture_level,
        .dst_level = download.texture_level,
        .src_layer = 0,
        .dst_layer = 0,
        .src_rect = scaled_rect,
        .dst_rect = unscaled_rect,
    };
    runtime->BlitTextures(*this, unscaled_surface, blit);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, unscaled_surface.Handle());

    const auto& tuple = runtime->GetFormatTuple(pixel_format);
    if (driver->IsOpenGLES()) {
        runtime->BindFramebuffer(GL_READ_FRAMEBUFFER, download.texture_level, GL_TEXTURE_2D, type,
                                 unscaled_surface.Handle());
        glReadPixels(0, 0, rect_width, rect_height, tuple.format, tuple.type,
                     staging.mapped.data());
    } else {
        glGetTexImage(GL_TEXTURE_2D, download.texture_level, tuple.format, tuple.type,
                      staging.mapped.data());
    }
}

Framebuffer::Framebuffer(TextureRuntime& runtime, Surface* const color,
                         Surface* const depth_stencil, const Pica::Regs& regs,
                         Common::Rectangle<u32> surfaces_rect)
    : VideoCore::FramebufferBase{regs, color, depth_stencil, surfaces_rect} {

    const bool shadow_rendering = regs.framebuffer.IsShadowRendering();
    const bool has_stencil = regs.framebuffer.HasStencil();
    if (shadow_rendering && !color) {
        return; // Framebuffer won't get used
    }

    if (color) {
        attachments[0] = color->Handle();
    }
    if (depth_stencil) {
        attachments[1] = depth_stencil->Handle();
    }

    const u64 hash = Common::ComputeStructHash64(attachments);
    auto [it, new_framebuffer] = runtime.framebuffer_cache.try_emplace(hash);
    if (!new_framebuffer) {
        handle = it->second.handle;
        return;
    }

    // Create a new framebuffer otherwise
    OGLFramebuffer& framebuffer = it->second;
    framebuffer.Create();

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer.handle);

    if (shadow_rendering) {
        glFramebufferParameteri(GL_DRAW_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_WIDTH,
                                color->width * res_scale);
        glFramebufferParameteri(GL_DRAW_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_HEIGHT,
                                color->height * res_scale);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0,
                               0);
    } else {
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               color ? color->Handle() : 0, 0);
        if (depth_stencil) {
            if (has_stencil) {
                // Attach both depth and stencil
                glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                       GL_TEXTURE_2D, depth_stencil->Handle(), 0);
            } else {
                // Attach depth
                glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                       depth_stencil->Handle(), 0);
                // Clear stencil attachment
                glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0,
                                       0);
            }
        } else {
            // Clear both depth and stencil attachment
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                                   0, 0);
        }
    }

    // Restore previous framebuffer
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, OpenGLState::GetCurState().draw.draw_framebuffer);
}

Framebuffer::~Framebuffer() = default;

Sampler::Sampler(TextureRuntime& runtime, VideoCore::SamplerParams params) {
    const GLenum mag_filter = PicaToGL::TextureMagFilterMode(params.min_filter);
    const GLenum min_filter = PicaToGL::TextureMinFilterMode(params.min_filter, params.mip_filter);
    const GLenum wrap_s = PicaToGL::WrapMode(params.wrap_s);
    const GLenum wrap_t = PicaToGL::WrapMode(params.wrap_t);
    const Common::Vec4f gl_color = PicaToGL::ColorRGBA8(params.border_color);
    const float lod_min = params.lod_min;
    const float lod_max = params.lod_max;

    sampler.Create();

    const GLuint handle = sampler.handle;
    glSamplerParameteri(handle, GL_TEXTURE_MAG_FILTER, mag_filter);
    glSamplerParameteri(handle, GL_TEXTURE_MIN_FILTER, min_filter);

    glSamplerParameteri(handle, GL_TEXTURE_WRAP_S, wrap_s);
    glSamplerParameteri(handle, GL_TEXTURE_WRAP_T, wrap_t);

    glSamplerParameterfv(handle, GL_TEXTURE_BORDER_COLOR, gl_color.AsArray());

    glSamplerParameterf(handle, GL_TEXTURE_MIN_LOD, lod_min);
    glSamplerParameterf(handle, GL_TEXTURE_MAX_LOD, lod_max);
}

Sampler::~Sampler() = default;

} // namespace OpenGL
