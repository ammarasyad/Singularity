#ifndef RADIANCE_CASCADES_H
#define RADIANCE_CASCADES_H
#include "graphics/vk/vk_common.h"

#define RADIANCE_PROBE_GRID_SIZE 4
#define NUM_CASCADES 4

class VkRenderer;

struct RadianceCascade
{
    alignas(16) glm::vec3 origin;
    alignas(16) glm::vec3 spacing;
    alignas(16) glm::uvec3 resolution;
};

class RadianceCascades
{
public:
    RadianceCascades() = default;
    void InitCascades(VkRenderer *renderer);

    struct RadianceCascadesParameters
    {
        RadianceCascade cascades[NUM_CASCADES];
        uint32_t cascadeIndex;
        uint32_t probeIndex;
        uint32_t raysPerProbe;
        uint32_t rayIndex;
    } cascadeData;

    VulkanBuffer cascadesBuffer;
    VulkanImage radianceImageArray;
};

#endif
