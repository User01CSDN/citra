// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/frontend/emu_window.h"
#include "video_core/renderer_base.h"

namespace VideoCore {

RendererBase::RendererBase(Frontend::EmuWindow& window, Frontend::EmuWindow* secondary_window_)
    : render_window{window}, secondary_window{secondary_window_} {}

RendererBase::~RendererBase() = default;

void RendererBase::UpdateCurrentFramebufferLayout(bool is_portrait_mode) {
    const auto update_layout = [is_portrait_mode](Frontend::EmuWindow& window) {
        const Layout::FramebufferLayout& layout = window.GetFramebufferLayout();
        window.UpdateCurrentFramebufferLayout(layout.width, layout.height, is_portrait_mode);
    };
    update_layout(render_window);
    if (secondary_window) {
        update_layout(*secondary_window);
    }
}

bool RendererBase::IsScreenshotPending() const {
    return renderer_settings.screenshot_requested;
}

void RendererBase::RequestScreenshot(void* data, std::function<void()> callback,
                                     const Layout::FramebufferLayout& layout) {
    if (renderer_settings.screenshot_requested) {
        LOG_ERROR(Render, "A screenshot is already requested or in progress, ignoring the request");
        return;
    }
    renderer_settings.screenshot_bits = data;
    renderer_settings.screenshot_complete_callback = callback;
    renderer_settings.screenshot_framebuffer_layout = layout;
    renderer_settings.screenshot_requested = true;
}

} // namespace VideoCore
