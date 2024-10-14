#include "vk_memory.h"
#include <ranges>

#include "../../vk_renderer.h"

VkMemoryManager::VkMemoryManager(const VkInstance &instance, const VkPhysicalDevice &physicalDevice, const VkDevice &device, const bool customPool) : allocator(), device(device), pool(VK_NULL_HANDLE) {
    const VmaAllocatorCreateInfo vmaAllocatorCreateInfo{
        VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        physicalDevice,
        device,
        0,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        instance,
        VK_API_VERSION_1_3,
        VK_NULL_HANDLE
    };

    VK_CHECK(vmaCreateAllocator(&vmaAllocatorCreateInfo, &allocator));

    if (customPool) {
        constexpr VkBufferCreateInfo bufferCreateInfo{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            VK_NULL_HANDLE,
            {},
            1024,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_SHARING_MODE_EXCLUSIVE // TODO: Might need to change this
        };

        constexpr VmaAllocationCreateInfo allocationCreateInfo{
                {},
                VMA_MEMORY_USAGE_AUTO_PREFER_HOST
        };

        uint32_t memoryTypeIndex;
        VK_CHECK(vmaFindMemoryTypeIndexForBufferInfo(allocator, &bufferCreateInfo, &allocationCreateInfo, &memoryTypeIndex));

        const VmaPoolCreateInfo poolCreateInfo{memoryTypeIndex};

        VK_CHECK(vmaCreatePool(allocator, &poolCreateInfo, &pool));
    }
}

VkMemoryManager::~VkMemoryManager() {
    for (auto &[buffer, allocation] : trackedBuffers) {
        vmaDestroyBuffer(allocator, buffer, allocation);
    }

    for (auto &[image, imageView, allocation, extent, format] : trackedImages) {
        vmaDestroyImage(allocator, image, allocation);

        if (imageView)
            vkDestroyImageView(device, imageView, nullptr);
    }

    trackedBuffers.clear();
    trackedImages.clear();

    if (pool)
        vmaDestroyPool(allocator, pool);

    vmaDestroyAllocator(allocator);
}

void VkMemoryManager::immediateBuffer(const VkBufferCreateInfo *bufferCreateInfo, VmaAllocationCreateInfo *allocationCreateInfo, const std::function<void(VkBuffer &, void *)> &mappedMemoryTask, const std::function<void(VkBuffer &)> &unmappedMemoryTask) const {
    VkBuffer buffer;
    VmaAllocation allocation{};
    VmaAllocationInfo allocationInfo{};

    if (pool)
        allocationCreateInfo->pool = pool;

    vmaCreateBuffer(allocator, bufferCreateInfo, allocationCreateInfo, &buffer, &allocation, &allocationInfo);

    if (allocationCreateInfo->flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) {
        vmaMapMemory(allocator, allocation, &allocationInfo.pMappedData);
        mappedMemoryTask(buffer, allocationInfo.pMappedData);
        vmaUnmapMemory(allocator, allocation);
    } else {
        void *data;
        vmaMapMemory(allocator, allocation, &data);
        mappedMemoryTask(buffer, data);
        vmaUnmapMemory(allocator, allocation);
    }

    if (unmappedMemoryTask)
        unmappedMemoryTask(buffer);

    vmaDestroyBuffer(allocator, buffer, allocation);
}

VulkanBuffer VkMemoryManager::createManagedBuffer(const VkBufferCreateInfo *bufferCreateInfo, VmaAllocationCreateInfo *allocationCreateInfo, long minAlignment) {
    VkBuffer buffer;
    VmaAllocation allocation{};
    VmaAllocationInfo allocationInfo{};

    if (pool)
        allocationCreateInfo->pool = pool;

    vmaCreateBufferWithAlignment(allocator, bufferCreateInfo, allocationCreateInfo, minAlignment, &buffer, &allocation, &allocationInfo);

    trackedBuffers[buffer] = allocation;
    return {buffer, allocation};
}

VulkanImage VkMemoryManager::createManagedImage(const VkImageCreateInfo *imageCreateInfo, VmaAllocationCreateInfo *allocationCreateInfo, const ImageViewCreateInfo *imageViewCreateInfo) {
    VkImage image;
    VmaAllocation allocation{};
    VmaAllocationInfo allocationInfo{};

    if (pool)
        allocationCreateInfo->pool = pool;

    vmaCreateImage(allocator, imageCreateInfo, allocationCreateInfo, &image, &allocation, &allocationInfo);
    VulkanImage trackedImage{image, VK_NULL_HANDLE, allocation, imageCreateInfo->extent, imageCreateInfo->format};

    if (imageViewCreateInfo) {
        auto [flags, viewType, format, components, subresourceRange] = *imageViewCreateInfo;
        const VkImageViewCreateInfo createInfo{
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            VK_NULL_HANDLE,
            flags,
            image,
            viewType,
            format,
            components,
            subresourceRange
        };
        vkCreateImageView(device, &createInfo, nullptr, &trackedImage.imageView);
    }

    trackedImages.push_back(trackedImage);
    return trackedImage;
}

VulkanBuffer VkMemoryManager::createUnmanagedBuffer(const VkBufferCreateInfo *bufferCreateInfo, VmaAllocationCreateInfo *allocationCreateInfo, long minAlignment) const {
    VkBuffer buffer;
    VmaAllocation allocation{};
    VmaAllocationInfo allocationInfo{};

    if (pool)
        allocationCreateInfo->pool = pool;

    vmaCreateBufferWithAlignment(allocator, bufferCreateInfo, allocationCreateInfo, minAlignment, &buffer, &allocation, &allocationInfo);

    return {buffer, allocation};
}

VulkanImage VkMemoryManager::createUnmanagedImage(const VkImageCreateInfo *imageCreateInfo, VmaAllocationCreateInfo *allocationCreateInfo, const ImageViewCreateInfo *imageViewCreateInfo) const {
    VkImage image;
    VmaAllocation allocation{};
    VmaAllocationInfo allocationInfo{};

    if (pool)
        allocationCreateInfo->pool = pool;

    vmaCreateImage(allocator, imageCreateInfo, allocationCreateInfo, &image, &allocation, &allocationInfo);

    VulkanImage untrackedImage{image, VK_NULL_HANDLE, allocation, imageCreateInfo->extent, imageCreateInfo->format};

    if (imageViewCreateInfo) {
        auto [flags, viewType, format, components, subresourceRange] = *imageViewCreateInfo;
        const VkImageViewCreateInfo createInfo{
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            VK_NULL_HANDLE,
            flags,
            image,
            viewType,
            format,
            components,
            subresourceRange
        };
        vkCreateImageView(device, &createInfo, nullptr, &untrackedImage.imageView);
    }

    return untrackedImage;
}

VulkanImage VkMemoryManager::createTexture(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) {
    VkImageCreateInfo imageCreateInfo{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        VK_NULL_HANDLE,
        {},
        VK_IMAGE_TYPE_2D,
        format,
        size,
        mipmapped ? static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) : 1,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_TILING_OPTIMAL,
        usage,
        VK_SHARING_MODE_EXCLUSIVE
    };

    VmaAllocationCreateInfo allocationCreateInfo{
        {},
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    };

    ImageViewCreateInfo imageViewCreateInfo{
        0,
        VK_IMAGE_VIEW_TYPE_2D,
        format,
        {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, imageCreateInfo.mipLevels, 0, 1}
    };

    if (format == VK_FORMAT_D32_SFLOAT)
        imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    return createManagedImage(&imageCreateInfo, &allocationCreateInfo, &imageViewCreateInfo);
}

void generateMipmaps(const VkCommandBuffer &commandBuffer, VkImage &image, VkExtent2D size) {
    // TODO: Generate this with a compute shader later
    auto mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height))));
    for (int mip = 0; mip < mipLevels; mip++) {
        VkExtent2D halfSize = {std::max(1u, size.width >> 1), std::max(1u, size.height >> 1)};

        VkImageMemoryBarrier2 imageBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;

        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBarrier.subresourceRange.baseMipLevel = mip;
        imageBarrier.subresourceRange.levelCount = 1;
        imageBarrier.subresourceRange.layerCount = 1;
        imageBarrier.image = image;

        VkDependencyInfo dependencyInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &imageBarrier;

        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

        if (mip < mipLevels - 1) {
            VkImageBlit2 imageBlit{VK_STRUCTURE_TYPE_IMAGE_BLIT_2};

            imageBlit.srcOffsets[1].x = static_cast<int32_t>(size.width);
            imageBlit.srcOffsets[1].y = static_cast<int32_t>(size.height);
            imageBlit.srcOffsets[1].z = 1;

            imageBlit.dstOffsets[1].x = static_cast<int32_t>(halfSize.width);
            imageBlit.dstOffsets[1].y = static_cast<int32_t>(halfSize.height);
            imageBlit.dstOffsets[1].z = 1;

            imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageBlit.srcSubresource.mipLevel = mip;
            imageBlit.srcSubresource.baseArrayLayer = 0;
            imageBlit.srcSubresource.layerCount = 1;

            imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageBlit.dstSubresource.mipLevel = mip + 1;
            imageBlit.dstSubresource.baseArrayLayer = 0;
            imageBlit.dstSubresource.layerCount = 1;

            // Is this really needed?
            imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            imageBarrier.srcAccessMask = VK_ACCESS_2_NONE;
            imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;

            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

            imageBarrier.subresourceRange.baseMipLevel = mip + 1;

            vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

            VkBlitImageInfo2 blitInfo{VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2};
            blitInfo.srcImage = image;
            blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            blitInfo.dstImage = image;
            blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            blitInfo.regionCount = 1;
            blitInfo.pRegions = &imageBlit;
            blitInfo.filter = VK_FILTER_LINEAR;

            vkCmdBlitImage2(commandBuffer, &blitInfo);

            size = halfSize;
        }
    }

    TransitionImage(commandBuffer, image, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_MEMORY_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

VulkanImage VkMemoryManager::createTexture(void *data, VkRenderer *renderer, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) {
    VulkanImage newTexture{};

    const auto dataSize = size.width * size.height * size.depth * 4;
    const auto textureCreation = [&](auto &buffer, void *mappedMemory) {
        memcpy(mappedMemory, data, dataSize);

        newTexture = createTexture(size, format, usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, mipmapped);

        renderer->ImmediateSubmit([&](auto &commandBuffer) {
            // TODO: Find the right pipeline stages
            TransitionImage(commandBuffer, newTexture.image, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkBufferImageCopy copyRegion{
                0,
                0,
                0,
                { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                {0, 0, 0},
                size
            };

            vkCmdCopyBufferToImage(commandBuffer, buffer, newTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

            if (mipmapped) {
                generateMipmaps(commandBuffer, newTexture.image, {size.width, size.height});
            }else {
                TransitionImage(commandBuffer, newTexture.image, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        });
    };

    VkBufferCreateInfo bufferCreateInfo{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        VK_NULL_HANDLE,
        {},
        dataSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_SHARING_MODE_EXCLUSIVE
    };

    VmaAllocationCreateFlags allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    if (renderer->is_integrated_gpu()) {
        allocationFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VmaAllocationCreateInfo allocationCreateInfo{
        allocationFlags,
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    };

    immediateBuffer(&bufferCreateInfo, &allocationCreateInfo, textureCreation);
    return newTexture;
}

void VkMemoryManager::mapBuffer(const VkBuffer *buffer, void **data, const VmaAllocation *allocation) {
    if (!allocation) {
        allocation = &trackedBuffers[*buffer];
        if (!allocation) {
            throw std::runtime_error("Buffer not found in tracked buffers. You have to pass the allocation manually for unmanaged buffers.");
        }
    }

    VK_CHECK(vmaMapMemory(allocator, *allocation, data));
}

void VkMemoryManager::unmapBuffer(const VkBuffer *buffer, const VmaAllocation *allocation) {
    if (!allocation) {
        allocation = &trackedBuffers[*buffer];
        if (!allocation) {
            throw std::runtime_error("Buffer not found in tracked buffers. You have to pass the allocation manually for unmanaged buffers.");
        }
    }

    vmaUnmapMemory(allocator, *allocation);
}

void VkMemoryManager::destroyBuffer(const VkBuffer *buffer, const VmaAllocation *allocation) {
    if (!allocation) {
        allocation = &trackedBuffers[*buffer];
        if (!allocation) {
            throw std::runtime_error("Buffer not found in tracked buffers. You have to pass the allocation manually for unmanaged buffers.");
        }
    }

    vmaDestroyBuffer(allocator, *buffer, *allocation);
}
