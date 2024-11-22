#include "material.h"
#include "file.h"
#include "vk_renderer.h"
#include "vk/vk_pipeline_builder.h"

void VkGLTFMetallic_Roughness::buildPipelines(const VkRenderer *renderer) {
    const auto device = renderer->device;
    constexpr VkPushConstantRange pushConstantRange{
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(MeshPushConstants)
    };

    constexpr VkPushConstantRange meshShaderPushConstantRange {
        VK_SHADER_STAGE_MESH_BIT_EXT,
        0,
        sizeof(MeshShaderPushConstants)
    };

    constexpr VkPushConstantRange fragmentPushConstantRange{
        VK_SHADER_STAGE_FRAGMENT_BIT,
        sizeof(MeshPushConstants),
        sizeof(FragmentPushConstants)
    };

    DescriptorLayoutBuilder layoutBuilder;

    layoutBuilder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_MESH_BIT_EXT);
    layoutBuilder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    layoutBuilder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);

    materialLayout = layoutBuilder.Build(device);

    std::array setLayouts = {renderer->sceneDescriptorSetLayout, materialLayout, renderer->mainDescriptorSetLayout};
    constexpr std::array pushConstantRanges = {pushConstantRange, fragmentPushConstantRange};
    const VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        setLayouts.size(),
        setLayouts.data(),
        renderer->meshShader ? 1 : static_cast<uint32_t>(pushConstantRanges.size()),
        renderer->meshShader ? &meshShaderPushConstantRange : pushConstantRanges.data(),
    };

    VkPipelineLayout pipelineLayout;
    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, VK_NULL_HANDLE, &pipelineLayout));

    opaquePipeline.layout = pipelineLayout;
    transparentPipeline.layout = pipelineLayout;

    VkGraphicsPipelineBuilder builder{.isMeshShader = renderer->meshShader};
    builder.SetPipelineLayout(pipelineLayout);

    if (builder.isMeshShader) {
        builder.CreateShaderModules(device, "shaders/meshshader.mesh.spv", "shaders/meshshader.frag.spv", "shaders/meshshader.task.spv");
    } else {
        builder.CreateShaderModules(device, "shaders/mesh.vert.spv", "shaders/lighting.frag.spv");
    }

    builder.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    builder.SetPolygonMode(VK_POLYGON_MODE_FILL);
    builder.SetCullingMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    builder.EnableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);

    static constexpr std::array<VkSpecializationMapEntry, 2> entries = {{
         {0, 0, sizeof(uint32_t)},
         {1, sizeof(uint32_t), sizeof(uint32_t)}
    }};

    static constexpr uint32_t data[] = {1, SHADOW_MAP_CASCADE_COUNT};

    VkSpecializationInfo specializationInfo{
            entries.size(),
            entries.data(),
            sizeof(uint32_t) * 2,
            data
    };

    bool dynamicRendering = renderer->dynamicRendering;
    if (dynamicRendering) {
         builder.SetColorAttachmentFormat(renderer->surfaceFormat.format);
         builder.SetDepthFormat(renderer->depthImage.format);
    }

    opaquePipeline.pipeline = builder.Build(dynamicRendering, device, renderer->pipelineCache, renderer->renderPass, {VK_SHADER_STAGE_FRAGMENT_BIT, specializationInfo});

    builder.EnableBlendingAlphaBlend();
    builder.EnableDepthTest(false, VK_COMPARE_OP_LESS_OR_EQUAL);
    transparentPipeline.pipeline = builder.Build(dynamicRendering, device, renderer->pipelineCache, renderer->renderPass, {VK_SHADER_STAGE_FRAGMENT_BIT, specializationInfo});

    builder.DestroyShaderModules(device);
}

void VkGLTFMetallic_Roughness::clearResources(const VkDevice &device) const {
    vkDestroyDescriptorSetLayout(device, materialLayout, VK_NULL_HANDLE);
    vkDestroyPipelineLayout(device, opaquePipeline.layout, VK_NULL_HANDLE);

    vkDestroyPipeline(device, opaquePipeline.pipeline, VK_NULL_HANDLE);
    vkDestroyPipeline(device, transparentPipeline.pipeline, VK_NULL_HANDLE);
}

VkMaterialInstance VkGLTFMetallic_Roughness::writeMaterial(VkDevice &device, const MaterialPass pass, const MaterialResources &resources, DescriptorAllocator &allocator) {
    const VkDescriptorSetLayout setLayouts[] = {materialLayout};
    VkMaterialInstance matData{
        .pipeline = pass == MaterialPass::Transparent ? transparentPipeline : opaquePipeline,
        .descriptorSet = allocator.Allocate(device, setLayouts),
        .pass = pass
    };

    descriptorWriter.Clear();
    descriptorWriter.WriteBuffer(0, resources.dataBuffer, resources.offset, sizeof(MaterialConstants), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    descriptorWriter.WriteImage(1, resources.colorImage.imageView, resources.colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    descriptorWriter.WriteImage(2, resources.metalRoughImage.imageView, resources.metalRoughSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    descriptorWriter.UpdateSet(device, matData.descriptorSet);

    return matData;
}
