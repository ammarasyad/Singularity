#ifndef RADIANCE_CASCADES_H
#define RADIANCE_CASCADES_H
#include "graphics/vk/vk_common.h"

#define RADIANCE_PROBE_GRID_SIZE 4
#define NUM_CASCADES 4

class VkRenderer;

struct RadianceProbe
{
    glm::vec4 position;
    glm::vec4 irradiance;
};

struct RadianceCascade
{
    glm::vec3 gridOrigin;
    glm::ivec3 resolution;
    float spacing;
};

class RadianceCascades
{
public:
    RadianceCascades() = default;
    void InitCascades(VkRenderer *renderer, const glm::vec3 &cameraPosition, int coverage);
    void BuildRadianceCascades(VkDevice device, VkCommandBuffer commandBuffer);
private:
    RadianceCascade cascades[NUM_CASCADES];
    uint32_t totalProbeCount;
    VulkanBuffer cascadesBuffer;

    VkDescriptorSetLayout cascadesLayout;
    VkDescriptorSet cascadesDescriptorSet;
    VkPipelineLayout radiancePipelineLayout;
    VkPipeline radianceComputePipeline;
};

#endif
