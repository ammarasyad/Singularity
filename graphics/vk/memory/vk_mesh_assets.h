#ifndef VK_MESH_ASSETS_H
#define VK_MESH_ASSETS_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "vk/vk_common.h"
#include "objects/material.h"

struct GLTFMaterial {
    VkMaterialInstance data;
};

struct Bounds {
    glm::vec3 origin;
    float sphereRadius;
    glm::vec3 extents;
};

struct GeoSurface {
    uint32_t startIndex;
    uint32_t indexCount;
    Bounds bounds;
    GLTFMaterial material;
};

struct MeshAsset {
    std::string name;
    std::vector<GeoSurface> surfaces;
    Mesh mesh;
};

class VkRenderer;

std::optional<std::vector<std::shared_ptr<MeshAsset>>> LoadGltfMeshes(VkRenderer *renderer, const std::filesystem::path& path);

#endif //VK_MESH_ASSETS_H
