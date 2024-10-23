#ifndef D3D12_STUFF_VK_MEMORY_H
#define D3D12_STUFF_VK_MEMORY_H

#include "vk_mem_alloc.h"
#include "vk/vk_common.h"
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <ktx.h>

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
    VkMemoryManager(const VkInstance &, const VkPhysicalDevice &, const VkDevice &, bool, bool customPool = false);

    ~VkMemoryManager();

    void stagingBuffer(VkDeviceSize bufferSize, const std::function<void(VkBuffer &, void *)> &mappedMemoryTask,
                       const std::function<void(VkBuffer &)> &unmappedMemoryTask = nullptr) const;

    VulkanBuffer createManagedBuffer(VkDeviceSize bufferSize, VkBufferUsageFlags bufferUsage,
                                     VmaAllocationCreateFlags allocationFlags, VmaMemoryUsage allocationUsage,
                                     VkMemoryPropertyFlags requiredFlags = 0, long minAlignment = 0);

    inline VulkanImage createManagedImage(VkImageCreateFlags createFlags, VkFormat imageFormat, VkExtent3D imageExtent,
                                          VkImageTiling imageTiling, VkImageUsageFlags imageUsage,
                                          VkImageLayout imageLayout, VmaAllocationCreateFlags allocationFlags,
                                          VmaMemoryUsage allocationUsage, VkMemoryPropertyFlags requiredFlags,
                                          bool mipmapped = false, ImageViewCreateInfo *imageViewCreateInfo = nullptr);

    VulkanBuffer createUnmanagedBuffer(VkDeviceSize bufferSize, VkBufferUsageFlags bufferUsage,
                                       VmaAllocationCreateFlags allocationFlags, VmaMemoryUsage allocationUsage,
                                       VkMemoryPropertyFlags requiredFlags = 0, long minAlignment = 0) const;

    inline VulkanImage createUnmanagedImage(VkImageCreateFlags createFlags, VkFormat imageFormat, VkExtent3D imageExtent,
                         VkImageTiling imageTiling, VkImageUsageFlags imageUsage, VkImageLayout imageLayout,
                         VmaAllocationCreateFlags allocationFlags, VmaMemoryUsage allocationUsage,
                         VkMemoryPropertyFlags requiredFlags, bool mipmapped = false,
                         ImageViewCreateInfo *imageViewCreateInfo = nullptr) const;

    // All textures are tracked.
    VulkanImage createTexture(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    VulkanImage createTexture(void *data, VkRenderer *renderer, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    VulkanImage createKtxCubemap(ktxTexture *texture, VkRenderer *renderer, VkFormat format);

    void mapBuffer(const VulkanBuffer &buffer, void **data);

    void unmapBuffer(const VulkanBuffer &buffer);

    void destroyBuffer(const VulkanBuffer &buffer);

    void destroyImage(const VulkanImage &vkImage);

    [[nodiscard]] VmaAllocator getAllocator() const {
        return allocator;
    }

private:
    VmaAllocator allocator;
    VkDevice device;
    VmaPool pool;

    bool isIntegratedGPU;

    std::unordered_set<VulkanBuffer> trackedBuffers;
    std::unordered_set<VulkanImage> trackedImages;
};

#endif
