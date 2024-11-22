#include "vk_descriptor_layout.h"
#include <bits/ranges_algobase.h>
#pragma region DescriptorWriter
void DescriptorWriter::WriteImage(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type) {
    auto &info = imageInfos.emplace_back(sampler, image, layout);

    writes.emplace_back(
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        binding,
        0,
        1,
        type,
        &info,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE
    );
}
void DescriptorWriter::WriteBuffer(int binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range, VkDescriptorType type) {
    auto &info = bufferInfos.emplace_back(buffer, offset, range);

    writes.emplace_back(
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        binding,
        0,
        1,
        type,
        VK_NULL_HANDLE,
        &info,
        VK_NULL_HANDLE
    );
}
void DescriptorWriter::Clear() {
    imageInfos.clear();
    bufferInfos.clear();
    writes.clear();
}

void DescriptorWriter::UpdateSet(VkDevice &device, VkDescriptorSet &descriptorSet) {
    for (auto &[sType, pNext, dstSet, dstBinding, dstArrayElement, descriptorCount, descriptorType, pImageInfo, pBufferInfo, pTexelBufferView] : writes) {
        dstSet = descriptorSet;
    }

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

#pragma endregion
#pragma region DescriptorAllocator
void DescriptorAllocator::InitPool(const VkDevice &device, const uint32_t maxSets, std::span<const PoolSizeRatio> poolRatios) {
    ratios.clear();
    std::ranges::copy(poolRatios, std::back_inserter(ratios));

    const auto newPool = CreatePool(device, maxSets, poolRatios);
    setsPerPool = maxSets * 2;
    fullPools.push_back(newPool);
}

void DescriptorAllocator::ClearPools(const VkDevice &device) {
    for (const auto &p : readyPools)
        vkResetDescriptorPool(device, p, 0);

    for (const auto &p : fullPools) {
        vkResetDescriptorPool(device, p, 0);
        readyPools.push_back(p);
    }

    fullPools.clear();
}

void DescriptorAllocator::Destroy(const VkDevice &device) {
    for (const auto &p : readyPools)
        vkDestroyDescriptorPool(device, p, nullptr);

    for (const auto &p : fullPools)
        vkDestroyDescriptorPool(device, p, nullptr);

    readyPools.clear();
    fullPools.clear();
}

VkDescriptorSet DescriptorAllocator::Allocate(const VkDevice &device, const std::span<const VkDescriptorSetLayout> layout) {
    auto poolToUse = GetPool(device);

    VkDescriptorSetAllocateInfo allocInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        VK_NULL_HANDLE,
        poolToUse,
        static_cast<uint32_t>(layout.size()),
        layout.data()
    };

    VkDescriptorSet descriptorSet;

    if (const auto result = vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet); result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
        fullPools.push_back(poolToUse);

        poolToUse = GetPool(device);
        allocInfo.descriptorPool = poolToUse;

        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
    }

    readyPools.push_back(poolToUse);
    return descriptorSet;
}

VkDescriptorPool DescriptorAllocator::GetPool(const VkDevice &device) {
    VkDescriptorPool newPool;

    if (!readyPools.empty()) {
        newPool = readyPools.back();
        readyPools.pop_back();
    } else {
        newPool = CreatePool(device, setsPerPool, ratios);

        setsPerPool *= 2;
        if (setsPerPool > 4096) {
            setsPerPool = 4096;
        }
    }

    return newPool;
}

VkDescriptorPool DescriptorAllocator::CreatePool(const VkDevice &device, const uint32_t maxSets, const std::span<const PoolSizeRatio> poolRatios) {
    std::vector<VkDescriptorPoolSize> poolSizes;
    poolSizes.reserve(poolRatios.size());
    for (auto &[type, ratio] : poolRatios) {
        poolSizes.emplace_back(type, static_cast<uint32_t>(static_cast<float>(maxSets) * ratio));
    }

    const VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = maxSets,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()
    };

    VkDescriptorPool pool;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool));
    return pool;
}
#pragma endregion