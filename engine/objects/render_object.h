#ifndef RENDEROBJECT_H
#define RENDEROBJECT_H

#include <glm.hpp>

#include "graphics/vk/vk_common.h"
#include "graphics/vk/memory/vk_mesh_assets.h"

struct VkMaterialInstance;
struct VkDrawContext;

struct VkRenderObject {
    uint32_t indexCount{};
    uint32_t firstIndex{};

    Bounds bounds{};
    glm::mat4 transform{};

    VkBuffer indexBuffer{VK_NULL_HANDLE};
    VkDeviceAddress vertexBufferAddress{0};
    VkDeviceAddress indexBufferAddress{0};

    VkMaterialInstance *materialInstance{nullptr};
};

struct VkDrawContext {
    std::vector<VkRenderObject> opaqueSurfaces;
    std::vector<VkRenderObject> transparentSurfaces;
};

enum class NodeType {
    Node = 0,
    MeshNode = 1
};

struct Node {
    glm::mat4 localTransform{};
    glm::mat4 worldTransform{};

    std::weak_ptr<Node> parent;
    std::vector<std::shared_ptr<Node>> children;

    NodeType type;
    MeshAsset meshAsset;

    void RefreshTransform(const glm::mat4 &parentTransform) {
        worldTransform = parentTransform * localTransform;
        for (const auto &c : children) {
            c->RefreshTransform(worldTransform);
        }
    }

    void Draw(const glm::mat4 &topMatrix, VkDrawContext &ctx) {
        switch (type) {
            case NodeType::MeshNode: {
                const glm::mat4 nodeMatrix = topMatrix * worldTransform;

                for (auto &[startIndex, indexCount, bounds, material]: meshAsset.surfaces) {
                    switch (material.data.pass) {
                        case MaterialPass::MainColor:
                            ctx.opaqueSurfaces.emplace_back(indexCount, startIndex, bounds, nodeMatrix, meshAsset.mesh.indexBuffer, meshAsset.mesh.vertexBufferDeviceAddress, meshAsset.mesh.indexBufferDeviceAddress, &material.data);
                            break;
                        case MaterialPass::Transparent:
                            ctx.transparentSurfaces.emplace_back(indexCount, startIndex, bounds, nodeMatrix, meshAsset.mesh.indexBuffer, meshAsset.mesh.vertexBufferDeviceAddress, meshAsset.mesh.indexBufferDeviceAddress, &material.data);
                            break;
                        default:
                            break;
                    }
                }
            }

            case NodeType::Node: {
                for (const auto &c: children) {
                    c->Draw(topMatrix, ctx);
                }
                break;
            }
        }
    }
};

#endif //RENDEROBJECT_H
