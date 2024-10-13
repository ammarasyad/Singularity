
#ifndef D3D12_STUFF_VK_COMMON_H
#define D3D12_STUFF_VK_COMMON_H

#include <cassert>
#include <vulkan/vulkan.hpp>
#include "../../third_party/VulkanMemoryAllocator/include/vk_mem_alloc.h"
#include <../../third_party/glm/glm.hpp>

#define VK_CHECK(x) do { VkResult err = x; if (err) { printf("Detected Vulkan error: %d\n", err); abort(); } } while (0)

struct Mesh {
    VkBuffer indexBuffer;
    VkBuffer vertexBuffer;
    VkDeviceAddress vertexBufferDeviceAddress;
};

struct MeshPushConstants {
    glm::mat4 worldMatrix;
    VkDeviceAddress vertexBufferDeviceAddress;
};

struct VulkanImage {
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D extent;
    VkFormat format;
};

struct VkVertex {
    alignas(16) glm::vec3 pos;
    alignas(16) glm::vec3 normal;
    alignas(16) glm::vec3 color;
    float uv_X;
    float uv_Y;

    static std::array<VkVertexInputAttributeDescription, 1> getVertexInputAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 1> attributeDescriptions{};

        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(VkVertex, pos);

        // attributeDescriptions[1].location = 1;
        // attributeDescriptions[1].binding = 0;
        // attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        // attributeDescriptions[1].offset = offsetof(VkVertex, color);

        return attributeDescriptions;
    }

    static VkVertexInputBindingDescription getVertexInputBindingDescription() {
        constexpr VkVertexInputBindingDescription bindingDescription{
            0,
            sizeof(VkVertex),
            VK_VERTEX_INPUT_RATE_VERTEX
        };

        return bindingDescription;
    }
};

inline void TransitionImage(VkCommandBuffer commandBuffer, VkImage image, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags srcAccessMask, VkPipelineStageFlags dstStageMask, VkPipelineStageFlags dstAccessMask, VkImageLayout oldLayout, VkImageLayout newLayout) {
    const auto aspectMask = newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL || newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
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
        image,
        {
            static_cast<VkImageAspectFlags>(aspectMask),
            0,
            VK_REMAINING_MIP_LEVELS,
            0,
            VK_REMAINING_ARRAY_LAYERS
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

#endif
