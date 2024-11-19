
#ifndef D3D12_STUFF_VK_COMMON_H
#define D3D12_STUFF_VK_COMMON_H

#define GLM_FORCE_AVX2

#include <cstdio>
#include <vulkan/vulkan.h>
#include <memory>
#include <vk_mem_alloc.h>
#include <glm.hpp>

#define VK_CHECK(x) do { VkResult err = x; if (err) { printf(#x ", file: " __FILE__ ", line %d: %d\n", __LINE__, err); abort(); } } while (0)

struct Mesh {
    VkBuffer indexBuffer;
    VkBuffer vertexBuffer;
    VkDeviceAddress vertexBufferDeviceAddress;
};

struct MeshPushConstants {
    alignas(16) glm::mat4 worldMatrix;
    alignas(16) VkDeviceAddress vertexBufferDeviceAddress;
};

struct MeshShaderPushConstants {
    glm::mat4 mvp;
};

struct FragmentPushConstants {
    alignas(16) glm::vec3 cameraPosition;
    alignas(16) glm::ivec2 viewportSize;
    alignas(16) glm::vec4 cascadeSplits;
};

struct VulkanImage {
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D extent;
    VkFormat format;
    VkSampler sampler;

    bool operator==(const VulkanImage &other) const {
        return image == other.image;
    }
};

struct VulkanBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;

    bool operator==(const VulkanBuffer &other) const {
        return buffer == other.buffer;
    }
};

template<>
struct std::hash<VulkanImage> {
    std::size_t operator()(const VulkanImage &image) const noexcept {
        return std::hash<VkImage>{}(image.image);
    }
};

template<>
struct std::hash<VulkanBuffer> {
    std::size_t operator()(const VulkanBuffer &buffer) const noexcept {
        return std::hash<VkBuffer>{}(buffer.buffer);
    }
};

struct VkVertex {
    alignas(16) glm::vec3 pos;
    alignas(16) glm::vec3 normal;
    alignas(16) glm::vec4 color;
    alignas(16) glm::vec2 uv;
};

inline void TransitionImage(VkCommandBuffer commandBuffer, VulkanImage image, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags srcAccessMask, VkPipelineStageFlags dstStageMask, VkPipelineStageFlags dstAccessMask, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels = -1, uint32_t layerCount = -1) {
    const auto aspectMask = image.format == VK_FORMAT_D16_UNORM ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageMemoryBarrier2 imageBarrier{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        VK_NULL_HANDLE,
        srcStageMask,
        srcAccessMask,
        dstStageMask,
        dstAccessMask,
        oldLayout,
        newLayout,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        image.image,
        {
            static_cast<VkImageAspectFlags>(aspectMask),
            0,
            mipLevels == -1 ? VK_REMAINING_MIP_LEVELS : mipLevels,
            0,
            layerCount == -1 ? VK_REMAINING_ARRAY_LAYERS : layerCount
        }
    };

    VkDependencyInfo dependencyInfo{
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        VK_NULL_HANDLE,
        0,
        0,
        VK_NULL_HANDLE,
        0,
        VK_NULL_HANDLE,
        1,
        &imageBarrier
    };

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

inline void BlitImage(const VkCommandBuffer &commandBuffer, const VulkanImage &srcImage, const VulkanImage &dstImage, VkImageLayout srcLayout, VkImageLayout dstLayout, VkImageAspectFlags aspectFlags) {
    VkImageBlit2 blitRegion{VK_STRUCTURE_TYPE_IMAGE_BLIT_2};

    blitRegion.srcOffsets[1].x = static_cast<int32_t>(srcImage.extent.width);
    blitRegion.srcOffsets[1].y = static_cast<int32_t>(srcImage.extent.height);
    blitRegion.srcOffsets[1].z = 1;

    blitRegion.dstOffsets[1].x = static_cast<int32_t>(dstImage.extent.width);
    blitRegion.dstOffsets[1].y = static_cast<int32_t>(dstImage.extent.height);
    blitRegion.dstOffsets[1].z = 1;

    blitRegion.srcSubresource.aspectMask = aspectFlags;
    blitRegion.srcSubresource.mipLevel = 0;
    blitRegion.srcSubresource.baseArrayLayer = 0;
    blitRegion.srcSubresource.layerCount = 1;

    blitRegion.dstSubresource.aspectMask = aspectFlags;
    blitRegion.dstSubresource.mipLevel = 0;
    blitRegion.dstSubresource.baseArrayLayer = 0;
    blitRegion.dstSubresource.layerCount = 1;

    const VkBlitImageInfo2 blitInfo{
        VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
        VK_NULL_HANDLE,
        srcImage.image,
        srcLayout,
        dstImage.image,
        dstLayout,
        1,
        &blitRegion,
        srcImage.format == VK_FORMAT_D16_UNORM ? VK_FILTER_NEAREST : VK_FILTER_LINEAR
    };

    vkCmdBlitImage2(commandBuffer, &blitInfo);
}

#endif
