#ifndef RENDEROBJECT_H
#define RENDEROBJECT_H
#include <cstdint>
#include <d3d12.h>
#include <glm.hpp>
#include <memory>
#include <wrl/client.h>

#include "vk/vk_common.h"
#include "vk/memory/vk_mesh_assets.h"

struct VkMaterialInstance;
class D3D12Renderer;
struct VkDrawContext;

struct RenderObject {
    uint32_t indexCount{};
    uint32_t firstIndex{};

    Bounds bounds{};
    glm::mat4 transform{};
};

struct VkRenderObject final : RenderObject {
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

class IRenderable {
public:
    virtual ~IRenderable() = default;

protected:
    virtual void Draw(const glm::mat4 &topMatrix, VkDrawContext &renderer) = 0;
};

struct Node : IRenderable {
    std::weak_ptr<Node> parent;
    std::vector<std::shared_ptr<Node>> children;

    glm::mat4 localTransform{};
    glm::mat4 worldTransform{};

    void RefreshTransform(const glm::mat4 &parentTransform) {
        worldTransform = parentTransform * localTransform;
        for (const auto &c : children) {
            c->RefreshTransform(worldTransform);
        }
    }

    void Draw(const glm::mat4 &topMatrix, VkDrawContext &renderer) override {
        for (const auto &c : children) {
            c->Draw(topMatrix, renderer);
        }
    }
};

struct MeshNode final : Node {
    MeshAsset meshAsset;

    void Draw(const glm::mat4 &topMatrix, VkDrawContext &ctx) override {
        const glm::mat4 nodeMatrix = topMatrix * worldTransform;

        for (auto &[startIndex, indexCount, bounds, material] : meshAsset.surfaces) {
            VkRenderObject renderObject;
            renderObject.indexCount = indexCount;
            renderObject.firstIndex = startIndex;
            renderObject.indexBuffer = meshAsset.mesh.indexBuffer;
            renderObject.materialInstance = &material.data;
            renderObject.bounds = bounds;
            renderObject.transform = nodeMatrix;
            renderObject.vertexBufferAddress = meshAsset.mesh.vertexBufferDeviceAddress;

            if (material.data.pass == MaterialPass::MainColor) {
                ctx.opaqueSurfaces.emplace_back(renderObject);
            } else if (material.data.pass == MaterialPass::Transparent) {
                ctx.transparentSurfaces.emplace_back(renderObject);
            }
        }

        Node::Draw(topMatrix, ctx);
    }
};

#endif //RENDEROBJECT_H
