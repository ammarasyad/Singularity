#include "vk_memory.h"
#include <ranges>
#include <thread>
#include <iostream>

#include "../../vk_renderer.h"

VkMemoryManager::VkMemoryManager(const VkInstance &instance, const VkPhysicalDevice &physicalDevice, const VkDevice &device, const bool isIntegratedGPU, const bool customPool) : allocator(), device(device), isIntegratedGPU(isIntegratedGPU), pool(VK_NULL_HANDLE) {
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
    for (const auto &vkBuffer : trackedBuffers) {
        vmaDestroyBuffer(allocator, vkBuffer.buffer, vkBuffer.allocation);
    }

    for (const auto &vkImage : trackedImages) {
        vmaDestroyImage(allocator, vkImage.image, vkImage.allocation);

        if (vkImage.imageView)
            vkDestroyImageView(device, vkImage.imageView, nullptr);

        if (vkImage.sampler)
            vkDestroySampler(device, vkImage.sampler, nullptr);
    }

    trackedBuffers.clear();
    trackedImages.clear();

    if (pool)
        vmaDestroyPool(allocator, pool);

    vmaDestroyAllocator(allocator);
}

void VkMemoryManager::stagingBuffer(VkDeviceSize bufferSize, const std::function<void(VkBuffer &, void *)> &mappedMemoryTask, const std::function<void(VkBuffer &)> &unmappedMemoryTask) const {
    VmaAllocationCreateFlags allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    if (isIntegratedGPU)
        allocationFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer;
    VmaAllocation allocation{};
    VmaAllocationInfo allocationInfo{};

    VkBufferCreateInfo bufferCreateInfo{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            VK_NULL_HANDLE,
            0,
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_SHARING_MODE_EXCLUSIVE // maybe change this to concurrent for separate graphics and present queues
    };

    VmaAllocationCreateInfo allocationCreateInfo{
            allocationFlags,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    };

    if (pool)
        allocationCreateInfo.pool = pool;

    VK_CHECK(vmaCreateBuffer(allocator, &bufferCreateInfo, &allocationCreateInfo, &stagingBuffer, &allocation, &allocationInfo));

    if (isIntegratedGPU) {
        VK_CHECK(vmaMapMemory(allocator, allocation, &allocationInfo.pMappedData));
        mappedMemoryTask(stagingBuffer, allocationInfo.pMappedData);
        vmaUnmapMemory(allocator, allocation);
    } else {
        void *data;
        VK_CHECK(vmaMapMemory(allocator, allocation, &data));
        mappedMemoryTask(stagingBuffer, data);
        vmaUnmapMemory(allocator, allocation);
    }

    if (unmappedMemoryTask)
        unmappedMemoryTask(stagingBuffer);

    vmaDestroyBuffer(allocator, stagingBuffer, allocation);
}

VulkanBuffer VkMemoryManager::createManagedBuffer(const VulkanBufferCreateInfo &info) {
    VulkanBuffer trackedBuffer = createUnmanagedBuffer(info);
    trackedBuffers.insert(trackedBuffer);
    return trackedBuffer;
}

VulkanImage VkMemoryManager::createManagedImage(const VulkanImageCreateInfo &info) {
    VulkanImage trackedImage = createUnmanagedImage(info);
    trackedImages.insert(trackedImage);
    return trackedImage;
}

VulkanBuffer VkMemoryManager::createUnmanagedBuffer(const VulkanBufferCreateInfo &info) const {
    auto [bufferSize, bufferUsage, allocationFlags, allocationUsage, requiredFlags, minAlignment] = info;

    VkBuffer buffer;
    VmaAllocation allocation{};
    VmaAllocationInfo allocationInfo{};

    VkBufferCreateInfo bufferCreateInfo{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            VK_NULL_HANDLE,
            0,
            bufferSize,
            bufferUsage,
            VK_SHARING_MODE_EXCLUSIVE // maybe change this to concurrent for separate graphics and present queues
    };

    if (isIntegratedGPU && (allocationFlags & VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT))
        allocationFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationCreateInfo allocationCreateInfo{
            allocationFlags,
            allocationUsage,
            requiredFlags
    };

    if (pool)
        allocationCreateInfo.pool = pool;

    VK_CHECK(vmaCreateBuffer(allocator, &bufferCreateInfo, &allocationCreateInfo, &buffer, &allocation, &allocationInfo));

    VulkanBuffer trackedBuffer{buffer, allocation};
    return trackedBuffer;
}

VulkanImage VkMemoryManager::createUnmanagedImage(const VulkanImageCreateInfo &info) const {
    auto &[createFlags, imageFormat, imageExtent, imageTiling, imageUsage, imageLayout, allocationFlags, allocationUsage, requiredFlags, mipmapped, imageViewCreateInfo] = info;

    VkImage image;
    VmaAllocation allocation{};
    VmaAllocationInfo allocationInfo{};

    VkImageCreateInfo imageCreateInfo{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        VK_NULL_HANDLE,
        createFlags,
        VK_IMAGE_TYPE_2D,
        imageFormat,
        imageExtent,
        mipmapped ? static_cast<uint32_t>(std::floor(std::log2(std::max(imageExtent.width, imageExtent.height)))) : 1,
        1,
        VK_SAMPLE_COUNT_1_BIT, // might need to take msaaSamples from VkRenderer
        imageTiling,
        imageUsage,
        VK_SHARING_MODE_EXCLUSIVE, // may change
        0,
        VK_NULL_HANDLE,
        imageLayout
    };

    VmaAllocationCreateInfo allocationCreateInfo{
        allocationFlags,
        allocationUsage,
        requiredFlags
    };

    if (pool)
        allocationCreateInfo.pool = pool;

    VK_CHECK(vmaCreateImage(allocator, &imageCreateInfo, &allocationCreateInfo, &image, &allocation, &allocationInfo));

    VulkanImage untrackedImage{image, VK_NULL_HANDLE, allocation, imageCreateInfo.extent, imageCreateInfo.format};

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
    ImageViewCreateInfo imageViewCreateInfo{
        0,
        VK_IMAGE_VIEW_TYPE_2D,
        format,
        {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipmapped ? static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) : 1, 0, 1}
    };

    if (format == VK_FORMAT_D32_SFLOAT)
        imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    return createManagedImage({0, format, size, VK_IMAGE_TILING_OPTIMAL, usage, VK_IMAGE_LAYOUT_UNDEFINED, 0,
                               VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mipmapped,
                               &imageViewCreateInfo});
}

void generateMipmaps(const VkCommandBuffer &commandBuffer, VulkanImage &image, VkExtent2D size) {
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
        imageBarrier.image = image.image;

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
            blitInfo.srcImage = image.image;
            blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            blitInfo.dstImage = image.image;
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
            TransitionImage(commandBuffer, newTexture, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

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
                generateMipmaps(commandBuffer, newTexture, {size.width, size.height});
            } else {
                TransitionImage(commandBuffer, newTexture, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        });
    };

    stagingBuffer(dataSize, textureCreation);
    return newTexture;
}

std::vector<VulkanImage> VkMemoryManager::createTexturesMultithreaded(const std::vector<LoadedImage> &loadedImages, VkRenderer *renderer) {
//    std::vector<VkCommandBuffer> tempCommandBuffers(std::thread::hardware_concurrency());
    uint64_t maxImageSize = 0;
    for (const auto &loadedImage : loadedImages) {
        maxImageSize = std::max(maxImageSize, static_cast<uint64_t>(loadedImage.size.width * loadedImage.size.height * 4));
    }

    auto imageBuffer = createUnmanagedBuffer({
         maxImageSize,
         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
         VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
         VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    });

    uint8_t *mappedData;
    mapBuffer(imageBuffer, reinterpret_cast<void **>(&mappedData));

    std::vector<VulkanImage> images;
    images.reserve(loadedImages.size());

//    for (size_t i = 0; i < tempCommandBuffers.size(); i++) {
//        const VkCommandBufferAllocateInfo allocateInfo{
//            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
//            VK_NULL_HANDLE,
//            renderer->threaded_command_pools()[i],
//            VK_COMMAND_BUFFER_LEVEL_PRIMARY,
//            1
//        };
//
//        VK_CHECK(vkAllocateCommandBuffers(device, &allocateInfo, &tempCommandBuffers[i]));
//
//        constexpr VkCommandBufferBeginInfo beginInfo{
//                VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
//                VK_NULL_HANDLE,
//                VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
//        };
//
//        VK_CHECK(vkBeginCommandBuffer(tempCommandBuffers[i], &beginInfo));
//    }

    for (const auto &loadedImage : loadedImages) {
        auto texture = createTexture(loadedImage.size, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, true);
        auto size = loadedImage.size;

        memcpy(mappedData, loadedImage.data, size.width * size.height * 4);

        renderer->ImmediateSubmit([&](auto &commandBuffer){
            TransitionImage(commandBuffer, texture, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
                            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkBufferImageCopy copyRegion{
                    0,
                    0,
                    0,
                    {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                    {0, 0, 0},
                    size
            };

            vkCmdCopyBufferToImage(commandBuffer, imageBuffer.buffer, texture.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

            generateMipmaps(commandBuffer, texture, {size.width, size.height});
        });

        images.emplace_back(texture);
    }

//#pragma omp parallel for ordered shared(renderer, loadedImages, imageBuffer, images, tempCommandBuffers, mappedData) default(none) num_threads(std::thread::hardware_concurrency())
//    for (uint32_t i = 0; i < loadedImages.size(); i++) {
//        const auto &loadedImage = loadedImages[i];
//        auto texture = createTexture(loadedImage.size, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, true);
//        auto size = loadedImage.size;
//        auto &commandBuffer = tempCommandBuffers[omp_get_thread_num()];
//
//        constexpr VkCommandBufferBeginInfo beginInfo{
//                VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
//                VK_NULL_HANDLE,
//                VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
//        };
//
//#pragma omp ordered
//        {
//            printf("Thread %d processing image %d\n", omp_get_thread_num(), i);
//            VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
//
//            TransitionImage(commandBuffer, texture, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
//                            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
//                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
//
//            VkBufferImageCopy copyRegion{
//                    0,
//                    0,
//                    0,
//                    {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
//                    {0, 0, 0},
//                    size
//            };
//
////#pragma omp critical
////        {
//            memcpy(mappedData, loadedImage.data, size.width * size.height * 4);
////        }
//
//            vkCmdCopyBufferToImage(commandBuffer, imageBuffer.buffer, texture.image,
//                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
//            generateMipmaps(commandBuffer, texture, {size.width, size.height});
//
//            VK_CHECK(vkEndCommandBuffer(commandBuffer));
//
////#pragma omp ordered
////        {
//            const VkSubmitInfo submitInfo{
//                    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
//                    .pNext = VK_NULL_HANDLE,
//                    .commandBufferCount = 1,
//                    .pCommandBuffers = &commandBuffer
//            };
//
//            VK_CHECK(vkQueueSubmit(renderer->graphics_queue(), 1, &submitInfo, VK_NULL_HANDLE));
//            VK_CHECK(vkQueueWaitIdle(renderer->graphics_queue()));
//
//            images.emplace_back(texture);
//        }
////        }
//    }

//    for (const auto &commandBuffer : tempCommandBuffers) {
//        VK_CHECK(vkEndCommandBuffer(commandBuffer));
//    }

//    renderer->SubmitMultithreadedCommandBuffers(tempCommandBuffers);
//    tempCommandBuffers.clear();

    unmapBuffer(imageBuffer);
    destroyBuffer(imageBuffer, false);

    return images;
}

VulkanImage VkMemoryManager::createKtxCubemap(ktxTexture *texture, VkRenderer *renderer, VkFormat format) {
    VkExtent3D imageSize{
            texture->baseWidth,
            texture->baseHeight,
            1
    };

    auto mipLevels = texture->numLevels;

    VkImageCreateInfo imageCreateInfo{
            VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            VK_NULL_HANDLE,
            VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
            VK_IMAGE_TYPE_2D,
            format,
            imageSize,
            mipLevels,
            6,
            VK_SAMPLE_COUNT_1_BIT, // might need to take msaaSamples from VkRenderer
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_SHARING_MODE_EXCLUSIVE, // may change
            0,
            VK_NULL_HANDLE,
            VK_IMAGE_LAYOUT_UNDEFINED
    };

    VmaAllocationCreateInfo allocationCreateInfo{
            0,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    };

    if (pool)
        allocationCreateInfo.pool = pool;

    VkImage image;
    VmaAllocation allocation{};
    VmaAllocationInfo allocationInfo{};

    vmaCreateImage(allocator, &imageCreateInfo, &allocationCreateInfo, &image, &allocation, &allocationInfo);

    auto data = ktxTexture_GetData(texture);
    auto bufferSize = ktxTexture_GetDataSize(texture);

    stagingBuffer(bufferSize, [&](auto &stagingBuffer, auto mappedMemory) {
        memcpy(mappedMemory, data, bufferSize);
    }, [&](auto &stagingBuffer) {
        renderer->ImmediateSubmit([&](auto &commandBuffer) {
            std::vector<VkBufferImageCopy> copyRegions;
            for (uint32_t face = 0; face < 6; face++) {
                for (uint32_t level = 0; level < mipLevels; level++) {
                    ktx_size_t offset;
                    auto result = ktxTexture_GetImageOffset(texture, level, 0, face, &offset);
                    assert(result == KTX_SUCCESS);
                    copyRegions.push_back({
                        offset,
                        0,
                        0,
                        { VK_IMAGE_ASPECT_COLOR_BIT, level, face, 1 },
                        {0, 0, 0},
                        {std::max(1u, imageSize.width >> level), std::max(1u, imageSize.height >> level), 1}
                    });
                }
            }

            TransitionImage(commandBuffer, {image, VK_NULL_HANDLE, allocation, imageSize, format}, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels, 6);

            vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copyRegions.size(), copyRegions.data());

            TransitionImage(commandBuffer, {image, VK_NULL_HANDLE, allocation, imageSize, format}, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels, 6);
        });
    });

    VulkanImage trackedImage{image, VK_NULL_HANDLE, allocation, imageSize, format};

    const VkImageViewCreateInfo createInfo{
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            VK_NULL_HANDLE,
            0,
            image,
            VK_IMAGE_VIEW_TYPE_CUBE,
            format,
            {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 6}
    };

    vkCreateImageView(device, &createInfo, nullptr, &trackedImage.imageView);

    VkSamplerCreateInfo samplerCreateInfo{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .mipLodBias = 0.0f,
            .anisotropyEnable = VK_TRUE,
            .maxAnisotropy = renderer->device_properties().limits.maxSamplerAnisotropy,
            .compareOp = VK_COMPARE_OP_NEVER,
            .minLod = 0.0f,
            .maxLod = static_cast<float>(mipLevels),
            .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
    };

    VK_CHECK(vkCreateSampler(device, &samplerCreateInfo, VK_NULL_HANDLE, &trackedImage.sampler));

    trackedImages.insert(trackedImage);
    return trackedImage;
}

void VkMemoryManager::mapBuffer(const VulkanBuffer &buffer, void **data) {
    VK_CHECK(vmaMapMemory(allocator, buffer.allocation, data));
}

void VkMemoryManager::mapImage(const VulkanImage &image, void **data) {
    VK_CHECK(vmaMapMemory(allocator, image.allocation, data));
}

void VkMemoryManager::unmapBuffer(const VulkanBuffer &buffer) {
    vmaUnmapMemory(allocator, buffer.allocation);
}

void VkMemoryManager::unmapImage(const VulkanImage &image) {
    vmaUnmapMemory(allocator, image.allocation);
}

void VkMemoryManager::destroyBuffer(const VulkanBuffer &buffer, const bool tracked) {
    vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);

    if (tracked)
        trackedBuffers.erase(buffer);
}

void VkMemoryManager::destroyImage(const VulkanImage &image, const bool tracked) {
    vmaDestroyImage(allocator, image.image, image.allocation);
    if (image.imageView)
        vkDestroyImageView(device, image.imageView, nullptr);

    if (image.sampler)
        vkDestroySampler(device, image.sampler, nullptr);

    if (tracked)
        trackedImages.erase(image);
}
