// Copyright 2022 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/vector_math.h"
#include "video_core/renderer_vulkan/vk_blit_helper.h"
#include "video_core/renderer_vulkan/vk_descriptor_manager.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_renderpass_cache.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/renderer_vulkan/vk_texture_runtime.h"

#include "video_core/host_shaders/full_screen_triangle_vert_spv.h"
#include "video_core/host_shaders/vulkan_blit_depth_stencil_frag_spv.h"
#include "video_core/host_shaders/vulkan_d32s8_to_r32_comp_spv.h"

namespace Vulkan {

namespace {
struct PushConstants {
    std::array<float, 2> tex_scale;
    std::array<float, 2> tex_offset;
};

template <u32 binding, vk::DescriptorType type, vk::ShaderStageFlagBits stage>
inline constexpr vk::DescriptorSetLayoutBinding TEXTURE_DESC_LAYOUT{
    .binding = binding,
    .descriptorType = type,
    .descriptorCount = 1,
    .stageFlags = stage,
};
template <u32 binding, vk::DescriptorType type>
inline constexpr vk::DescriptorUpdateTemplateEntry TEXTURE_TEMPLATE{
    .dstBinding = binding,
    .dstArrayElement = 0,
    .descriptorCount = 1,
    .descriptorType = type,
    .offset = sizeof(vk::DescriptorImageInfo),
    .stride = 0,
};

constexpr std::array COMPUTE_DESCRIPTOR_SET_BINDINGS = {
    TEXTURE_DESC_LAYOUT<0, vk::DescriptorType::eSampledImage, vk::ShaderStageFlagBits::eCompute>,
    TEXTURE_DESC_LAYOUT<1, vk::DescriptorType::eSampledImage, vk::ShaderStageFlagBits::eCompute>,
    TEXTURE_DESC_LAYOUT<2, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eCompute>,
};
constexpr vk::DescriptorSetLayoutCreateInfo COMPUTE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO{
    .bindingCount = static_cast<u32>(COMPUTE_DESCRIPTOR_SET_BINDINGS.size()),
    .pBindings = COMPUTE_DESCRIPTOR_SET_BINDINGS.data(),
};
const std::array COMPUTE_UPDATE_TEMPLATES = {
    TEXTURE_TEMPLATE<0, vk::DescriptorType::eSampledImage>,
    TEXTURE_TEMPLATE<1, vk::DescriptorType::eSampledImage>,
    TEXTURE_TEMPLATE<2, vk::DescriptorType::eStorageImage>,
};
inline constexpr vk::PushConstantRange COMPUTE_PUSH_CONSTANT_RANGE{
    .stageFlags = vk::ShaderStageFlagBits::eCompute,
    .offset = 0,
    .size = sizeof(Common::Vec2i),
};

constexpr std::array TWO_TEXTURES_DESCRIPTOR_SET_LAYOUT_BINDINGS{
    TEXTURE_DESC_LAYOUT<0, vk::DescriptorType::eCombinedImageSampler,
                        vk::ShaderStageFlagBits::eFragment>,
    TEXTURE_DESC_LAYOUT<1, vk::DescriptorType::eCombinedImageSampler,
                        vk::ShaderStageFlagBits::eFragment>,
};
constexpr vk::DescriptorSetLayoutCreateInfo TWO_TEXTURES_DESCRIPTOR_SET_LAYOUT_CREATE_INFO{
    .bindingCount = static_cast<u32>(TWO_TEXTURES_DESCRIPTOR_SET_LAYOUT_BINDINGS.size()),
    .pBindings = TWO_TEXTURES_DESCRIPTOR_SET_LAYOUT_BINDINGS.data(),
};
const std::array TWO_TEXTURES_UPDATE_TEMPLATES = {
    TEXTURE_TEMPLATE<0, vk::DescriptorType::eCombinedImageSampler>,
    TEXTURE_TEMPLATE<1, vk::DescriptorType::eCombinedImageSampler>,
};

inline constexpr vk::PushConstantRange PUSH_CONSTANT_RANGE{
    .stageFlags = vk::ShaderStageFlagBits::eVertex,
    .offset = 0,
    .size = sizeof(PushConstants),
};
constexpr vk::PipelineVertexInputStateCreateInfo PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO{
    .vertexBindingDescriptionCount = 0,
    .pVertexBindingDescriptions = nullptr,
    .vertexAttributeDescriptionCount = 0,
    .pVertexAttributeDescriptions = nullptr,
};
constexpr vk::PipelineInputAssemblyStateCreateInfo PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO{
    .topology = vk::PrimitiveTopology::eTriangleList,
    .primitiveRestartEnable = VK_FALSE,
};
constexpr vk::PipelineViewportStateCreateInfo PIPELINE_VIEWPORT_STATE_CREATE_INFO{
    .viewportCount = 1,
    .pViewports = nullptr,
    .scissorCount = 1,
    .pScissors = nullptr,
};
constexpr vk::PipelineRasterizationStateCreateInfo PIPELINE_RASTERIZATION_STATE_CREATE_INFO{
    .depthClampEnable = VK_FALSE,
    .rasterizerDiscardEnable = VK_FALSE,
    .polygonMode = vk::PolygonMode::eFill,
    .cullMode = vk::CullModeFlagBits::eBack,
    .frontFace = vk::FrontFace::eClockwise,
    .depthBiasEnable = VK_FALSE,
    .depthBiasConstantFactor = 0.0f,
    .depthBiasClamp = 0.0f,
    .depthBiasSlopeFactor = 0.0f,
    .lineWidth = 1.0f,
};
constexpr vk::PipelineMultisampleStateCreateInfo PIPELINE_MULTISAMPLE_STATE_CREATE_INFO{
    .rasterizationSamples = vk::SampleCountFlagBits::e1,
    .sampleShadingEnable = VK_FALSE,
    .minSampleShading = 0.0f,
    .pSampleMask = nullptr,
    .alphaToCoverageEnable = VK_FALSE,
    .alphaToOneEnable = VK_FALSE,
};
constexpr std::array DYNAMIC_STATES{
    vk::DynamicState::eViewport,
    vk::DynamicState::eScissor,
};
constexpr vk::PipelineDynamicStateCreateInfo PIPELINE_DYNAMIC_STATE_CREATE_INFO{
    .dynamicStateCount = static_cast<u32>(DYNAMIC_STATES.size()),
    .pDynamicStates = DYNAMIC_STATES.data(),
};
constexpr vk::PipelineColorBlendStateCreateInfo PIPELINE_COLOR_BLEND_STATE_EMPTY_CREATE_INFO{
    .logicOpEnable = VK_FALSE,
    .logicOp = vk::LogicOp::eClear,
    .attachmentCount = 0,
    .pAttachments = nullptr,
    .blendConstants = std::array{0.0f, 0.0f, 0.0f, 0.0f},
};
constexpr vk::PipelineColorBlendAttachmentState PIPELINE_COLOR_BLEND_ATTACHMENT_STATE{
    .blendEnable = VK_FALSE,
    .srcColorBlendFactor = vk::BlendFactor::eZero,
    .dstColorBlendFactor = vk::BlendFactor::eZero,
    .colorBlendOp = vk::BlendOp::eAdd,
    .srcAlphaBlendFactor = vk::BlendFactor::eZero,
    .dstAlphaBlendFactor = vk::BlendFactor::eZero,
    .alphaBlendOp = vk::BlendOp::eAdd,
    .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                      vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
};
constexpr vk::PipelineColorBlendStateCreateInfo PIPELINE_COLOR_BLEND_STATE_GENERIC_CREATE_INFO{
    .logicOpEnable = VK_FALSE,
    .logicOp = vk::LogicOp::eClear,
    .attachmentCount = 1,
    .pAttachments = &PIPELINE_COLOR_BLEND_ATTACHMENT_STATE,
    .blendConstants = std::array{0.0f, 0.0f, 0.0f, 0.0f},
};
constexpr vk::PipelineDepthStencilStateCreateInfo PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO{
    .depthTestEnable = VK_TRUE,
    .depthWriteEnable = VK_TRUE,
    .depthCompareOp = vk::CompareOp::eAlways,
    .depthBoundsTestEnable = VK_FALSE,
    .stencilTestEnable = VK_FALSE,
    .front = vk::StencilOpState{},
    .back = vk::StencilOpState{},
    .minDepthBounds = 0.0f,
    .maxDepthBounds = 0.0f,
};

template <vk::Filter filter>
inline constexpr vk::SamplerCreateInfo SAMPLER_CREATE_INFO{
    .magFilter = filter,
    .minFilter = filter,
    .mipmapMode = vk::SamplerMipmapMode::eNearest,
    .addressModeU = vk::SamplerAddressMode::eClampToBorder,
    .addressModeV = vk::SamplerAddressMode::eClampToBorder,
    .addressModeW = vk::SamplerAddressMode::eClampToBorder,
    .mipLodBias = 0.0f,
    .anisotropyEnable = VK_FALSE,
    .maxAnisotropy = 0.0f,
    .compareEnable = VK_FALSE,
    .compareOp = vk::CompareOp::eNever,
    .minLod = 0.0f,
    .maxLod = 0.0f,
    .borderColor = vk::BorderColor::eFloatOpaqueWhite,
    .unnormalizedCoordinates = VK_TRUE,
};

constexpr vk::PipelineLayoutCreateInfo PipelineLayoutCreateInfo(
    const vk::DescriptorSetLayout* set_layout, bool compute = false) {
    return vk::PipelineLayoutCreateInfo{
        .setLayoutCount = 1,
        .pSetLayouts = set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = (compute ? &COMPUTE_PUSH_CONSTANT_RANGE : &PUSH_CONSTANT_RANGE),
    };
}

constexpr vk::DescriptorUpdateTemplateCreateInfo DescriptorUpdateTemplateCreateInfo(
    std::span<const vk::DescriptorUpdateTemplateEntry> entries, vk::DescriptorSetLayout layout) {
    return vk::DescriptorUpdateTemplateCreateInfo{
        .descriptorUpdateEntryCount = static_cast<u32>(entries.size()),
        .pDescriptorUpdateEntries = entries.data(),
        .templateType = vk::DescriptorUpdateTemplateType::eDescriptorSet,
        .descriptorSetLayout = layout,
    };
}

constexpr std::array<vk::PipelineShaderStageCreateInfo, 2> MakeStages(
    vk::ShaderModule vertex_shader, vk::ShaderModule fragment_shader) {
    return std::array{
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = vertex_shader,
            .pName = "main",
        },
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = fragment_shader,
            .pName = "main",
        },
    };
}

constexpr vk::PipelineShaderStageCreateInfo MakeStages(vk::ShaderModule compute_shader) {
    return vk::PipelineShaderStageCreateInfo{
        .stage = vk::ShaderStageFlagBits::eCompute,
        .module = compute_shader,
        .pName = "main",
    };
}

} // Anonymous namespace

BlitHelper::BlitHelper(const Instance& instance_, Scheduler& scheduler_,
                       DescriptorManager& desc_manager_, RenderpassCache& renderpass_cache_)
    : instance{instance_}, scheduler{scheduler_}, desc_manager{desc_manager_},
      renderpass_cache{renderpass_cache_}, device{instance.GetDevice()},
      compute_descriptor_layout{
          device.createDescriptorSetLayout(COMPUTE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO)},
      two_textures_descriptor_layout{
          device.createDescriptorSetLayout(TWO_TEXTURES_DESCRIPTOR_SET_LAYOUT_CREATE_INFO)},
      compute_update_template{device.createDescriptorUpdateTemplate(
          DescriptorUpdateTemplateCreateInfo(COMPUTE_UPDATE_TEMPLATES, compute_descriptor_layout))},
      two_textures_update_template{
          device.createDescriptorUpdateTemplate(DescriptorUpdateTemplateCreateInfo(
              TWO_TEXTURES_UPDATE_TEMPLATES, two_textures_descriptor_layout))},
      compute_pipeline_layout{
          device.createPipelineLayout(PipelineLayoutCreateInfo(&compute_descriptor_layout, true))},
      two_textures_pipeline_layout{
          device.createPipelineLayout(PipelineLayoutCreateInfo(&two_textures_descriptor_layout))},
      full_screen_vert{CompileSPV(FULL_SCREEN_TRIANGLE_VERT_SPV, device)},
      copy_d24s8_to_r32_comp{CompileSPV(VULKAN_D32S8_TO_R32_COMP_SPV, device)},
      blit_depth_stencil_frag{CompileSPV(VULKAN_BLIT_DEPTH_STENCIL_FRAG_SPV, device)},
      depth_blit_pipeline{MakeDepthStencilBlitPipeline()},
      linear_sampler{device.createSampler(SAMPLER_CREATE_INFO<vk::Filter::eLinear>)},
      nearest_sampler{device.createSampler(SAMPLER_CREATE_INFO<vk::Filter::eNearest>)} {
    MakeComputePipelines();
}

BlitHelper::~BlitHelper() {
    device.destroyPipelineLayout(compute_pipeline_layout);
    device.destroyPipelineLayout(two_textures_pipeline_layout);
    device.destroyDescriptorUpdateTemplate(compute_update_template);
    device.destroyDescriptorUpdateTemplate(two_textures_update_template);
    device.destroyDescriptorSetLayout(compute_descriptor_layout);
    device.destroyDescriptorSetLayout(two_textures_descriptor_layout);
    device.destroyShaderModule(full_screen_vert);
    device.destroyShaderModule(copy_d24s8_to_r32_comp);
    device.destroyShaderModule(blit_depth_stencil_frag);
    device.destroyPipeline(copy_d24s8_to_r32_pipeline);
    device.destroyPipeline(depth_blit_pipeline);
    device.destroySampler(linear_sampler);
    device.destroySampler(nearest_sampler);
}

void BindBlitState(vk::CommandBuffer cmdbuf, vk::PipelineLayout layout,
                   const VideoCore::TextureBlit& blit) {
    const vk::Offset2D offset{
        .x = std::min<s32>(blit.dst_rect.left, blit.dst_rect.right),
        .y = std::min<s32>(blit.dst_rect.bottom, blit.dst_rect.top),
    };
    const vk::Extent2D extent{
        .width = blit.dst_rect.GetWidth(),
        .height = blit.dst_rect.GetHeight(),
    };
    const vk::Viewport viewport{
        .x = static_cast<float>(offset.x),
        .y = static_cast<float>(offset.y),
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    const vk::Rect2D scissor{
        .offset = offset,
        .extent = extent,
    };
    const float scale_x = static_cast<float>(blit.src_rect.GetWidth());
    const float scale_y = static_cast<float>(blit.src_rect.GetHeight());
    const PushConstants push_constants{
        .tex_scale = {scale_x, scale_y},
        .tex_offset = {static_cast<float>(blit.src_rect.left),
                       static_cast<float>(blit.src_rect.bottom)},
    };
    cmdbuf.setViewport(0, viewport);
    cmdbuf.setScissor(0, scissor);
    cmdbuf.pushConstants(layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(push_constants),
                         &push_constants);
}

bool BlitHelper::BlitDepthStencil(Surface& source, Surface& dest,
                                  const VideoCore::TextureBlit& blit) {
    if (!instance.IsShaderStencilExportSupported()) {
        LOG_ERROR(Render_Vulkan, "Unable to emulate depth stencil images");
        return false;
    }

    const vk::Rect2D dst_render_area = {
        .offset = {0, 0},
        .extent = {dest.GetScaledWidth(), dest.GetScaledHeight()},
    };

    const std::array textures = {
        vk::DescriptorImageInfo{
            .sampler = nearest_sampler,
            .imageView = source.DepthView(),
            .imageLayout = vk::ImageLayout::eGeneral,
        },
        vk::DescriptorImageInfo{
            .sampler = nearest_sampler,
            .imageView = source.StencilView(),
            .imageLayout = vk::ImageLayout::eGeneral,
        },
    };

    vk::DescriptorSet set = desc_manager.AllocateSet(two_textures_descriptor_layout);
    device.updateDescriptorSetWithTemplate(set, two_textures_update_template, textures[0]);

    renderpass_cache.BeginRendering(nullptr, &dest, dst_render_area);
    scheduler.Record([blit, set, this](vk::CommandBuffer cmdbuf) {
        const vk::PipelineLayout layout = two_textures_pipeline_layout;

        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, depth_blit_pipeline);
        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, set, {});
        BindBlitState(cmdbuf, layout, blit);
        cmdbuf.draw(3, 1, 0, 0);
    });
    scheduler.MakeDirty(StateFlags::Pipeline);
    return true;
}

void BlitHelper::BlitD24S8ToR32(Surface& source, Surface& dest,
                                const VideoCore::TextureBlit& blit) {
    const std::array textures = {
        vk::DescriptorImageInfo{
            .imageView = source.DepthView(),
            .imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal,
        },
        vk::DescriptorImageInfo{
            .imageView = source.StencilView(),
            .imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal,
        },
        vk::DescriptorImageInfo{
            .imageView = dest.ImageView(),
            .imageLayout = vk::ImageLayout::eGeneral,
        },
    };

    vk::DescriptorSet set = desc_manager.AllocateSet(compute_descriptor_layout);
    device.updateDescriptorSetWithTemplate(set, compute_update_template, textures[0]);

    renderpass_cache.EndRendering();
    scheduler.Record([this, set, blit, src_image = source.Image(),
                      dst_image = dest.Image()](vk::CommandBuffer cmdbuf) {
        const std::array pre_barriers = {
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                .oldLayout = vk::ImageLayout::eGeneral,
                .newLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = src_image,
                .subresourceRange{
                    .aspectMask =
                        vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
                    .baseMipLevel = 0,
                    .levelCount = VK_REMAINING_MIP_LEVELS,
                    .baseArrayLayer = 0,
                    .layerCount = VK_REMAINING_ARRAY_LAYERS,
                },
            },
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eNone,
                .dstAccessMask = vk::AccessFlagBits::eShaderWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = dst_image,
                .subresourceRange{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = VK_REMAINING_MIP_LEVELS,
                    .baseArrayLayer = 0,
                    .layerCount = VK_REMAINING_ARRAY_LAYERS,
                },
            },
        };
        const std::array post_barriers = {
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eShaderRead,
                .dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite |
                                 vk::AccessFlagBits::eDepthStencilAttachmentRead,
                .oldLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal,
                .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = src_image,
                .subresourceRange{
                    .aspectMask =
                        vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
                    .baseMipLevel = 0,
                    .levelCount = VK_REMAINING_MIP_LEVELS,
                    .baseArrayLayer = 0,
                    .layerCount = VK_REMAINING_ARRAY_LAYERS,
                },
            },
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
                .dstAccessMask = vk::AccessFlagBits::eTransferRead,
                .oldLayout = vk::ImageLayout::eGeneral,
                .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = dst_image,
                .subresourceRange{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = VK_REMAINING_MIP_LEVELS,
                    .baseArrayLayer = 0,
                    .layerCount = VK_REMAINING_ARRAY_LAYERS,
                },
            }};
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eEarlyFragmentTests |
                                   vk::PipelineStageFlagBits::eLateFragmentTests,
                               vk::PipelineStageFlagBits::eComputeShader,
                               vk::DependencyFlagBits::eByRegion, {}, {}, pre_barriers);

        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, compute_pipeline_layout, 0, set,
                                  {});
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, copy_d24s8_to_r32_pipeline);

        const auto src_offset = Common::MakeVec(blit.src_rect.left, blit.src_rect.bottom);
        cmdbuf.pushConstants(compute_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0,
                             sizeof(Common::Vec2i), src_offset.AsArray());

        cmdbuf.dispatch(blit.src_rect.GetWidth() / 8, blit.src_rect.GetHeight() / 8, 1);

        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                               vk::PipelineStageFlagBits::eEarlyFragmentTests |
                                   vk::PipelineStageFlagBits::eLateFragmentTests |
                                   vk::PipelineStageFlagBits::eTransfer,
                               vk::DependencyFlagBits::eByRegion, {}, {}, post_barriers);
    });
}

void BlitHelper::MakeComputePipelines() {
    const vk::ComputePipelineCreateInfo compute_info = {
        .stage = MakeStages(copy_d24s8_to_r32_comp),
        .layout = compute_pipeline_layout,
    };

    if (const auto result = device.createComputePipeline({}, compute_info);
        result.result == vk::Result::eSuccess) {
        copy_d24s8_to_r32_pipeline = result.value;
    } else {
        LOG_CRITICAL(Render_Vulkan, "D24S8->R32 compute pipeline creation failed!");
        UNREACHABLE();
    }
}

vk::Pipeline BlitHelper::MakeDepthStencilBlitPipeline() {
    if (!instance.IsShaderStencilExportSupported()) {
        return VK_NULL_HANDLE;
    }

    const std::array stages = MakeStages(full_screen_vert, blit_depth_stencil_frag);
    const VideoCore::PixelFormat depth_stencil = VideoCore::PixelFormat::D24S8;
    const vk::Format depth_stencil_format = instance.GetTraits(depth_stencil).native;
    vk::GraphicsPipelineCreateInfo depth_stencil_info = {
        .stageCount = static_cast<u32>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pInputAssemblyState = &PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pTessellationState = nullptr,
        .pViewportState = &PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pRasterizationState = &PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pMultisampleState = &PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pDepthStencilState = &PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pColorBlendState = &PIPELINE_COLOR_BLEND_STATE_GENERIC_CREATE_INFO,
        .pDynamicState = &PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .layout = two_textures_pipeline_layout,
    };

    if (!instance.IsDynamicRenderingSupported()) {
        depth_stencil_info.renderPass =
            renderpass_cache.GetRenderpass(VideoCore::PixelFormat::Invalid, depth_stencil, false);
    }

    vk::StructureChain depth_blit_chain = {
        depth_stencil_info,
        vk::PipelineRenderingCreateInfoKHR{
            .colorAttachmentCount = 0,
            .pColorAttachmentFormats = nullptr,
            .depthAttachmentFormat = depth_stencil_format,
            .stencilAttachmentFormat = depth_stencil_format,
        },
    };

    if (!instance.IsDynamicRenderingSupported()) {
        depth_blit_chain.unlink<vk::PipelineRenderingCreateInfoKHR>();
    }

    if (const auto result = device.createGraphicsPipeline({}, depth_blit_chain.get());
        result.result == vk::Result::eSuccess) {
        return result.value;
    } else {
        LOG_CRITICAL(Render_Vulkan, "Depth stencil blit pipeline creation failed!");
        UNREACHABLE();
    }
    return VK_NULL_HANDLE;
}

} // namespace Vulkan
