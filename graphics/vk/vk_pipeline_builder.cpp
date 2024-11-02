#include "vk_pipeline_builder.h"

#include "file.h"

VkPipeline VkGraphicsPipelineBuilder::Build(const bool dynamicRendering, const VkDevice &device, const VkPipelineCache &pipelineCache, const VkRenderPass &renderPass, const SpecializationInfoHelper &info) {
    if (!dynamicRendering && !renderPass)
        throw std::runtime_error("Render pass must be provided if not using dynamic rendering.");

    const VkPipelineShaderStageCreateInfo vertexShaderStageInfo{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        VK_SHADER_STAGE_VERTEX_BIT,
        vertexShaderModule,
        "main",
        info.stageFlags & VK_SHADER_STAGE_VERTEX_BIT ? &info.specializationInfo : VK_NULL_HANDLE
    };

    const VkPipelineShaderStageCreateInfo fragmentShaderStageInfo{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        fragmentShaderModule,
        "main",
        info.stageFlags & VK_SHADER_STAGE_FRAGMENT_BIT ? &info.specializationInfo : VK_NULL_HANDLE
    };

//    VkPipelineShaderStageCreateInfo shaderStages[] = {vertexShaderStageInfo, fragmentShaderStageInfo};
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages{vertexShaderStageInfo};

    if (fragmentShaderModule != VK_NULL_HANDLE)
        shaderStages.emplace_back(fragmentShaderStageInfo);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        static_cast<uint32_t>(vertexInputBindingDescriptions.size()),
        vertexInputBindingDescriptions.data(),
        static_cast<uint32_t>(vertexInputAttributeDescriptions.size()),
        vertexInputAttributeDescriptions.data()
    };

    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        1,
        VK_NULL_HANDLE,
        1,
        VK_NULL_HANDLE
    };

    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        VK_SAMPLE_COUNT_1_BIT
    };

    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        VK_FALSE,
        VK_LOGIC_OP_COPY,
        1,
        &colorBlendAttachment
    };

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        2,
        dynamicStates
    };

    rasterizerCreateInfo.lineWidth = 1.0f;

    VkGraphicsPipelineCreateInfo pipelineInfo{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        dynamicRendering ? &renderingCreateInfo : VK_NULL_HANDLE,
        0,
        static_cast<uint32_t>(shaderStages.size()),
        shaderStages.data(),
        &vertexInputInfo,
        &inputAssemblyCreateInfo,
        VK_NULL_HANDLE,
        &viewportState,
        &rasterizerCreateInfo,
        &multisample,
        &depthStencilCreateInfo,
        &colorBlending,
        &dynamicState,
        pipelineLayout,
        dynamicRendering ? VK_NULL_HANDLE : renderPass
    };

    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineInfo, VK_NULL_HANDLE, &pipeline));

    return pipeline;
}

void VkGraphicsPipelineBuilder::Clear() {
    inputAssemblyCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    rasterizerCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    depthStencilCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    colorBlendAttachment = {VK_FALSE};
    renderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    colorAttachmentFormat = VK_FORMAT_UNDEFINED;
}

void VkGraphicsPipelineBuilder::CreateShaderModules(const VkDevice &device, const std::string &vertexShaderFilePath, const std::string &fragmentShaderFilePath) {
    const auto vertexShaderCode = ReadFile<char>(vertexShaderFilePath);

    std::vector<char> fragmentShaderCode{};
    if (!fragmentShaderFilePath.empty())
        fragmentShaderCode = ReadFile<char>(fragmentShaderFilePath);

    const VkShaderModuleCreateInfo vertShaderModuleCreateInfo{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        vertexShaderCode.size(),
        reinterpret_cast<const uint32_t *>(vertexShaderCode.data())
    };

    const VkShaderModuleCreateInfo fragShaderModuleCreateInfo{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        fragmentShaderCode.size(),
        reinterpret_cast<const uint32_t *>(fragmentShaderCode.data())
    };

    VK_CHECK(vkCreateShaderModule(device, &vertShaderModuleCreateInfo, VK_NULL_HANDLE, &vertexShaderModule));

    if (!fragmentShaderFilePath.empty())
        VK_CHECK(vkCreateShaderModule(device, &fragShaderModuleCreateInfo, VK_NULL_HANDLE, &fragmentShaderModule));
}

void VkGraphicsPipelineBuilder::DestroyShaderModules(const VkDevice &device) {
    vkDestroyShaderModule(device, vertexShaderModule, VK_NULL_HANDLE);

    if (fragmentShaderModule != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, fragmentShaderModule, VK_NULL_HANDLE);
}

void VkGraphicsPipelineBuilder::AddBindingDescription(uint32_t binding, uint32_t stride, VkVertexInputRate inputRate) {
    vertexInputBindingDescriptions.emplace_back(binding, stride, inputRate);
}

void VkGraphicsPipelineBuilder::AddAttributeDescription(uint32_t location, uint32_t binding, VkFormat format, uint32_t offset) {
    vertexInputAttributeDescriptions.emplace_back(location, binding, format, offset);
}

void VkGraphicsPipelineBuilder::SetPipelineLayout(VkPipelineLayout pipelineLayout) {
    this->pipelineLayout = pipelineLayout;
}

void VkGraphicsPipelineBuilder::SetTopology(const VkPrimitiveTopology topology) {
    inputAssemblyCreateInfo.topology = topology;
}

void VkGraphicsPipelineBuilder::SetPolygonMode(const VkPolygonMode polygonMode) {
    rasterizerCreateInfo.polygonMode = polygonMode;
}

void VkGraphicsPipelineBuilder::SetCullingMode(const VkCullModeFlags cullMode, const VkFrontFace frontFace) {
    rasterizerCreateInfo.cullMode = cullMode;
    rasterizerCreateInfo.frontFace = frontFace;
}

void VkGraphicsPipelineBuilder::EnableClampMode() {
    rasterizerCreateInfo.depthClampEnable = VK_TRUE;
}

void VkGraphicsPipelineBuilder::EnableDepthTest(const bool depthWrite, const VkCompareOp compareOp) {
    depthStencilCreateInfo.depthTestEnable = VK_TRUE;
    depthStencilCreateInfo.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
    depthStencilCreateInfo.depthCompareOp = compareOp;
    depthStencilCreateInfo.maxDepthBounds = 1.0f;
}

void VkGraphicsPipelineBuilder::EnableBlendingAdditive() {
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
}

void VkGraphicsPipelineBuilder::EnableBlendingAlphaBlend() {
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
}

void VkGraphicsPipelineBuilder::SetColorAttachmentFormat(const VkFormat format) {
    colorAttachmentFormat = format;

    renderingCreateInfo.colorAttachmentCount = 1;
    renderingCreateInfo.pColorAttachmentFormats = &colorAttachmentFormat;
}

void VkGraphicsPipelineBuilder::SetDepthFormat(const VkFormat format) {
    renderingCreateInfo.depthAttachmentFormat = format;
}
