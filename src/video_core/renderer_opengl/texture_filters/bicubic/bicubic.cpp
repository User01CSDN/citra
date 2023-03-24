// Copyright 2020 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "video_core/rasterizer_cache/utils.h"
#include "video_core/renderer_opengl/texture_filters/bicubic/bicubic.h"

#include "shaders/bicubic.frag"
#include "shaders/tex_coord.vert"

namespace OpenGL {

Bicubic::Bicubic(u16 scale_factor) : TextureFilterBase(scale_factor) {
    program.Create(tex_coord_vert.data(), bicubic_frag.data());
    vao.Create();
    src_sampler.Create();

    state.draw.shader_program = program.handle;
    state.draw.vertex_array = vao.handle;
    state.draw.shader_program = program.handle;
    state.texture_units[0].sampler = src_sampler.handle;

    glSamplerParameteri(src_sampler.handle, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(src_sampler.handle, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(src_sampler.handle, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(src_sampler.handle, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Bicubic::Filter(GLuint src_tex, GLuint dst_tex, const VideoCore::TextureBlit& blit) {
    const OpenGLState cur_state = OpenGLState::GetCurState();
    state.texture_units[0].texture_2d = src_tex;
    state.draw.draw_framebuffer = draw_fbo.handle;
    state.viewport.x = blit.dst_rect.left;
    state.viewport.y = blit.dst_rect.bottom;
    state.viewport.width = blit.dst_rect.GetWidth();
    state.viewport.height = blit.dst_rect.GetHeight();
    state.Apply();

    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst_tex,
                           blit.dst_level);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    cur_state.Apply();
}

} // namespace OpenGL
