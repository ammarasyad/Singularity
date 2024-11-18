#ifndef MESHLETS_H
#define MESHLETS_H
#include "vk/vk_common.h"
#include <vector>

static constexpr uint32_t MAX_MESHLET_PRIMITIVES = 124;
static constexpr uint32_t MAX_MESHLET_VERTICES = 64;

struct Meshlet {
    uint32_t vertexOffset;
    uint32_t vertexCount;
    uint32_t indexOffset;
    uint32_t indexCount;
    glm::vec4 boundingSphere;
};

std::vector<Meshlet> GenerateMeshlets(const std::vector<VkVertex> &vertices, const std::vector<uint32_t> &indices);

#endif //MESHLETS_H
