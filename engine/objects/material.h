//
// Created by Ammar on 13/10/2024.
//

#ifndef MATERIAL_H
#define MATERIAL_H

#include <d3d12.h>
#include <wrl/client.h>

#include "vk/vk_descriptor_layout.h"
#include "vk/vk_common.h"

class VkRenderer;

enum class MaterialPass : uint8_t {
    MainColor,
    Transparent,
    Other
};

struct VkMaterialPipeline {
    VkPipeline pipeline;
    VkPipelineLayout layout;
};

struct VkMaterialInstance {
    VkMaterialPipeline *pipeline;
    VkDescriptorSet descriptorSet;
    MaterialPass pass;
};

struct D3D12MaterialPipeline {
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
};

struct D3D12MaterialInstance {
    D3D12MaterialPipeline *pipeline;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    MaterialPass pass;
};

struct VkGLTFMetallic_Roughness {
    struct MaterialConstants {
        glm::vec4 colorFactors;
        glm::vec4 metalRoughFactors;
        const glm::vec4 padding[14];
    };

    struct MaterialResources {
        VulkanImage colorImage;
        VkSampler colorSampler;
        VulkanImage metalRoughImage;
        VkSampler metalRoughSampler;
        VkBuffer dataBuffer;
        uint32_t offset;
    };

    VkMaterialPipeline opaquePipeline{VK_NULL_HANDLE};
    VkMaterialPipeline transparentPipeline{VK_NULL_HANDLE};
    VkDescriptorSetLayout materialLayout{VK_NULL_HANDLE};
    DescriptorWriter descriptorWriter{};

    void buildPipelines(const VkRenderer *renderer);
    void clearResources(const VkDevice &device) const;

    VkMaterialInstance writeMaterial(VkDevice &device, MaterialPass pass, const MaterialResources &resources, DescriptorAllocator &allocator);
};

#endif //MATERIAL_H
