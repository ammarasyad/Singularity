#include "radiance_cascades.h"

#include "common/file.h"
#include "graphics/vk_renderer.h"

struct PushConstants
{
    glm::uvec4 resolution;
    glm::vec4 origin;
    uint32_t cascadeIndex;
    float spacing;
};

void RadianceCascades::InitCascades(VkRenderer *renderer, const glm::vec3 &cameraPosition, const int coverage)
{
    for (int i = 0; i < NUM_CASCADES; i++)
    {
        auto &[gridOrigin, resolution, spacing] = cascades[i];
        gridOrigin = cameraPosition - glm::vec3(coverage / 2.f);
        resolution = glm::ivec3(coverage / (i + 1));
        spacing = i + 1;

        totalProbeCount += resolution.x * resolution.y * resolution.z;
    }

    cascadesBuffer = renderer->memoryManager.createManagedBuffer({
        totalProbeCount * sizeof(RadianceProbe),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    });

    // Descriptor set layout creation
    {
        DescriptorLayoutBuilder builder;
        // TODO: Add more bindings if needed
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
        cascadesLayout = builder.Build(renderer->device);

        const VkDescriptorSetLayout layouts[] = {cascadesLayout};
        cascadesDescriptorSet = renderer->mainDescriptorAllocator.Allocate(renderer->device, layouts);
    }

    // Pipeline layout creation
    {
        constexpr VkPushConstantRange pushConstantRange {
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(PushConstants)
        };

        const VkPipelineLayoutCreateInfo layoutCreateInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            VK_NULL_HANDLE,
            0,
            1,
            &cascadesLayout,
            1,
            &pushConstantRange
        };

        VK_CHECK(vkCreatePipelineLayout(renderer->device, &layoutCreateInfo, VK_NULL_HANDLE, &radiancePipelineLayout));
    }

    // Pipeline creation
    {
        const auto computeShaderCode = ReadFile<uint32_t>("shaders/radiance_cascades.comp.spv");
        const VkShaderModuleCreateInfo createInfo{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            VK_NULL_HANDLE,
            0,
            computeShaderCode.size(),
            computeShaderCode.data()
        };

        VkShaderModule computeShaderModule;
        VK_CHECK(vkCreateShaderModule(renderer->device, &createInfo, VK_NULL_HANDLE, &computeShaderModule));

        const VkPipelineShaderStageCreateInfo stageInfo{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            VK_NULL_HANDLE,
            0,
            VK_SHADER_STAGE_COMPUTE_BIT,
            computeShaderModule,
            "main"
        };

        const VkComputePipelineCreateInfo pipelineCreateInfo{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            VK_NULL_HANDLE,
            0,
            stageInfo,
            radiancePipelineLayout,
            VK_NULL_HANDLE,
            -1
        };

        VK_CHECK(vkCreateComputePipelines(renderer->device, renderer->pipelineCache, 1, &pipelineCreateInfo, VK_NULL_HANDLE, &radianceComputePipeline));
        vkDestroyShaderModule(renderer->device, computeShaderModule, VK_NULL_HANDLE);
    }
}

void RadianceCascades::BuildRadianceCascades(VkDevice device, VkCommandBuffer commandBuffer)
{
    {
        DescriptorWriter writer;
        writer.WriteBuffer(0, cascadesBuffer.buffer, 0,  totalProbeCount * sizeof(RadianceProbe), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.UpdateSet(device, cascadesDescriptorSet);
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, radianceComputePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, radiancePipelineLayout, 0, 1, &cascadesDescriptorSet, 0, nullptr);

    // TODO: Determine group count
    for (uint32_t i = 0; i < NUM_CASCADES; i++)
    {
        PushConstants pc{
            glm::uvec4(cascades[i].resolution, 1),
            glm::vec4{cascades[i].gridOrigin, 1},
            i,
            cascades[i].spacing
        };

        vkCmdPushConstants(commandBuffer, radiancePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdDispatch(commandBuffer, 8, 8, 8);
    }
}
