// Copyright 2020 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string_view>
#include "common/common_types.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"

namespace VideoCore {
struct TextureBlit;
}

namespace OpenGL {

class TextureFilterBase {
    friend class TextureFilterer;

public:
    explicit TextureFilterBase(u32 scale_factor) : scale_factor(scale_factor) {
        draw_fbo.Create();
    };

    virtual ~TextureFilterBase() = default;

private:
    virtual void Filter(GLuint src_tex, GLuint dst_tex, const VideoCore::TextureBlit& blit) = 0;

protected:
    OGLFramebuffer draw_fbo;
    const u32 scale_factor{};
};

} // namespace OpenGL
