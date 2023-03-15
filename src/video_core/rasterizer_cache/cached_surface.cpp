// Copyright 2022 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/scope_exit.h"
#include "video_core/rasterizer_cache/cached_surface.h"
#include "video_core/rasterizer_cache/rasterizer_cache.h"
#include "video_core/renderer_opengl/gl_state.h"

namespace OpenGL {

CachedSurface::~CachedSurface() {
    if (texture.handle) {
        auto tag =
            HostTextureTag{GetFormatTuple(pixel_format), GetScaledWidth(), GetScaledHeight()};

        owner.host_texture_recycler.emplace(tag, std::move(texture));
    }
}

void CachedSurface::Upload(const BufferTextureCopy& upload, const StagingData& staging) {
    // Ensure no bad interactions with GL_UNPACK_ALIGNMENT
    ASSERT(stride * GetBytesPerPixel(pixel_format) % 4 == 0);

    const bool is_scaled = res_scale != 1;
    if (is_scaled) {
        LOG_ERROR(Render_OpenGL, "Scaled uploads not supported!");
        return;
    } else {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(stride));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture.handle);

        const auto& tuple = GetFormatTuple(pixel_format);
        glTexSubImage2D(GL_TEXTURE_2D, upload.texture_level, upload.texture_rect.left,
                        upload.texture_rect.bottom, upload.texture_rect.GetWidth(),
                        upload.texture_rect.GetHeight(), tuple.format, tuple.type,
                        staging.mapped.data());

        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glBindTexture(GL_TEXTURE_2D, OpenGLState::GetCurState().texture_units[0].texture_2d);
    }

    InvalidateAllWatcher();
}

void CachedSurface::Download(const BufferTextureCopy& download, const StagingData& staging) {
    // Ensure no bad interactions with GL_PACK_ALIGNMENT
    ASSERT(stride * GetBytesPerPixel(pixel_format) % 4 == 0);

    OpenGLState prev_state = OpenGLState::GetCurState();
    SCOPE_EXIT({ prev_state.Apply(); });

    glPixelStorei(GL_PACK_ROW_LENGTH, static_cast<GLint>(stride));

    const bool is_scaled = res_scale != 1;
    if (is_scaled) {
        LOG_ERROR(Render_OpenGL, "Scaled downloads not supported!");
        return;
    } else {
        runtime.ReadTexture(texture, download.texture_rect, pixel_format, download.texture_level,
                            staging.mapped);
    }

    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
}

bool CachedSurface::CanFill(const SurfaceParams& dest_surface,
                            SurfaceInterval fill_interval) const {
    if (type == SurfaceType::Fill && IsRegionValid(fill_interval) &&
        boost::icl::first(fill_interval) >= addr &&
        boost::icl::last_next(fill_interval) <= end && // dest_surface is within our fill range
        dest_surface.FromInterval(fill_interval).GetInterval() ==
            fill_interval) { // make sure interval is a rectangle in dest surface
        if (fill_size * 8 != dest_surface.GetFormatBpp()) {
            // Check if bits repeat for our fill_size
            const u32 dest_bytes_per_pixel = std::max(dest_surface.GetFormatBpp() / 8, 1u);
            std::vector<u8> fill_test(fill_size * dest_bytes_per_pixel);

            for (u32 i = 0; i < dest_bytes_per_pixel; ++i)
                std::memcpy(&fill_test[i * fill_size], &fill_data[0], fill_size);

            for (u32 i = 0; i < fill_size; ++i)
                if (std::memcmp(&fill_test[dest_bytes_per_pixel * i], &fill_test[0],
                                dest_bytes_per_pixel) != 0)
                    return false;

            if (dest_surface.GetFormatBpp() == 4 && (fill_test[0] & 0xF) != (fill_test[0] >> 4))
                return false;
        }
        return true;
    }
    return false;
}

bool CachedSurface::CanCopy(const SurfaceParams& dest_surface,
                            SurfaceInterval copy_interval) const {
    SurfaceParams subrect_params = dest_surface.FromInterval(copy_interval);
    ASSERT(subrect_params.GetInterval() == copy_interval);
    if (CanSubRect(subrect_params))
        return true;

    if (CanFill(dest_surface, copy_interval))
        return true;

    return false;
}

} // namespace OpenGL
