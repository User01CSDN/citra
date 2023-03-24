// Copyright 2022 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_state.h"
#include "video_core/renderer_opengl/texture_filters/texture_filter_base.h"

namespace OpenGL {

class NearestNeighbor : public TextureFilterBase {
public:
    static constexpr std::string_view NAME = "Nearest Neighbor";

    explicit NearestNeighbor(u16 scale_factor);
    void Filter(GLuint src_tex, GLuint dst_tex, const VideoCore::TextureBlit& blit) override;

private:
    OpenGLState state{};
    OGLProgram program{};
    OGLVertexArray vao{};
    OGLSampler src_sampler{};
};
} // namespace OpenGL
