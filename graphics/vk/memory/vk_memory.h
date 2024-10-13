#ifndef D3D12_STUFF_VK_MEMORY_H
#define D3D12_STUFF_VK_MEMORY_H

#include "vk_mem_alloc.h"
#include "vk/vk_common.h"
#include <functional>
#include <unordered_map>

class VkRenderer;

struct ImageViewCreateInfo {
    VkImageViewCreateFlags flags;
    VkImageViewType viewType;
    VkFormat format;
    VkComponentMapping components;
    VkImageSubresourceRange subresourceRange;
};

class VkMemoryManager {
public:
    VkMemoryManager(const VkInstance&, const VkPhysicalDevice&, const VkDevice&, bool customPool = false);
    ~VkMemoryManager();

    void immediateBuffer(const VkBufferCreateInfo *bufferCreateInfo, VmaAllocationCreateInfo *allocationCreateInfo, const std::function<void(VkBuffer &, void *)> &mappedMemoryTask, const std::function<void(VkBuffer &)> &unmappedMemoryTask = nullptr) const;
    VulkanBuffer createManagedBuffer(const VkBufferCreateInfo *bufferCreateInfo, VmaAllocationCreateInfo *allocationCreateInfo, long minAlignment = 0);
    VulkanImage createManagedImage(const VkImageCreateInfo *imageCreateInfo, VmaAllocationCreateInfo *allocationCreateInfo, const ImageViewCreateInfo *imageViewCreateInfo = nullptr);
    VulkanBuffer createUnmanagedBuffer(const VkBufferCreateInfo *bufferCreateInfo, VmaAllocationCreateInfo *allocationCreateInfo, long minAlignment = 0) const;
    VulkanImage createUnmanagedImage(const VkImageCreateInfo *imageCreateInfo, VmaAllocationCreateInfo *allocationCreateInfo, const ImageViewCreateInfo *imageViewCreateInfo = nullptr) const;

    // All textures are tracked.
    VulkanImage createTexture(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    VulkanImage createTexture(void *data, VkRenderer *renderer, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);

    void mapBuffer(const VkBuffer *buffer, void **data, const VmaAllocation *allocation = nullptr);
    void unmapBuffer(const VkBuffer *buffer, const VmaAllocation *allocation = nullptr);

    void destroyBuffer(const VkBuffer *buffer, const VmaAllocation *allocation = nullptr);

    [[nodiscard]] VmaAllocator getAllocator() const {
        return allocator;
    }
private:
    VmaAllocator allocator;
    VkDevice device;
    VmaPool pool;

    std::unordered_map<VkBuffer, VmaAllocation> trackedBuffers;
    std::vector<VulkanImage> trackedImages;
};

#endif
