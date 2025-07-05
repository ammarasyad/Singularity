#ifndef D3D12_STUFF_VK_MEMORY_H
#define D3D12_STUFF_VK_MEMORY_H

#include "graphics/vk/vk_common.h"
#include <functional>
#include <unordered_set>
#include <ktx.h>
#include <atomic>
#include <d3d12.h>

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

struct alignas(64) LoadedImage {
    inline static std::atomic_uint64_t totalBytesSize{0};
    VkExtent3D size;
    uint32_t index;
    uint8_t *data;
};

struct VulkanExternalImage
{
    VkImage image;
    VkDeviceMemory memory;
};

class VkMemoryManager {
public:
    // explicit VkMemoryManager(const VkRenderer *, bool customPool = false);
    VkMemoryManager() = default;

    void Initialize(const VkRenderer *, bool customPool = false);
    void Shutdown();

    // void stagingBuffer(VkDeviceSize bufferSize, const std::function<void(VkBuffer &, void *)> &&mappedMemoryTask, const std::function<void(VkBuffer &)> &&unmappedMemoryTask = nullptr) const;

    void useStagingBuffer(const std::function<void(void *)> &&mappedMemoryTask, const std::function<void(VkBuffer)> &&unmappedMemoryTask = nullptr) const;

    VulkanBuffer createManagedBuffer(const VulkanBufferCreateInfo &info);

    VulkanImage createManagedImage(const VulkanImageCreateInfo &info);

    VulkanBuffer createUnmanagedBuffer(const VulkanBufferCreateInfo &info);

    VulkanImage createUnmanagedImage(const VulkanImageCreateInfo &info);

#ifdef _WIN32
    VulkanExternalImage createExternalImage(const VkImageCreateInfo &imageCreateInfo, VkPhysicalDeviceMemoryProperties &properties, ID3D12Device *d3d12Device, ID3D12Resource *deviceHandle);
    void destroyExternalImageMemory(const VkImage &image, const VkDeviceMemory &memory);
#endif

    // All textures are tracked.
    VulkanImage createTexture(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    VulkanImage createTexture(const void *data, VkRenderer *renderer, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    std::vector<VulkanImage> createTexturesMultithreaded(const std::vector<LoadedImage> &loadedImages, const VkRenderer *renderer);
    VulkanImage createKtxCubemap(ktxTexture *texture, VkRenderer *renderer, VkFormat format);

    void copyToBuffer(const VulkanBuffer &buffer, const void *data, VkDeviceSize size, VkDeviceSize offset = 0) const;

    void mapBuffer(const VulkanBuffer &buffer, void **data) const;
    void mapImage(const VulkanImage &vkImage, void **data) const;

    void unmapBuffer(const VulkanBuffer &buffer) const;
    void unmapImage(const VulkanImage &vkImage) const;

    void destroyBuffer(const VulkanBuffer &buffer, bool tracked = true);
    void destroyImage(const VulkanImage &vkImage, bool tracked = true);
private:
    static VmaVirtualBlock createVirtualBuffer(VkDeviceSize size) ;
    static inline void destroyVirtualBuffer(const VmaVirtualBlock &block);
    VmaAllocator allocator;
    VkDevice device;
    VmaPool pool{VK_NULL_HANDLE};
    VkDeviceSize availableMemory;
    VkDeviceSize totalMemory;

    VulkanBuffer stagingBuffer;
    void *mappedStagingBuffer;

    bool isIntegratedGPU;

    std::unordered_set<VulkanBuffer> trackedBuffers;
    std::unordered_set<VulkanImage> trackedImages;
};

#endif
