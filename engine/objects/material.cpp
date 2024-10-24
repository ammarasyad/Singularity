#include "material.h"
#include "file.h"
#include "vk_renderer.h"
#include "vk/vk_pipeline_builder.h"

void VkGLTFMetallic_Roughness::buildPipelines(const VkRenderer *renderer) {
    const auto device = renderer->logical_device();
    constexpr VkPushConstantRange pushConstantRange{
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(MeshPushConstants)
    };

    constexpr VkPushConstantRange fragmentPushConstantRange{
        VK_SHADER_STAGE_FRAGMENT_BIT,
        sizeof(MeshPushConstants),
        sizeof(FragmentPushConstants)
    };

    DescriptorLayoutBuilder layoutBuilder;

    layoutBuilder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
    layoutBuilder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    layoutBuilder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);

    materialLayout = layoutBuilder.Build(device);

    std::array setLayouts = {renderer->scene_descriptor_set_layout(), materialLayout, renderer->main_descriptor_set_layout()};
    std::array pushConstantRanges = {pushConstantRange, fragmentPushConstantRange};
    const VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        setLayouts.size(),
        setLayouts.data(),
        pushConstantRanges.size(),
        pushConstantRanges.data(),
    };

    VkPipelineLayout pipelineLayout;
    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, VK_NULL_HANDLE, &pipelineLayout));

    opaquePipeline.layout = pipelineLayout;
    transparentPipeline.layout = pipelineLayout;

    VkGraphicsPipelineBuilder builder;
    builder.SetPipelineLayout(pipelineLayout);
    builder.CreateShaderModules(device, "shaders/mesh.vert.spv", "shaders/lighting.frag.spv");
    builder.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    builder.SetPolygonMode(VK_POLYGON_MODE_FILL);
    builder.SetCullingMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    builder.EnableDepthTest(false, VK_COMPARE_OP_EQUAL);

    // if (renderer->is_dynamic_rendering()) {
    //     builder.SetColorAttachmentFormat(renderer->draw_image().format);
    // }
    opaquePipeline.pipeline = builder.Build(false, device, renderer->pipeline_cache(), renderer->render_pass());

    builder.EnableBlendingAdditive();
    transparentPipeline.pipeline = builder.Build(false, device, renderer->pipeline_cache(), renderer->render_pass());

    builder.DestroyShaderModules(device);
}

void VkGLTFMetallic_Roughness::clearResources(const VkDevice &device) const {
    vkDestroyDescriptorSetLayout(device, materialLayout, VK_NULL_HANDLE);
    vkDestroyPipelineLayout(device, opaquePipeline.layout, VK_NULL_HANDLE);

    vkDestroyPipeline(device, opaquePipeline.pipeline, VK_NULL_HANDLE);
    vkDestroyPipeline(device, transparentPipeline.pipeline, VK_NULL_HANDLE);
}

VkMaterialInstance VkGLTFMetallic_Roughness::writeMaterial(VkDevice &device, MaterialPass pass, const MaterialResources &resources, DescriptorAllocator &allocator) {
    VkDescriptorSetLayout setLayouts[] = {materialLayout};
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
