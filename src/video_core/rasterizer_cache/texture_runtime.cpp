// Copyright 2022 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/scope_exit.h"
#include "video_core/rasterizer_cache/cached_surface.h"
#include "video_core/rasterizer_cache/rasterizer_cache_utils.h"
#include "video_core/rasterizer_cache/texture_runtime.h"
#include "video_core/renderer_opengl/gl_state.h"

namespace OpenGL {

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

TextureRuntime::TextureRuntime() {
    read_fbo.Create();
    draw_fbo.Create();
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

    const auto tuple = GetFormatTuple(format);
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

    const GLuint handle = surface.texture.handle;
    switch (surface.type) {
    case SurfaceType::Color:
    case SurfaceType::Texture:
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, handle,
                               clear.texture_level);
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
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, handle,
                               clear.texture_level);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);

        state.depth.write_mask = GL_TRUE;
        state.Apply();

        glClearBufferfv(GL_DEPTH, 0, &clear.value.depth);
        break;
    case SurfaceType::DepthStencil:
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                               handle, clear.texture_level);

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
    glCopyImageSubData(source.texture.handle, src_textarget, copy.src_level, copy.src_offset.x,
                       copy.src_offset.y, copy.src_layer, dest.texture.handle, dst_textarget,
                       copy.dst_level, copy.dst_offset.x, copy.dst_offset.y, copy.dst_layer,
                       copy.extent.width, copy.extent.height, 1);
    return true;
}

bool TextureRuntime::CopyTextures(CachedSurface& source, CachedTextureCube& dest, const TextureCopy& copy) {
    glCopyImageSubData(source.texture.handle, GL_TEXTURE_2D, copy.src_level, copy.src_offset.x,
                       copy.src_offset.y, copy.src_layer, dest.texture.handle, GL_TEXTURE_CUBE_MAP,
                       copy.dst_level, copy.dst_offset.x, copy.dst_offset.y, copy.dst_layer,
                       copy.extent.width, copy.extent.height, 1);
    return true;
}

bool TextureRuntime::BlitTextures(CachedSurface& source, CachedSurface& dest, const TextureBlit& blit) {
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
        BindAttachment(GL_COLOR_ATTACHMENT0, source.texture.handle, dest.texture.handle);
        BindAttachment(GL_DEPTH_STENCIL_ATTACHMENT, 0, 0);
        break;
    case SurfaceType::Depth:
        BindAttachment(GL_COLOR_ATTACHMENT0, 0, 0);
        BindAttachment(GL_DEPTH_ATTACHMENT, source.texture.handle, dest.texture.handle);
        BindAttachment(GL_STENCIL_ATTACHMENT, 0, 0);
        break;
    case SurfaceType::DepthStencil:
        BindAttachment(GL_COLOR_ATTACHMENT0, 0, 0);
        BindAttachment(GL_DEPTH_STENCIL_ATTACHMENT, source.texture.handle, dest.texture.handle);
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
    state.texture_units[0].texture_2d = surface.texture.handle;
    state.Apply();

    glActiveTexture(GL_TEXTURE0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, max_level);

    glGenerateMipmap(GL_TEXTURE_2D);
}

} // namespace OpenGL
