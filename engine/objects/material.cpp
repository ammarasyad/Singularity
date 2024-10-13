//
// Created by Ammar on 13/10/2024.
//

#include "material.h"
#include "file.h"
#include "vk_renderer.h"
#include "vk/vk_pipeline_builder.h"

void VkGLTFMetallic_Roughness::buildPipelines(const VkRenderer *renderer) {
    const auto device = renderer->logical_device();
    VkPushConstantRange pushConstantRange{
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(MeshPushConstants)
    };

    DescriptorLayoutBuilder layoutBuilder;

    layoutBuilder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
    layoutBuilder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    layoutBuilder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);

    materialLayout = layoutBuilder.Build(device);

    VkDescriptorSetLayout setLayouts[] = {renderer->scene_descriptor_set_layout(), materialLayout};
    const VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        2,
        setLayouts,
        1,
        &pushConstantRange,
    };

    VkPipelineLayout pipelineLayout;
    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, VK_NULL_HANDLE, &pipelineLayout));

    opaquePipeline.layout = pipelineLayout;
    transparentPipeline.layout = pipelineLayout;

    // TODO: Make a generic pipeline builder
    VkGraphicsPipelineBuilder builder;
    builder.SetPipelineLayout(pipelineLayout);
    builder.CreateShaderModules(device, "shaders/shader.vert.spv", "shaders/shader.frag.spv");
    builder.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    builder.SetPolygonMode(VK_POLYGON_MODE_FILL);
    builder.SetCullingMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    builder.EnableDepthTest(true, VK_COMPARE_OP_LESS);

    // if (renderer->is_dynamic_rendering()) {
    //     builder.SetColorAttachmentFormat(renderer->draw_image().format);
    // }
    const auto renderPass = renderer->render_pass();
    opaquePipeline.pipeline = builder.Build(false, device, renderer->pipeline_cache(), &renderPass);

    builder.EnableBlendingAdditive();
    builder.EnableDepthTest(false, VK_COMPARE_OP_LESS);
    transparentPipeline.pipeline = builder.Build(false, device, renderer->pipeline_cache(), &renderPass);

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
        .pipeline = pass == MaterialPass::Transparent ? &transparentPipeline : &opaquePipeline,
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
