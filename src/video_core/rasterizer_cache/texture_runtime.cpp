// Copyright 2022 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/scope_exit.h"
#include "common/settings.h"
#include "video_core/rasterizer_cache/cached_surface.h"
#include "video_core/rasterizer_cache/rasterizer_cache_utils.h"
#include "video_core/rasterizer_cache/texture_runtime.h"
#include "video_core/renderer_opengl/gl_state.h"

namespace OpenGL {

namespace {

constexpr FormatTuple DEFAULT_TUPLE = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};

static constexpr std::array<FormatTuple, 4> DEPTH_TUPLES = {{
    {GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT}, // D16
    {},
    {GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT},   // D24
    {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8}, // D24S8
}};

static constexpr std::array<FormatTuple, 5> COLOR_TUPLES = {{
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8},     // RGBA8
    {GL_RGB8, GL_BGR, GL_UNSIGNED_BYTE},              // RGB8
    {GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1}, // RGB5A1
    {GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5},     // RGB565
    {GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4},   // RGBA4
}};

static constexpr std::array<FormatTuple, 5> COLOR_TUPLES_OES = {{
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE},            // RGBA8
    {GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE},              // RGB8
    {GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1}, // RGB5A1
    {GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5},     // RGB565
    {GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4},   // RGBA4
}};

[[nodiscard]] GLbitfield MakeBufferMask(SurfaceType type) {
    switch (type) {
    case SurfaceType::Color:
    case SurfaceType::Texture:
    case SurfaceType::Fill:
        return GL_COLOR_BUFFER_BIT;
    case SurfaceType::Depth:
        return GL_DEPTH_BUFFER_BIT;
    case SurfaceType::DepthStencil:
        return GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    default:
        UNREACHABLE_MSG("Invalid surface type!");
    }
    return GL_COLOR_BUFFER_BIT;
}

} // Anonymous namespace

TextureRuntime::TextureRuntime() {
    read_fbo.Create();
    draw_fbo.Create();
}

TextureRuntime::~TextureRuntime() = default;

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

const FormatTuple& TextureRuntime::GetFormatTuple(PixelFormat pixel_format) {
    const auto type = GetFormatType(pixel_format);
    const std::size_t format_index = static_cast<std::size_t>(pixel_format);
    const bool gles = Settings::values.use_gles.GetValue();

    if (type == SurfaceType::Color) {
        ASSERT(format_index < COLOR_TUPLES.size());
        return (gles ? COLOR_TUPLES_OES : COLOR_TUPLES)[format_index];
    } else if (type == SurfaceType::Depth || type == SurfaceType::DepthStencil) {
        const std::size_t tuple_idx = format_index - 14;
        ASSERT(tuple_idx < DEPTH_TUPLES.size());
        return DEPTH_TUPLES[tuple_idx];
    }

    return DEFAULT_TUPLE;
}

void TextureRuntime::Recycle(const HostTextureTag tag, Allocation&& alloc) {
    texture_recycler.emplace(tag, std::move(alloc));
}

Allocation TextureRuntime::Allocate(u32 width, u32 height, u32 levels, const FormatTuple& tuple,
                                    TextureType type) {
    const GLenum target = type == TextureType::CubeMap ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
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

void TextureRuntime::ReadTexture(OGLTexture& texture, Common::Rectangle<u32> rect,
                                 PixelFormat format, GLint level, std::span<u8> pixels) const {
    OpenGLState prev_state = OpenGLState::GetCurState();
    SCOPE_EXIT({ prev_state.Apply(); });

    OpenGLState state;
    state.ResetTexture(texture.handle);
    state.draw.read_framebuffer = read_fbo.handle;
    state.Apply();

    switch (GetFormatType(format)) {
    case SurfaceType::Color:
    case SurfaceType::Texture:
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               texture.handle, level);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0,
                               0);
        break;
    case SurfaceType::Depth:
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                               texture.handle, level);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        break;
    case SurfaceType::DepthStencil:
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                               texture.handle, level);
        break;
    default:
        UNREACHABLE_MSG("Invalid surface type!");
    }

    const auto& tuple = GetFormatTuple(format);
    glReadPixels(rect.left, rect.bottom, rect.GetWidth(), rect.GetHeight(), tuple.format,
                 tuple.type, pixels.data());
}

bool TextureRuntime::ClearTexture(CachedSurface& surface, const TextureClear& clear) {
    OpenGLState prev_state = OpenGLState::GetCurState();
    SCOPE_EXIT({ prev_state.Apply(); });

    // Setup scissor rectangle according to the clear rectangle
    OpenGLState state;
    state.scissor.enabled = true;
    state.scissor.x = clear.texture_rect.left;
    state.scissor.y = clear.texture_rect.bottom;
    state.scissor.width = clear.texture_rect.GetWidth();
    state.scissor.height = clear.texture_rect.GetHeight();
    state.draw.draw_framebuffer = draw_fbo.handle;
    state.Apply();

    switch (surface.type) {
    case SurfaceType::Color:
    case SurfaceType::Texture:
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
    case SurfaceType::Depth:
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                               surface.Handle(), clear.texture_level);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);

        state.depth.write_mask = GL_TRUE;
        state.Apply();

        glClearBufferfv(GL_DEPTH, 0, &clear.value.depth);
        break;
    case SurfaceType::DepthStencil:
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                               surface.Handle(), clear.texture_level);

        state.depth.write_mask = GL_TRUE;
        state.stencil.write_mask = -1;
        state.Apply();

        glClearBufferfi(GL_DEPTH_STENCIL, 0, clear.value.depth, clear.value.stencil);
        break;
    default:
        UNREACHABLE_MSG("Unknown surface type {}", surface.type);
        return false;
    }

    return true;
}

bool TextureRuntime::CopyTextures(CachedSurface& source, CachedSurface& dest,
                                  const TextureCopy& copy) {
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

bool TextureRuntime::CopyTextures(CachedSurface& source, CachedTextureCube& dest,
                                  const TextureCopy& copy) {
    glCopyImageSubData(source.Handle(), GL_TEXTURE_2D, copy.src_level, copy.src_offset.x,
                       copy.src_offset.y, copy.src_layer, dest.texture.handle, GL_TEXTURE_CUBE_MAP,
                       copy.dst_level, copy.dst_offset.x, copy.dst_offset.y, copy.dst_layer,
                       copy.extent.width, copy.extent.height, 1);
    return true;
}

bool TextureRuntime::BlitTextures(CachedSurface& source, CachedSurface& dest,
                                  const TextureBlit& blit) {
    OpenGLState prev_state = OpenGLState::GetCurState();
    SCOPE_EXIT({ prev_state.Apply(); });

    OpenGLState state{};
    state.draw.read_framebuffer = read_fbo.handle;
    state.draw.draw_framebuffer = draw_fbo.handle;
    state.Apply();

    const auto BindAttachment = [&](GLenum target, u32 src_tex, u32 dst_tex) -> void {
        const GLenum src_textarget = source.texture_type == TextureType::CubeMap
                                         ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + blit.src_layer
                                         : GL_TEXTURE_2D;
        const GLenum dst_textarget = dest.texture_type == TextureType::CubeMap
                                         ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + blit.dst_layer
                                         : GL_TEXTURE_2D;
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, target, src_textarget, src_tex, blit.src_level);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, target, dst_textarget, dst_tex, blit.dst_level);
    };

    switch (source.type) {
    case SurfaceType::Color:
    case SurfaceType::Texture:
        BindAttachment(GL_COLOR_ATTACHMENT0, source.Handle(), dest.Handle());
        BindAttachment(GL_DEPTH_STENCIL_ATTACHMENT, 0, 0);
        break;
    case SurfaceType::Depth:
        BindAttachment(GL_COLOR_ATTACHMENT0, 0, 0);
        BindAttachment(GL_DEPTH_ATTACHMENT, source.Handle(), dest.Handle());
        BindAttachment(GL_STENCIL_ATTACHMENT, 0, 0);
        break;
    case SurfaceType::DepthStencil:
        BindAttachment(GL_COLOR_ATTACHMENT0, 0, 0);
        BindAttachment(GL_DEPTH_STENCIL_ATTACHMENT, source.Handle(), dest.Handle());
        break;
    default:
        UNREACHABLE_MSG("Unknown surface type {}", source.type);
        return false;
    }

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

void TextureRuntime::GenerateMipmaps(CachedSurface& surface, u32 max_level) {
    OpenGLState prev_state = OpenGLState::GetCurState();
    SCOPE_EXIT({ prev_state.Apply(); });

    OpenGLState state;
    state.texture_units[0].texture_2d = surface.Handle();
    state.Apply();

    glActiveTexture(GL_TEXTURE0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, max_level);

    glGenerateMipmap(GL_TEXTURE_2D);
}

} // namespace OpenGL
