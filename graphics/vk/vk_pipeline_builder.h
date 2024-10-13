//
// Created by Ammar on 13/10/2024.
//

#ifndef VK_PIPELINE_BUILDER_H
#define VK_PIPELINE_BUILDER_H

#include "vk/vk_common.h"

struct VkGraphicsPipelineBuilder {
    VkPipeline Build(bool dynamicRendering, const VkDevice &device, const VkPipelineCache &pipelineCache, const VkRenderPass *renderPass = nullptr);
    void Clear();

    void CreateShaderModules(const VkDevice &device, const std::string &vertexShaderFilePath, const std::string &fragmentShaderFilePath);
    void DestroyShaderModules(const VkDevice &device);
    void SetPipelineLayout(VkPipelineLayout pipelineLayout);
    void SetTopology(VkPrimitiveTopology topology);
    void SetPolygonMode(VkPolygonMode polygonMode);
    void SetCullingMode(VkCullModeFlags cullMode, VkFrontFace frontFace);
    void EnableDepthTest(bool depthWrite, VkCompareOp compareOp);
    void EnableBlendingAdditive();
    void EnableBlendingAlphaBlend();

    // Dynamic rendering stuff
    void SetColorAttachmentFormat(VkFormat format);
    void SetDepthFormat(VkFormat format);

    VkShaderModule vertexShaderModule{VK_NULL_HANDLE};
    VkShaderModule fragmentShaderModule{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    VkPipelineRasterizationStateCreateInfo rasterizerCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    VkPipelineDepthStencilStateCreateInfo depthStencilCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};

    VkPipelineRenderingCreateInfo renderingCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};

    VkFormat colorAttachmentFormat{VK_FORMAT_UNDEFINED};
};

#endif //VK_PIPELINE_BUILDER_H
