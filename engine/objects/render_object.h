//
// Created by Ammar on 12/10/2024.
//

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
    virtual ~RenderObject() = default;

    uint32_t indexCount{};
    uint32_t firstIndex{};

    glm::mat4 transform{};
};

struct VkRenderObject final : RenderObject {
    VkBuffer indexBuffer{VK_NULL_HANDLE};
    VkDeviceAddress vertexBufferAddress{0};

    VkMaterialInstance *materialInstance{nullptr};
};

struct VkDrawContext {
    std::vector<VkRenderObject> opaqueSurfaces;
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
        for (auto &c : children) {
            c->RefreshTransform(worldTransform);
        }
    }

    void Draw(const glm::mat4 &topMatrix, VkDrawContext &renderer) override {
        for (const auto &c : children) {
            c->Draw(topMatrix, renderer);
        }
    }
};

struct MeshNode : Node {
    std::shared_ptr<MeshAsset> mesh;

    void Draw(const glm::mat4 &topMatrix, VkDrawContext &ctx) override;
};

#endif //RENDEROBJECT_H
