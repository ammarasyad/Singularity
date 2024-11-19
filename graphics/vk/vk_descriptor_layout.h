#ifndef D3D12_STUFF_VK_DESCRIPTOR_LAYOUT_H
#define D3D12_STUFF_VK_DESCRIPTOR_LAYOUT_H

#include <deque>
#include <vector>
#include <span>
#include "vk_common.h"

struct DescriptorLayoutBuilder {
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    void AddBinding(const uint32_t binding, const VkDescriptorType &type, const VkShaderStageFlags &stageFlags = VK_SHADER_STAGE_ALL) {
        bindings.emplace_back(binding, type, 1, stageFlags, nullptr);
    }

    void Clear() {
        bindings.clear();
    }

    VkDescriptorSetLayout Build(const VkDevice &device, void *pNext = VK_NULL_HANDLE, const VkDescriptorSetLayoutCreateFlags &flags = 0) {
        const VkDescriptorSetLayoutCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = pNext,
            .flags = flags,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data()
        };

        VkDescriptorSetLayout descriptorSetLayout;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &descriptorSetLayout));

        return descriptorSetLayout;
    }
};

struct DescriptorWriter {
    void WriteImage(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type);
    void WriteBuffer(int binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range, VkDescriptorType type);
    void Clear();
    void UpdateSet(VkDevice &device, VkDescriptorSet &descriptorSet);

    std::deque<VkDescriptorImageInfo> imageInfos;
    std::deque<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkWriteDescriptorSet> writes;
};

struct DescriptorAllocator {
    struct PoolSizeRatio {
        VkDescriptorType type;
        float ratio;
    };

    void InitPool(const VkDevice &device, uint32_t maxSets, std::span<PoolSizeRatio>poolRatios);
    void ClearPools(const VkDevice &device);
    void Destroy(const VkDevice &device);
    [[nodiscard]] VkDescriptorSet Allocate(const VkDevice &device, std::span<VkDescriptorSetLayout>layout);

private:
    VkDescriptorPool GetPool(const VkDevice &device);
    static VkDescriptorPool CreatePool(const VkDevice &device, uint32_t maxSets, std::span<PoolSizeRatio>poolRatios);

    std::vector<PoolSizeRatio> ratios;
    std::vector<VkDescriptorPool> fullPools;
    std::vector<VkDescriptorPool> readyPools;
    uint32_t setsPerPool{};
};

// struct DescriptorAllocator {
//     struct PoolSizeRatio {
//         VkDescriptorType type;
//         float ratio;
//     };
//
//     VkDescriptorPool pool;
//
//     void InitPool(const VkDevice &device, const uint32_t maxSets, std::span<PoolSizeRatio>poolRatios) {
//         std::vector<VkDescriptorPoolSize> poolSizes;
//         poolSizes.reserve(poolRatios.size());
//
//         for (auto &[type, ratio] : poolRatios) {
//             poolSizes.emplace_back(type, static_cast<uint32_t>(static_cast<float>(maxSets) * ratio));
//         }
//
//         const VkDescriptorPoolCreateInfo poolInfo{
//             .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
//             .maxSets = maxSets,
//             .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
//             .pPoolSizes = poolSizes.data()
//         };
//
//         VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool));
//     }
//
//     void ClearDescriptors(const VkDevice &device) const {
//         VK_CHECK(vkResetDescriptorPool(device, pool, 0));
//     }
//
//     void Destroy(const VkDevice &device) const {
//         vkDestroyDescriptorPool(device, pool, nullptr);
//     }
//
//     [[nodiscard]] VkDescriptorSet Allocate(const VkDevice &device, std::span<VkDescriptorSetLayout>layout) const {
//         const VkDescriptorSetAllocateInfo allocateInfo{
//             .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
//             .descriptorPool = pool,
//             .descriptorSetCount = static_cast<uint32_t>(layout.size()),
//             .pSetLayouts = layout.data()
//         };
//
//         VkDescriptorSet descriptorSet;
//         VK_CHECK(vkAllocateDescriptorSets(device, &allocateInfo, &descriptorSet));
//
//         return descriptorSet;
//     }
// };

#endif //D3D12_STUFF_VK_DESCRIPTOR_LAYOUT_H
