#ifndef RENDEROBJECT_H
#define RENDEROBJECT_H

#ifdef _WIN32
#include <d3d12.h>
#include <wrl/client.h>
#endif

#include <cstdint>
#include <glm.hpp>
#include <memory>

#include "vk/vk_common.h"
#include "vk/memory/vk_mesh_assets.h"

struct VkMaterialInstance;
class D3D12Renderer;
struct VkDrawContext;

struct VkRenderObject {
    uint32_t indexCount{};
    uint32_t firstIndex{};

    Bounds bounds{};
    glm::mat4 transform{};

    VkBuffer indexBuffer{VK_NULL_HANDLE};
    VkDeviceAddress vertexBufferAddress{0};

    VkMaterialInstance *materialInstance{nullptr};
};

struct VkDrawContext {
    std::vector<VkRenderObject> opaqueSurfaces;
    std::vector<VkRenderObject> transparentSurfaces;
};

// struct D3D12RenderObject final : RenderObject {
//     Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
//     D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
//
//     void Draw(const glm::mat4 &topMatrix, DrawContext &renderer) override;
// };

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
                            ctx.opaqueSurfaces.emplace_back(indexCount, startIndex, bounds, nodeMatrix, meshAsset.mesh.indexBuffer, meshAsset.mesh.vertexBufferDeviceAddress, &material.data);
                            break;
                        case MaterialPass::Transparent:
                            ctx.transparentSurfaces.emplace_back(indexCount, startIndex, bounds, nodeMatrix, meshAsset.mesh.indexBuffer, meshAsset.mesh.vertexBufferDeviceAddress,&material.data);
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
