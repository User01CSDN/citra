// Copyright 2022 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <condition_variable>
#include <mutex>
#include <glm/glm.hpp>
#include "common/common_types.h"
#include "common/math_util.h"
#include "core/hw/gpu.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_vulkan/vk_descriptor_manager.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_renderpass_cache.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_swapchain.h"

namespace Core {
class System;
class TelemetrySession;
} // namespace Core

namespace Memory {
class MemorySystem;
}

namespace Layout {
struct FramebufferLayout;
}

namespace Vulkan {

/**
 * Structure used for storing information about the textures for each 3DS screen
 **/
struct TextureInfo {
    u32 width;
    u32 height;
    GPU::Regs::PixelFormat format;
    vk::Image image;
    vk::ImageView image_view;
    VmaAllocation allocation;
};

/**
 * Structure used for storing information about the display target for each 3DS screen
 **/
struct ScreenInfo {
    TextureInfo texture;
    Common::Rectangle<f32> texcoords;
    vk::ImageView image_view;
};

struct Frame;
class PresentMailbox;

class RendererVulkan : public VideoCore::RendererBase {
    static constexpr std::size_t PRESENT_PIPELINES = 3;

public:
    explicit RendererVulkan(Core::System& system, Frontend::EmuWindow& window,
                            Frontend::EmuWindow* secondary_window);
    ~RendererVulkan() override;

    [[nodiscard]] VideoCore::RasterizerInterface* Rasterizer() override {
        return &rasterizer;
    }

    void SwapBuffers() override;
    void NotifySurfaceChanged() override;
    void TryPresent(int timeout_ms, bool is_secondary) override {}
    void PrepareVideoDumping() override {}
    void CleanupVideoDumping() override {}
    void Sync() override;

private:
    void Report() const;
    void ReloadSampler();
    void ReloadPipeline();
    void CompileShaders();
    void BuildLayouts();
    void BuildPipelines();
    void ConfigureFramebufferTexture(TextureInfo& texture,
                                     const GPU::Regs::FramebufferConfig& framebuffer);
    void ConfigureRenderPipeline();
    void PrepareRendertarget();
    void RenderScreenshot();
    void RenderToMailbox(const Layout::FramebufferLayout& layout,
                         std::unique_ptr<PresentMailbox>& mailbox, bool flipped);
    void BeginRendering(Frame* frame);

    /**
     * Draws the emulated screens to the render frame.
     */
    void DrawScreens(Frame* frame, const Layout::FramebufferLayout& layout, bool flipped);

    /**
     * Draws a single texture to the emulator window.
     */
    void DrawSingleScreen(u32 screen_id, float x, float y, float w, float h);
    void DrawSingleScreenStereo(u32 screen_id_l, u32 screen_id_r, float x, float y, float w,
                                float h);

    /**
     * Draws a single texture to the emulator window, rotating the texture to correct for the 3DS's
     * LCD rotation.
     */
    void DrawSingleScreenRotated(u32 screen_id, float x, float y, float w, float h);
    void DrawSingleScreenStereoRotated(u32 screen_id_l, u32 screen_id_r, float x, float y, float w,
                                       float h);

    /**
     * Loads framebuffer from emulated memory into the active OpenGL texture.
     */
    void LoadFBToScreenInfo(const GPU::Regs::FramebufferConfig& framebuffer,
                            ScreenInfo& screen_info, bool right_eye);

    /**
     * Fills active Vulkan texture with the given RGB color. Since the color is solid, the texture
     * can be 1x1 but will stretch across whatever it's rendered on.
     */
    void LoadColorToActiveVkTexture(u8 color_r, u8 color_g, u8 color_b, const TextureInfo& texture);

private:
    Core::System& system;
    Memory::MemorySystem& memory;
    Core::TelemetrySession& telemetry_session;

    Instance instance;
    Scheduler scheduler;
    RenderpassCache renderpass_cache;
    DescriptorManager desc_manager;
    TextureRuntime runtime;
    Swapchain swapchain;
    StreamBuffer vertex_buffer;
    RasterizerVulkan rasterizer;
    std::unique_ptr<PresentMailbox> mailbox;

    /// Present pipelines (Normal, Anaglyph, Interlaced)
    vk::PipelineLayout present_pipeline_layout;
    vk::DescriptorSetLayout present_descriptor_layout;
    vk::DescriptorUpdateTemplate present_update_template;
    std::array<vk::Pipeline, PRESENT_PIPELINES> present_pipelines;
    std::array<vk::DescriptorSet, PRESENT_PIPELINES> present_descriptor_sets;
    std::array<vk::ShaderModule, PRESENT_PIPELINES> present_shaders;
    std::array<vk::Sampler, 2> present_samplers;
    vk::ShaderModule present_vertex_shader;
    u32 current_pipeline = 0;
    u32 current_sampler = 0;

    struct PresentUniformData {
        glm::mat4 modelview;
        Common::Vec4f i_resolution;
        Common::Vec4f o_resolution;
        int screen_id_l = 0;
        int screen_id_r = 0;
        int layer = 0;
        int reverse_interlaced = 0;
    };
    static_assert(sizeof(PresentUniformData) < 256, "PresentUniformData must be below 256 bytes!");

    /// Display information for top and bottom screens respectively
    std::array<ScreenInfo, 3> screen_infos{};
    std::array<vk::DescriptorImageInfo, 4> present_textures{};
    PresentUniformData draw_info{};
    vk::ClearColorValue clear_color{};
};

} // namespace Vulkan
