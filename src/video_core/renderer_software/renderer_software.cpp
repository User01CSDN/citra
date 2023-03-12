// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/core.h"
#include "core/frontend/emu_window.h"
#include "core/tracer/recorder.h"
#include "video_core/debug_utils/debug_utils.h"
#include "video_core/renderer_software/renderer_software.h"

namespace VideoCore {

RendererSoftware::RendererSoftware(Frontend::EmuWindow& window)
    : RendererBase{window, nullptr}, rasterizer{std::make_unique<RasterizerSoftware>()} {}

RendererSoftware::~RendererSoftware() = default;

void RendererSoftware::SwapBuffers() {
    m_current_frame++;

    Core::System::GetInstance().perf_stats->EndSystemFrame();

    render_window.PollEvents();

    Core::System::GetInstance().frame_limiter.DoFrameLimiting(
        Core::System::GetInstance().CoreTiming().GetGlobalTimeUs());
    Core::System::GetInstance().perf_stats->BeginSystemFrame();

    if (Pica::g_debug_context && Pica::g_debug_context->recorder) {
        Pica::g_debug_context->recorder->FrameFinished();
    }
}

} // namespace VideoCore
