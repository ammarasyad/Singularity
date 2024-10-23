#ifndef GLTF_H
#define GLTF_H
#include <unordered_map>

#include "render_object.h"

struct LoadedGLTF : IRenderable {
//    ~LoadedGLTF() override { clear(); }
    void Draw(const glm::mat4 &topMatrix, VkDrawContext &ctx) override;
    void Clear();

    std::unordered_map<std::string, MeshAsset> meshes;
    std::unordered_map<std::string, std::shared_ptr<Node>> nodes;
    std::unordered_map<std::string, VulkanImage> images;
    std::unordered_map<std::string, GLTFMaterial> materials;

    std::vector<std::shared_ptr<Node>> rootNodes;
    std::vector<VkSampler> samplers;

    DescriptorAllocator descriptorAllocator;
    VulkanBuffer materialDataBuffer;
    VkRenderer *renderer;
};

std::optional<LoadedGLTF> LoadGLTF(VkRenderer *renderer, const std::filesystem::path &path);

#endif //GLTF_H
