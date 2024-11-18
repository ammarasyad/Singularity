#ifndef GLTF_H
#define GLTF_H
#include "render_object.h"

struct LoadedGLTF {
//    ~LoadedGLTF() override { clear(); }
    void Draw(const glm::mat4 &topMatrix, VkDrawContext &ctx);
    void Clear(const VkRenderer *renderer);

    std::vector<std::shared_ptr<Node>> rootNodes;
    std::vector<VkSampler> samplers;

    DescriptorAllocator descriptorAllocator;
    VulkanBuffer materialDataBuffer;
};

std::optional<LoadedGLTF> LoadGLTF(VkRenderer *renderer, bool multithread, const std::filesystem::path &path, const std::filesystem::path &assetPath);

#endif //GLTF_H
