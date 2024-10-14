//
// Created by Ammar on 13/10/2024.
//

#ifndef GLTF_H
#define GLTF_H
#include <unordered_map>

#include "render_object.h"

struct LoadedGLTF : IRenderable {
    ~LoadedGLTF() override { clear(); }
    void Draw(const glm::mat4 &topMatrix, VkDrawContext &ctx) override;

//    std::unordered_map<std::string, std::shared_ptr<MeshAsset>> meshes;
    std::unordered_map<std::string, MeshAsset> meshes;
    std::unordered_map<std::string, std::shared_ptr<Node>> nodes;
    std::unordered_map<std::string, VulkanImage> images;
//    std::unordered_map<std::string, std::shared_ptr<GLTFMaterial>> materials;
    std::unordered_map<std::string, GLTFMaterial> materials;

    std::vector<std::shared_ptr<Node>> rootNodes;
    std::vector<VkSampler> samplers;

    DescriptorAllocator descriptorAllocator;
    VulkanBuffer materialDataBuffer;
    VkRenderer *renderer;
private:
    void clear();
};

std::optional<std::shared_ptr<LoadedGLTF>> LoadGLTF(VkRenderer *renderer, const std::filesystem::path &path);

#endif //GLTF_H
