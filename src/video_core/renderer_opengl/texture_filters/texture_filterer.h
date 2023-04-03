// Copyright 2020 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <vector>
#include "video_core/renderer_opengl/texture_filters/texture_filter_base.h"

namespace VideoCore {
enum class SurfaceType : u32;
}

namespace OpenGL {

class TextureFilterer {
public:
    static constexpr std::string_view NONE = "Linear (Default)";

public:
    explicit TextureFilterer(std::string_view filter_name, u32 scale_factor);

    /// Returns true if the filter actually changed
    bool Reset(std::string_view new_filter_name, u32 new_scale_factor);

    /// Returns true if there is no active filter
    bool IsNull() const;

    /// Returns true if the texture was able to be filtered
    bool Filter(GLuint src_tex, GLuint dst_tex, const VideoCore::TextureBlit& blit,
                VideoCore::SurfaceType type) const;

    static std::vector<std::string_view> GetFilterNames();

private:
    std::string_view filter_name = NONE;
    std::unique_ptr<TextureFilterBase> filter;
};

} // namespace OpenGL
