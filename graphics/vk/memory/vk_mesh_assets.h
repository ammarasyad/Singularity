#ifndef VK_MESH_ASSETS_H
#define VK_MESH_ASSETS_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>
#include "graphics/vk/vk_common.h"
#include "engine/objects/material.h"

struct GLTFMaterial {
    VkMaterialInstance data;
};

struct Bounds {
    glm::vec3 origin;
    glm::vec3 extents;
    float sphereRadius;
};

struct GeoSurface {
    uint32_t startIndex;
    uint32_t indexCount;
    uint32_t vertexCount;
    Bounds bounds;
    GLTFMaterial material;
};

struct MeshAsset {
    std::vector<GeoSurface> surfaces;
    Mesh mesh;
};

class VkRenderer;

std::optional<std::vector<std::shared_ptr<MeshAsset>>> LoadGltfMeshes(VkRenderer *renderer, const std::filesystem::path& path);

#endif //VK_MESH_ASSETS_H
