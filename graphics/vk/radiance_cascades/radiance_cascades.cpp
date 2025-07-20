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

void RadianceCascades::InitCascades(VkRenderer *renderer)
{

}
