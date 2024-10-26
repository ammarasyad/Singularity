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

struct VulkanBufferCreateInfo {
    VkDeviceSize bufferSize = 0;
    VkBufferUsageFlags bufferUsage = 0;

    VmaAllocationCreateFlags allocationFlags = 0;
    VmaMemoryUsage allocationUsage = VMA_MEMORY_USAGE_AUTO;
    VkMemoryPropertyFlags requiredFlags = 0;

    long minAlignment = 0;
};

struct VulkanImageCreateInfo {
    VkImageCreateFlags createFlags = 0;
    VkFormat imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent3D imageExtent = {0, 0, 0};
    VkImageTiling imageTiling = VK_IMAGE_TILING_OPTIMAL;
    VkImageUsageFlags imageUsage = 0;
    VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateFlags allocationFlags = 0;
    VmaMemoryUsage allocationUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VkMemoryPropertyFlags requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    bool mipmapped = false;
    ImageViewCreateInfo *imageViewCreateInfo = nullptr;
};

class VkMemoryManager {
public:
    VkMemoryManager(const VkInstance &, const VkPhysicalDevice &, const VkDevice &, bool, bool customPool = false);

    ~VkMemoryManager();

    void stagingBuffer(VkDeviceSize bufferSize, const std::function<void(VkBuffer &, void *)> &mappedMemoryTask,
                       const std::function<void(VkBuffer &)> &unmappedMemoryTask = nullptr) const;

    VulkanBuffer createManagedBuffer(const VulkanBufferCreateInfo &info);

    VulkanImage createManagedImage(const VulkanImageCreateInfo &info);

    VulkanBuffer createUnmanagedBuffer(const VulkanBufferCreateInfo &info) const;

    VulkanImage createUnmanagedImage(const VulkanImageCreateInfo &info) const;

    // All textures are tracked.
    VulkanImage createTexture(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    VulkanImage createTexture(void *data, VkRenderer *renderer, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    VulkanImage createKtxCubemap(ktxTexture *texture, VkRenderer *renderer, VkFormat format);

    void mapBuffer(const VulkanBuffer &buffer, void **data);
    void mapImage(const VulkanImage &vkImage, void **data);

    void unmapBuffer(const VulkanBuffer &buffer);
    void unmapImage(const VulkanImage &vkImage);

    void destroyBuffer(const VulkanBuffer &buffer, const bool tracked = true);

    void destroyImage(const VulkanImage &vkImage, const bool tracked = true);

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
