#ifndef VK_PIPELINE_BUILDER_H
#define VK_PIPELINE_BUILDER_H

#include <vector>
#include "vk/vk_common.h"

struct SpecializationInfoHelper {
    VkPipelineStageFlags stageFlags;
    VkSpecializationInfo specializationInfo;
};

struct VkGraphicsPipelineBuilder {
    VkPipeline Build(bool dynamicRendering, const VkDevice &device, const VkPipelineCache &pipelineCache, const VkRenderPass &renderPass = VK_NULL_HANDLE, const SpecializationInfoHelper &info = {});
    void Clear();

    void CreateShaderModules(const VkDevice &device, const std::string &vertexOrMeshShaderFilePath, const std::string &fragmentShaderFilePath, const std::string &taskShaderFilePath = "");
    void DestroyShaderModules(const VkDevice &device) const;
    void AddBindingDescription(uint32_t binding, uint32_t stride, VkVertexInputRate inputRate);
    void AddAttributeDescription(uint32_t location, uint32_t binding, VkFormat format, uint32_t offset);
    void SetPipelineLayout(VkPipelineLayout pipelineLayout);
    void SetTopology(VkPrimitiveTopology topology);
    void SetPolygonMode(VkPolygonMode polygonMode);
    void SetCullingMode(VkCullModeFlags cullMode, VkFrontFace frontFace);
    void EnableClampMode();
    void EnableDepthTest(bool depthWrite, VkCompareOp compareOp);
    void EnableBlendingAdditive();
    void EnableBlendingAlphaBlend();

    // Dynamic rendering stuff
    void SetColorAttachmentFormat(VkFormat format);
    void SetDepthFormat(VkFormat format);

    VkShaderModule vertexOrMeshShaderModule{VK_NULL_HANDLE};
    VkShaderModule taskShaderModule{VK_NULL_HANDLE};
    VkShaderModule fragmentShaderModule{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    VkPipelineRasterizationStateCreateInfo rasterizerCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    VkPipelineDepthStencilStateCreateInfo depthStencilCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};

    VkPipelineRenderingCreateInfo renderingCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};

    VkFormat colorAttachmentFormat{VK_FORMAT_UNDEFINED};
    bool isMeshShader{false};

    std::vector<VkVertexInputBindingDescription> vertexInputBindingDescriptions{};
    std::vector<VkVertexInputAttributeDescription> vertexInputAttributeDescriptions{};
};

#endif //VK_PIPELINE_BUILDER_H
