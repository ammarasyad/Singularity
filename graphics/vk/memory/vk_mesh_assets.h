#ifndef VK_MESH_ASSETS_H
#define VK_MESH_ASSETS_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "vk_common.h"

struct GeoSurface {
    uint32_t startIndex;
    uint32_t indexCount;
};

struct MeshAsset {
    std::string name;

    std::vector<GeoSurface> surfaces;
    Mesh mesh;
};

class VkRenderer;

std::optional<std::vector<std::shared_ptr<MeshAsset>>> LoadGltfMeshes(VkRenderer *renderer, const std::filesystem::path& path);

#endif //VK_MESH_ASSETS_H
