#include <iostream>
#include <set>
#include <algorithm>
#include <chrono>
#include "vk_renderer.h"

#include <imgui_impl_vulkan.h>
#include <gtc/matrix_transform.hpp>
#include <gtx/transform.inl>
#include "vk/memory/vk_mesh_assets.h"

#include "file.h"
#include "vk/vk_gui.h"

VkRenderer::VkRenderer(GLFWwindow *window, const bool dynamicRendering, const bool asyncCompute)
    : dynamicRendering(dynamicRendering),
      asyncCompute(asyncCompute),
#ifndef _NDEBUG
      debugMessenger(VK_NULL_HANDLE),
#endif
      glfwWindow(window),
      viewport(),
      scissor(),
      instance(VK_NULL_HANDLE),
      surface(VK_NULL_HANDLE),
      physicalDevice(VK_NULL_HANDLE),
      device(VK_NULL_HANDLE),
      queryPool(VK_NULL_HANDLE),
      pipelineCache(VK_NULL_HANDLE),
      graphicsQueue(VK_NULL_HANDLE),
      computeQueue(VK_NULL_HANDLE),
      presentQueue(VK_NULL_HANDLE),
      surfaceFormat(),
      swapChain(VK_NULL_HANDLE),
      renderPass(VK_NULL_HANDLE),
      mainDescriptorSet(VK_NULL_HANDLE),
      mainDescriptorSetLayout(VK_NULL_HANDLE),
      pipelineLayout(VK_NULL_HANDLE),
      graphicsPipeline(VK_NULL_HANDLE),
      computePipeline(VK_NULL_HANDLE),
      frames(),
      immediateCommandPool(VK_NULL_HANDLE),
      presentMode(),
      swapChainExtent(),
      deviceProperties(),
      memoryProperties(),
      textureSamplerLinear(),
      textureSamplerNearest(),
      sceneDescriptorSetLayout(VK_NULL_HANDLE)
{
    frames[0].frameCallbacks.reserve(MAX_FRAMES_IN_FLIGHT);
    frames[1].frameCallbacks.reserve(MAX_FRAMES_IN_FLIGHT);

    glfwSetWindowUserPointer(window, this);
    InitializeInstance();

#ifndef _NDEBUG
    constexpr VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        VK_NULL_HANDLE,
        0,
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        DebugCallback,
        VK_NULL_HANDLE
    };

    VK_CHECK(CreateDebugUtilsMessengerEXT(instance, &debugCreateInfo, VK_NULL_HANDLE, &debugMessenger));
#endif

    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateQueryPool();

    memoryManager = std::make_shared<VkMemoryManager>(instance, physicalDevice, device);

    // Reuse pipeline cache
    const auto pipelineCacheData = ReadFile<char>("pipeline_cache.bin");
    if (!pipelineCacheData.empty()) {
        VkPipelineCacheCreateInfo pipelineCacheCreateInfo{
            VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
            VK_NULL_HANDLE,
            0,
            pipelineCacheData.size(),
            reinterpret_cast<const uint8_t *>(pipelineCacheData.data())
        };

        VK_CHECK(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, VK_NULL_HANDLE, &pipelineCache));
    } else {
        CreatePipelineCache();
    }

    CreateSwapChain();

    viewport = {
        0.0f,
        0.0f,
        static_cast<float>(swapChainExtent.width),
        static_cast<float>(swapChainExtent.height),
        0.0f,
        1.0f
    };

    scissor = {
        {0, 0},
        swapChainExtent
    };

    if (!dynamicRendering)
        CreateRenderPass();

    CreateDescriptors();
    CreatePipelineLayout();
    CreateGraphicsPipeline();
    CreateComputePipeline();

    if (!dynamicRendering)
        CreateFramebuffers();

    CreateCommandPool();

    meshAssets = LoadGltfMeshes(this, "../assets/BoxVertexColors.glb").value();

    CreateCommandBuffers();
    CreateDefaultTexture();
    CreateSyncObjects();

    isVkRunning = true;
}

VkRenderer::~VkRenderer() {
    Shutdown();
}

void VkRenderer::FramebufferResizeCallback(GLFWwindow *window, int, int) {
    const auto app = static_cast<VkRenderer *>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
}

void VkRenderer::InitializeInstance() {
    VkApplicationInfo appInfo{
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        VK_NULL_HANDLE,
        "D3D12 Stuff",
        VK_MAKE_VERSION(1, 0, 0),
        "D3D12 Stuff",
        VK_MAKE_VERSION(1, 0, 0),
        VK_API_VERSION_1_3
    };

    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
#ifndef _NDEBUG
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    bool layerFound = false;
    for (const char *validationLayerName: validationLayers) {
        for (const auto &[layerName, specVersion, implementationVersion, description]: availableLayers) {
            if (strcmp(validationLayerName, layerName) == 0) {
                layerFound = true;
                break;
            }
        }
    }

    if (!layerFound) {
        throw std::runtime_error("validation layers requested, but not available!");
    }

    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

#ifndef _NDEBUG
    auto validationSize = static_cast<uint32_t>(validationLayers.size());
    auto validationData = validationLayers.data();
#else
    auto validationSize = 0;
    auto validationData = VK_NULL_HANDLE;
#endif


    const VkInstanceCreateInfo createInfo{
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        &appInfo,
        validationSize,
        validationData,
        static_cast<uint32_t>(extensions.size()),
        extensions.data()
    };

    VK_CHECK(vkCreateInstance(&createInfo, VK_NULL_HANDLE, &instance));
    glfwCreateWindowSurface(instance, glfwWindow, nullptr, &surface);
}

void VkRenderer::Render(EngineStats &stats) {
    stats.drawCallCount = 0;
    stats.triangleCount = 0;

    vkWaitForFences(device, 1, &frames[currentFrame].inFlightFence, VK_TRUE, UINT64_MAX);

    for (auto &callback : frames[currentFrame].frameCallbacks)
        callback();

    frames[currentFrame].frameCallbacks.clear();
    frames[currentFrame].frameDescriptors.ClearPools(device);

    uint32_t imageIndex;
    auto result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, frames[currentFrame].imageAvailableSemaphore,
                                        VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        framebufferResized = true;
        return;
    }

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swap chain image!");
    }

    vkResetFences(device, 1, &frames[currentFrame].inFlightFence);
    vkResetCommandBuffer(frames[currentFrame].commandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

    const auto start = std::chrono::high_resolution_clock::now();

    Draw(frames[currentFrame].commandBuffer, imageIndex, stats);

    const auto end = std::chrono::high_resolution_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    stats.meshDrawTime = static_cast<float>(elapsed) / 1000.f;

    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submitInfo{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        VK_NULL_HANDLE,
        1,
        &frames[currentFrame].imageAvailableSemaphore,
        waitStages,
        1,
        &frames[currentFrame].commandBuffer,
        1,
        &frames[currentFrame].renderFinishedSemaphore
    };

    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, frames[currentFrame].inFlightFence));

    VkPresentInfoKHR presentInfo{
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        VK_NULL_HANDLE,
        1,
        &frames[currentFrame].renderFinishedSemaphore,
        1,
        &swapChain,
        &imageIndex
    };

    result = vkQueuePresentKHR(presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
        framebufferResized = false;
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to acquire swap chain image!");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VkRenderer::Draw(const VkCommandBuffer &commandBuffer, uint32_t imageIndex, EngineStats &stats) {
    VkCommandBufferBeginInfo beginInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        VK_NULL_HANDLE,
        0,
        VK_NULL_HANDLE
    };

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    if (dynamicRendering) {
        VkImageMemoryBarrier2 barrier{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            VK_NULL_HANDLE,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            drawImage.image,
            {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0,
                VK_REMAINING_MIP_LEVELS,
                0,
                VK_REMAINING_ARRAY_LAYERS
            }
        };

        VkDependencyInfo dependencyInfo{
            VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            VK_NULL_HANDLE,
            {},
            0,
            VK_NULL_HANDLE,
            0,
            VK_NULL_HANDLE,
            1,
            &barrier
        };

        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &mainDescriptorSet, 0,
                                VK_NULL_HANDLE);

        ComputePushConstants pushConstants{
            {1.0f, 0.0f, 0.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 1.0f}
        };

        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants),
                           &pushConstants);
        vkCmdDispatch(commandBuffer, std::ceil(drawImage.extent.width / 16.), std::ceil(drawImage.extent.height / 16.), 1);

        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.image = swapChainImages[imageIndex];

        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

        VkRenderingAttachmentInfo colorAttachment{
            VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            VK_NULL_HANDLE,
            drawImage.imageView,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };

        VkRenderingInfo renderingInfo{
            VK_STRUCTURE_TYPE_RENDERING_INFO,
            VK_NULL_HANDLE,
            {},
            {0, 0, drawImage.extent.width, drawImage.extent.height},
            1,
            0,
            1,
            &colorAttachment
        }; {
            VulkanImage dstImage{
                swapChainImages[imageIndex],
                swapChainImageViews[imageIndex],
                {},
                {swapChainExtent.width, swapChainExtent.height, 1},
                surfaceFormat.format
            };

            BlitImage(commandBuffer, drawImage, dstImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        }

        vkCmdBeginRendering(commandBuffer, &renderingInfo);
    } else {
        VkClearValue clearValue = {0.2f, 0.2f, 0.6f, 1.0f};
        VkRenderPassBeginInfo renderPassInfo{
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            VK_NULL_HANDLE,
            renderPass,
            swapChainFramebuffers[imageIndex],
            {{0, 0}, swapChainExtent},
            1,
            &clearValue
        };

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    std::array layout = {mainDescriptorSetLayout};
    auto imageSet = frames[currentFrame].frameDescriptors.Allocate(device, layout);

    {
        DescriptorWriter writer;
        writer.WriteImage(1, defaultImage.imageView, textureSamplerNearest, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        writer.UpdateSet(device, mainDescriptorSet);
        writer.UpdateSet(device, imageSet);
    }

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &imageSet, 0, VK_NULL_HANDLE);

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    MeshPushConstants meshPushConstants{
        glm::mat4{1.f},
        meshAssets[0]->mesh.vertexBufferDeviceAddress
    };

    UpdatePushConstants(meshPushConstants);

    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &meshPushConstants);

    auto indexCount = meshAssets[0]->surfaces[0].indexCount;

    VkDeviceSize deviceSize{0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &meshAssets[0]->mesh.vertexBuffer, &deviceSize);
    vkCmdBindIndexBuffer(commandBuffer, meshAssets[0]->mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(commandBuffer, indexCount, 1, meshAssets[0]->surfaces[0].startIndex, 0, 0);

    stats.drawCallCount++;
    stats.triangleCount += indexCount / 3;

    if (dynamicRendering) {
        vkCmdEndRendering(commandBuffer);

        VkImageMemoryBarrier2 barrier{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            VK_NULL_HANDLE,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            drawImage.image,
            {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0,
                VK_REMAINING_MIP_LEVELS,
                0,
                VK_REMAINING_ARRAY_LAYERS
            }
        };

        VkDependencyInfo dependencyInfo{
            VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            VK_NULL_HANDLE,
            {},
            0,
            VK_NULL_HANDLE,
            0,
            VK_NULL_HANDLE,
            1,
            &barrier
        };

        barrier.image = swapChainImages[imageIndex];

        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

        // ImGui Render
        VkRenderingAttachmentInfo renderingAttachmentInfo{
            VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            VK_NULL_HANDLE,
            swapChainImageViews[imageIndex],
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };

        VkRenderingInfo renderingInfo{
            VK_STRUCTURE_TYPE_RENDERING_INFO,
            VK_NULL_HANDLE,
            {},
            {0, 0, drawImage.extent.width, drawImage.extent.height},
            1,
            0,
            1,
            &renderingAttachmentInfo
        };

        vkCmdBeginRendering(commandBuffer, &renderingInfo);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
        vkCmdEndRendering(commandBuffer);
    } else {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

        vkCmdEndRenderPass(commandBuffer);
    }

    // TODO: Maybe store this in each frame?
    // VkBufferCreateInfo bufferCreateInfo{
    //     VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    //     VK_NULL_HANDLE,
    //     {},
    //     sizeof(SceneData),
    //     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    //     VK_SHARING_MODE_EXCLUSIVE
    // };
    //
    // VmaAllocationCreateFlags allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    // if (isIntegratedGPU) {
    //     allocationFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    // }
    //
    // VmaAllocationCreateInfo allocationInfo{
    //         allocationFlags,
    //         VMA_MEMORY_USAGE_AUTO_PREFER_HOST
    //     };
    //
    // auto [sceneDataBuffer, allocation] = memoryManager->createUnmanagedBuffer(&bufferCreateInfo, &allocationInfo);
    // frames[currentFrame].frameCallbacks.emplace_back([&, allocation] { memoryManager->destroyBuffer(&sceneDataBuffer, &allocation); });
    //
    // void *data;
    // memoryManager->mapBuffer(&sceneDataBuffer, &data, &allocation);
    // // *static_cast<SceneData *>(data) = sceneData;
    // memcpy(data, &sceneData, sizeof(SceneData));
    // memoryManager->unmapBuffer(&sceneDataBuffer);
    //
    // std::array layouts = {mainDescriptorSetLayout};
    // auto descriptor = frames[currentFrame].frameDescriptors.Allocate(device, layouts);
    //
    // DescriptorWriter writer;
    // writer.WriteBuffer(0, sceneDataBuffer, 0, sizeof(SceneData), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    // writer.UpdateSet(device, descriptor);

    VK_CHECK(vkEndCommandBuffer(commandBuffer));
}

void VkRenderer::Shutdown() {
    if (!isVkRunning)
        return;

    vkDeviceWaitIdle(device);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(device, frames[i].renderFinishedSemaphore, nullptr);
        vkDestroySemaphore(device, frames[i].imageAvailableSemaphore, nullptr);
        vkDestroyFence(device, frames[i].inFlightFence, nullptr);

        vkDestroyCommandPool(device, frames[i].commandPool, nullptr);
        frames[i].frameDescriptors.Destroy(device);
    }

    vkDestroyCommandPool(device, immediateCommandPool, nullptr);

    CleanupSwapChain();

    vkDestroySampler(device, textureSamplerLinear, VK_NULL_HANDLE);
    vkDestroySampler(device, textureSamplerNearest, VK_NULL_HANDLE);

    // All buffers and images will be destroyed by the memory manager (EXCEPT UNMANAGED BUFFERS AND IMAGES), no need for manual management
    memoryManager.reset();

    mainDescriptorAllocator.Destroy(device);
    vkDestroyDescriptorSetLayout(device, mainDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, sceneDescriptorSetLayout, nullptr);

    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipeline(device, computePipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

    SavePipelineCache();

    vkDestroyPipelineCache(device, pipelineCache, nullptr);

    if (!dynamicRendering)
        vkDestroyRenderPass(device, renderPass, nullptr);

    vkDestroyDevice(device, nullptr);

#ifndef _NDEBUG
    DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
#endif

    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);

    isVkRunning = false;
}

void VkRenderer::RecreateSwapChain() {
    vkDeviceWaitIdle(device);

    int width, height;
    glfwGetFramebufferSize(glfwWindow, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(glfwWindow, &width, &height);
        glfwWaitEvents();
    }

    if (!dynamicRendering) {
        CleanupSwapChain();

        CreateSwapChain();
        CreateFramebuffers();

        framebufferResized = false;
    }
}

Mesh VkRenderer::CreateMesh(const std::span<VkVertex> &vertices, const std::span<uint16_t> &indices) {
    Mesh mesh{};

    VkBufferCreateInfo bufferCreateInfo{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        VK_NULL_HANDLE,
        {},
        vertices.size() * sizeof(vertices[0]) + indices.size() * sizeof(indices[0]),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_SHARING_MODE_EXCLUSIVE
    };

    VmaAllocationCreateFlags allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    if (isIntegratedGPU) {
        allocationFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VmaAllocationCreateInfo allocationCreateInfo{
        allocationFlags,
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    };

    const auto stagingBufferMappedTask = [&](auto &, void *mappedMemory) {
        const auto verticesSize = vertices.size() * sizeof(vertices[0]);
        const auto indicesSize = indices.size() * sizeof(indices[0]);

        memcpy(mappedMemory, vertices.data(), verticesSize);
        memcpy(static_cast<char *>(mappedMemory) + verticesSize, indices.data(), indicesSize);
    };

    const auto stagingBufferUnmappedTask = [&](const VkBuffer &stagingBuffer) {
        allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        mesh.vertexBuffer = memoryManager->createManagedBuffer(&bufferCreateInfo, &allocationCreateInfo).buffer;
        const VkBufferDeviceAddressInfo deviceAddressInfo{
            VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            VK_NULL_HANDLE,
            mesh.vertexBuffer
        };
        mesh.vertexBufferDeviceAddress = vkGetBufferDeviceAddress(device, &deviceAddressInfo);

        bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        mesh.indexBuffer = memoryManager->createManagedBuffer(&bufferCreateInfo, &allocationCreateInfo).buffer;

        ImmediateSubmit([&](auto &commandBuffer) {
            VkBufferCopy copyRegion{
                0,
                0,
                vertices.size() * sizeof(vertices[0])
            };

            vkCmdCopyBuffer(commandBuffer, stagingBuffer, mesh.vertexBuffer, 1, &copyRegion);

            copyRegion.srcOffset = vertices.size() * sizeof(vertices[0]);
            copyRegion.size = indices.size() * sizeof(indices[0]);

            vkCmdCopyBuffer(commandBuffer, stagingBuffer, mesh.indexBuffer, 1, &copyRegion);
        });
    };

    memoryManager->immediateBuffer(&bufferCreateInfo, &allocationCreateInfo, stagingBufferMappedTask,
                                           stagingBufferUnmappedTask);

    return mesh;
}

void VkRenderer::BlitImage(const VkCommandBuffer &commandBuffer, const VulkanImage &srcImage,
                           const VulkanImage &dstImage, const VkImageLayout srcLayout, const VkImageLayout dstLayout,
                           const VkImageAspectFlags aspectFlags) {
    VkImageBlit2 blitRegion{
        VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
        VK_NULL_HANDLE
    };

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
        VK_FILTER_LINEAR
    };

    vkCmdBlitImage2(commandBuffer, &blitInfo);
}

void VkRenderer::PickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, VK_NULL_HANDLE);
    if (deviceCount == 0) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support");
    }

    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());

    uint32_t maxScore = 0;
    for (const auto &gpu: physicalDevices) {
        uint32_t score = 0;
        VkPhysicalDeviceProperties tempDeviceProperties;
        vkGetPhysicalDeviceProperties(gpu, &tempDeviceProperties);

        if (tempDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        }

        score += tempDeviceProperties.limits.maxImageDimension2D;

        if (score > maxScore) {
            this->deviceProperties = tempDeviceProperties;
            maxScore = score;
            physicalDevice = gpu;
            isIntegratedGPU = tempDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
        }
    }

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR raytracingProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR,
        VK_NULL_HANDLE
    };

    VkPhysicalDeviceProperties2 deviceProperties2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        &raytracingProperties,
        this->deviceProperties
    };

    vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties2);
    raytracingCapable = raytracingProperties.shaderGroupHandleSize > 0;
}

// TODO: Maybe not use this and fix the issue with the compiler optimizing away structs down the (conditional) pNext chain?
#pragma GCC push_options
#pragma GCC optimize("O0")
void VkRenderer::CreateLogicalDevice() {
    FindQueueFamilies(physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set uniqueQueueFamilies = {
        queueFamilyIndices.graphicsFamily.value(), queueFamilyIndices.presentFamily.value()
    };
    size_t queueCount = uniqueQueueFamilies.size();

    auto *queuePriorities = new float[queueCount];
    for (size_t i = 0; i < queueCount; i++) {
        queuePriorities[i] = 1.0f;
    }

    for (uint32_t queueFamily: uniqueQueueFamilies) {
        queueCreateInfos.emplace_back(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,VK_NULL_HANDLE, 0, queueFamily, 1,
                                      queuePriorities);
    }

    VkPhysicalDeviceVulkan12Features vulkan12Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = VK_NULL_HANDLE,
        //            .descriptorIndexing = VK_TRUE,
        .bufferDeviceAddress = VK_TRUE
    };

    VkPhysicalDeviceVulkan13Features vulkan13Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &vulkan12Features,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };

    VkPhysicalDeviceHostQueryResetFeatures hostQueryResetFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
        &vulkan13Features,
        VK_TRUE
    };

    VkPhysicalDeviceFeatures2 deviceFeatures2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        &hostQueryResetFeatures,
        {.multiDrawIndirect = VK_TRUE }
    };

    deviceExtensions.push_back(VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME);
    deviceExtensions.push_back(VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME);

    if (dynamicRendering)
        deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);

    deviceExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

    if (raytracingCapable) {
        deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
        deviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);

        vulkan12Features.descriptorIndexing = VK_TRUE;

        VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
            VK_NULL_HANDLE,
            VK_TRUE
        };

        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
            &rayQueryFeatures,
            VK_TRUE
        };

        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
            &accelerationStructureFeatures,
            VK_TRUE,
            VK_FALSE,
            VK_FALSE,
            VK_TRUE,
            VK_TRUE,
        };

        vulkan12Features.pNext = &rayTracingPipelineFeatures;
    }

    VkDeviceCreateInfo createInfo{
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        &deviceFeatures2,
        0,
        static_cast<uint32_t>(queueCreateInfos.size()),
        queueCreateInfos.data(),
        0,
        VK_NULL_HANDLE,
        static_cast<uint32_t>(deviceExtensions.size()),
        deviceExtensions.data()
    };

#ifdef _NDEBUG
    createInfo.enabledLayerCount = 0;
#else
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
#endif

    VK_CHECK(vkCreateDevice(physicalDevice, &createInfo, VK_NULL_HANDLE, &device));

    vkGetDeviceQueue(device, queueFamilyIndices.graphicsFamily.value(), 0, &graphicsQueue);
    if (!asyncCompute)
        vkGetDeviceQueue(device, queueFamilyIndices.computeFamily.value(), 0, &computeQueue);

    vkGetDeviceQueue(device, queueFamilyIndices.presentFamily.value(), 0, &presentQueue);

    delete[] queuePriorities;
}
#pragma GCC pop_options

void VkRenderer::CreateQueryPool() {
    const VkQueryPoolCreateInfo createInfo{
        VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        VK_QUERY_TYPE_TIMESTAMP,
        2
    };

    vkCreateQueryPool(device, &createInfo, VK_NULL_HANDLE, &queryPool);
}

void VkRenderer::CreatePipelineCache() {
    constexpr VkPipelineCacheCreateInfo pipelineCacheInfo{
        VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        0,
        VK_NULL_HANDLE
    };

    VK_CHECK(vkCreatePipelineCache(device, &pipelineCacheInfo, VK_NULL_HANDLE, &pipelineCache));
}

void VkRenderer::CreateSwapChain() {
    auto [capabilities, formats, presentModes] = QuerySwapChainSupport(physicalDevice);

    surfaceFormat = ChooseSwapSurfaceFormat(formats);
    presentMode = ChooseSwapPresentMode(presentModes);
    swapChainExtent = ChooseSwapExtent(capabilities);

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        VK_NULL_HANDLE,
        0,
        surface,
        imageCount,
        surfaceFormat.format,
        surfaceFormat.colorSpace,
        swapChainExtent,
        1,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        VK_NULL_HANDLE,
        capabilities.currentTransform,
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        presentMode,
        VK_TRUE,
        VK_NULL_HANDLE
    };

    uint32_t qfi[] = {queueFamilyIndices.graphicsFamily.value(), queueFamilyIndices.presentFamily.value()};

    if (queueFamilyIndices.graphicsFamily != queueFamilyIndices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = qfi;
    }

    VK_CHECK(vkCreateSwapchainKHR(device, &createInfo, VK_NULL_HANDLE, &swapChain));

    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, VK_NULL_HANDLE);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());
    swapChainImageViews.resize(swapChainImages.size());

    VkImageViewCreateInfo imageViewCreateInfo{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        VK_NULL_HANDLE,
        VK_IMAGE_VIEW_TYPE_2D,
        surfaceFormat.format,
        {
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY
        },
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };

    for (size_t i = 0; i < swapChainImages.size(); i++) {
        imageViewCreateInfo.image = swapChainImages[i];
        VK_CHECK(vkCreateImageView(device, &imageViewCreateInfo, VK_NULL_HANDLE, &swapChainImageViews[i]));
    }

    // Compute shader image
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    auto imageCreateInfo = CreateImageCreateInfo(VK_FORMAT_R16G16B16A16_SFLOAT, {swapChainExtent.width, swapChainExtent.height, 1}, usage, VK_IMAGE_TILING_OPTIMAL,
                                                 VK_IMAGE_LAYOUT_UNDEFINED, {});

    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    VmaAllocationCreateInfo allocationCreateInfo{
        {},
        VMA_MEMORY_USAGE_AUTO
    };

    ImageViewCreateInfo viewCreateInfo{
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };

    drawImage = memoryManager->createManagedImage(&imageCreateInfo, &allocationCreateInfo, &viewCreateInfo);

    // Depth Image
    // usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    // imageCreateInfo = CreateImageCreateInfo(VK_FORMAT_D32_SFLOAT, {swapChainExtent.width, swapChainExtent.height, 1}, usage, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED, {});
    //
    // viewCreateInfo.format = VK_FORMAT_D32_SFLOAT;
    // viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    //
    // depthImage = memoryManager->createManagedImage(&imageCreateInfo, &allocationCreateInfo, &viewCreateInfo);
}

void VkRenderer::CreateRenderPass() {
    VkAttachmentDescription swapChainImageDescription{
        0,
        surfaceFormat.format,
        msaaSamples,
        VK_ATTACHMENT_LOAD_OP_CLEAR,
        VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };

    VkAttachmentReference colorAttachmentRef{
        0,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkSubpassDescription graphicsSubpass{
        0,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        0,
        VK_NULL_HANDLE,
        1,
        &colorAttachmentRef,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        0,
        VK_NULL_HANDLE
    };

    VkSubpassDependency dependency{
        VK_SUBPASS_EXTERNAL,
        0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
    };

    const VkRenderPassCreateInfo renderPassInfo{
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        1,
        &swapChainImageDescription,
        1,
        &graphicsSubpass,
        1,
        &dependency
    };

    VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, VK_NULL_HANDLE, &renderPass));
}

void VkRenderer::CreatePipelineLayout() {
    VkPushConstantRange graphicsPushConstantRange{
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(MeshPushConstants)
    };

    const std::array pushConstantRanges = {graphicsPushConstantRange};
    const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        1,
        &mainDescriptorSetLayout,
        pushConstantRanges.size(),
        pushConstantRanges.data()
    };

    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, VK_NULL_HANDLE, &pipelineLayout));
}

void VkRenderer::CreateGraphicsPipeline() {
    auto vertShaderCode = ReadFile<char>("shaders/shader.vert.spv");
    auto fragShaderCode = ReadFile<char>("shaders/shader.frag.spv");

    VkShaderModuleCreateInfo vertShaderModuleCreateInfo{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        vertShaderCode.size(),
        reinterpret_cast<const uint32_t *>(vertShaderCode.data())
    };

    VkShaderModuleCreateInfo fragShaderModuleCreateInfo{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        fragShaderCode.size(),
        reinterpret_cast<const uint32_t *>(fragShaderCode.data())
    };

    VkShaderModule vertShaderModule;
    VkShaderModule fragShaderModule;

    VK_CHECK(vkCreateShaderModule(device, &vertShaderModuleCreateInfo, VK_NULL_HANDLE, &vertShaderModule));
    VK_CHECK(vkCreateShaderModule(device, &fragShaderModuleCreateInfo, VK_NULL_HANDLE, &fragShaderModule));

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        VK_SHADER_STAGE_VERTEX_BIT,
        vertShaderModule,
        "main",
        VK_NULL_HANDLE
    };

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        fragShaderModule,
        "main",
        VK_NULL_HANDLE
    };

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    auto vertexInputAttributeDescriptions = VkVertex::getVertexInputAttributeDescriptions();
    auto vertexInputBindingDescription = VkVertex::getVertexInputBindingDescription();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        1,
        &vertexInputBindingDescription,
        vertexInputAttributeDescriptions.size(),
        vertexInputAttributeDescriptions.data()
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VK_FALSE
    };

    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        1,
        VK_NULL_HANDLE,
        1,
        VK_NULL_HANDLE
    };

    VkPipelineRasterizationStateCreateInfo rasterizer{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        VK_FALSE,
        VK_FALSE,
        VK_POLYGON_MODE_FILL,
        VK_CULL_MODE_BACK_BIT,
        VK_FRONT_FACE_COUNTER_CLOCKWISE,
        VK_FALSE,
        0.0f,
        0.0f,
        0.0f,
        1.0f
    };

    VkPipelineMultisampleStateCreateInfo multisampling{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        msaaSamples,
        VK_FALSE,
        1.0f,
        VK_NULL_HANDLE,
        VK_FALSE,
        VK_FALSE
    };

    VkPipelineDepthStencilStateCreateInfo depthStencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        VK_TRUE,
        VK_TRUE,
        VK_COMPARE_OP_GREATER_OR_EQUAL,
        VK_FALSE,
        VK_FALSE,
        {},
        {},
        0.f,
        1.f
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachment{
        VK_FALSE,
        VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_OP_ADD,
        VK_BLEND_FACTOR_ONE,
        VK_BLEND_FACTOR_ZERO,
        VK_BLEND_OP_ADD,
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    VkPipelineColorBlendStateCreateInfo colorBlending{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        VK_FALSE,
        VK_LOGIC_OP_COPY,
        1,
        &colorBlendAttachment,
        {0.0f, 0.0f, 0.0f, 0.0f}
    };

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamicState{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        2,
        dynamicStates
    };

    VkPipelineRenderingCreateInfo renderingCreateInfo{
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        VK_NULL_HANDLE,
        {},
    };

    VkGraphicsPipelineCreateInfo pipelineInfo{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        dynamicRendering ? &renderingCreateInfo : VK_NULL_HANDLE,
        0,
        2,
        shaderStages,
        &vertexInputInfo,
        &inputAssembly,
        VK_NULL_HANDLE,
        &viewportState,
        &rasterizer,
        &multisampling,
        &depthStencil,
        &colorBlending,
        &dynamicState,
        pipelineLayout,
        dynamicRendering ? VK_NULL_HANDLE : renderPass,
        0,
        VK_NULL_HANDLE,
        -1
    };

    VK_CHECK(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineInfo, VK_NULL_HANDLE, &graphicsPipeline));

    vkDestroyShaderModule(device, vertShaderModule, VK_NULL_HANDLE);
    vkDestroyShaderModule(device, fragShaderModule, VK_NULL_HANDLE);
}

void VkRenderer::CreateComputePipeline() {
    const auto computeShaderCode = ReadFile<char>("shaders/shader.comp.spv");

    const VkShaderModuleCreateInfo computeShaderModuleCreateInfo{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        computeShaderCode.size(),
        reinterpret_cast<const uint32_t *>(computeShaderCode.data())
    };

    VkShaderModule computeShaderModule;

    VK_CHECK(vkCreateShaderModule(device, &computeShaderModuleCreateInfo, VK_NULL_HANDLE, &computeShaderModule));

    const VkPipelineShaderStageCreateInfo computeShaderStageInfo{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        VK_SHADER_STAGE_COMPUTE_BIT,
        computeShaderModule,
        "main",
        VK_NULL_HANDLE
    };

    const VkComputePipelineCreateInfo pipelineInfo{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        computeShaderStageInfo,
        pipelineLayout,
        VK_NULL_HANDLE,
        -1
    };

    VK_CHECK(vkCreateComputePipelines(device, pipelineCache, 1, &pipelineInfo, VK_NULL_HANDLE, &computePipeline));

    vkDestroyShaderModule(device, computeShaderModule, VK_NULL_HANDLE);
}

void VkRenderer::CreateFramebuffers() {
    swapChainFramebuffers.resize(swapChainImageViews.size());
    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
        VkFramebufferCreateInfo framebufferInfo{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            VK_NULL_HANDLE,
            0,
            renderPass,
            1,
            &swapChainImageViews[i],
            swapChainExtent.width,
            swapChainExtent.height,
            1
        };

        VK_CHECK(vkCreateFramebuffer(device, &framebufferInfo, VK_NULL_HANDLE, &swapChainFramebuffers[i]));
    }
}

void VkRenderer::CreateCommandPool() {
    VkCommandPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        VK_NULL_HANDLE,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        queueFamilyIndices.graphicsFamily.value()
    };

    VK_CHECK(vkCreateCommandPool(device, &poolInfo, VK_NULL_HANDLE, &immediateCommandPool));

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VK_CHECK(vkCreateCommandPool(device, &poolInfo, VK_NULL_HANDLE, &frames[i].commandPool));
    }
}

void VkRenderer::CreateCommandBuffers() {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkCommandBufferAllocateInfo allocInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            VK_NULL_HANDLE,
            frames[i].commandPool,
            VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            1
        };

        VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &frames[i].commandBuffer));
    }
}

void VkRenderer::CreateDefaultTexture() {
    const uint32_t purple = packUnorm4x8(glm::vec4{1, 0, 1, 1});

    constexpr uint32_t textureSize = 16;
    std::array<uint32_t, textureSize * textureSize> pixels{};
    for (size_t i = 0; i < textureSize * textureSize; i++) {
        pixels[i] = (i / textureSize + i % textureSize) % 2 == 0 ? 0 : purple;
    }

    defaultImage = memoryManager->createTexture(pixels.data(), this, {16, 16, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    VkSamplerCreateInfo sample{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

    sample.magFilter = VK_FILTER_NEAREST;
    sample.minFilter = VK_FILTER_NEAREST;

    vkCreateSampler(device, &sample, VK_NULL_HANDLE, &textureSamplerNearest);

    sample.magFilter = VK_FILTER_LINEAR;
    sample.minFilter = VK_FILTER_LINEAR;

    vkCreateSampler(device, &sample, VK_NULL_HANDLE, &textureSamplerLinear);
}

void VkRenderer::CreateSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        VK_NULL_HANDLE,
        0
    };

    VkFenceCreateInfo fenceInfo{
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        VK_NULL_HANDLE,
        VK_FENCE_CREATE_SIGNALED_BIT
    };

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, VK_NULL_HANDLE, &frames[i].imageAvailableSemaphore));
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, VK_NULL_HANDLE, &frames[i].renderFinishedSemaphore));
        VK_CHECK(vkCreateFence(device, &fenceInfo, VK_NULL_HANDLE, &frames[i].inFlightFence));
    }
}

void VkRenderer::CreateDescriptors() {
    std::array<DescriptorAllocator::PoolSizeRatio, 2> sizes{
        {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}
        }
    };

    mainDescriptorAllocator.InitPool(device, 10, sizes);

    {
        DescriptorLayoutBuilder builder;
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);
        builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        mainDescriptorSetLayout = builder.Build(device);
    }

    {
        DescriptorLayoutBuilder builder;
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        sceneDescriptorSetLayout = builder.Build(device);
    }

    std::array layouts{mainDescriptorSetLayout};
    mainDescriptorSet = mainDescriptorAllocator.Allocate(device, layouts);

    DescriptorWriter writer;
    writer.WriteImage(0, drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    writer.UpdateSet(device, mainDescriptorSet);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        std::array<DescriptorAllocator::PoolSizeRatio, 4> frameSizes = {
            {
                { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          3 },
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         3 },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         3 },
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 },
            }
        };

        frames[i].frameDescriptors = DescriptorAllocator{};
        frames[i].frameDescriptors.InitPool(device, 1000, frameSizes);
    }
}

void VkRenderer::FindQueueFamilies(const VkPhysicalDevice &gpu) {
    if (queueFamilyIndices.IsComplete()) {
        return;
    }

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamilyCount, VK_NULL_HANDLE);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto &queueFamily: queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queueFamilyIndices.graphicsFamily = i;
        }

        if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            queueFamilyIndices.computeFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &presentSupport);

        if (presentSupport) {
            queueFamilyIndices.presentFamily = i;
        }

        if (queueFamilyIndices.IsComplete()) {
            break;
        }

        i++;
    }
}

VkRenderer::SwapChainSupportDetails VkRenderer::QuerySwapChainSupport(const VkPhysicalDevice &gpu) const {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, VK_NULL_HANDLE);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &presentModeCount, VK_NULL_HANDLE);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR VkRenderer::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats) {
    for (const auto &availableFormat: availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SINT && availableFormat.colorSpace ==
            VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR VkRenderer::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes) {
    for (const auto &availablePresentMode: availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VkRenderer::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) const {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    int w, h;
    glfwGetFramebufferSize(glfwWindow, &w, &h);

    const VkExtent2D actualExtent = {
        static_cast<uint32_t>(w),
        static_cast<uint32_t>(h)
    };

    return {
        std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
    };
}

void VkRenderer::CleanupSwapChain() {
    for (const auto &f: swapChainFramebuffers) {
        vkDestroyFramebuffer(device, f, nullptr);
    }

    for (const auto &imageView: swapChainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }

    vkDestroySwapchainKHR(device, swapChain, nullptr);
}

VkBufferCreateInfo VkRenderer::CreateBufferCreateInfo(VkDeviceSize size, VkBufferUsageFlags usage,
                                                      VkSharingMode sharingMode) {
    return VkBufferCreateInfo{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        VK_NULL_HANDLE,
        {},
        size,
        usage,
        sharingMode
    };
}

VkImageCreateInfo VkRenderer::CreateImageCreateInfo(VkFormat format, VkExtent3D extent, VkImageUsageFlags usage,
                                                    VkImageTiling tiling, VkImageLayout layout,
                                                    VkImageCreateFlags flags) {
    return VkImageCreateInfo{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        VK_NULL_HANDLE,
        flags,
        VK_IMAGE_TYPE_2D,
        format,
        extent,
        1,
        1,
        VK_SAMPLE_COUNT_1_BIT, // Might need to change this later down the line for multisampling
        tiling,
        usage,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr,
        layout,
    };
}

void VkRenderer::UpdatePushConstants(MeshPushConstants &meshPushConstants) const {
    const glm::mat4 view = lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 proj = glm::perspective(glm::radians(fov),
                                      static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.
                                          height), 0.1f, 10000.f);
    proj[1][1] *= -1;

    meshPushConstants.worldMatrix = proj * view;
}

void VkRenderer::SavePipelineCache() const {
    size_t size;
    vkGetPipelineCacheData(device, pipelineCache, &size, nullptr);

    std::vector<uint8_t> data(size);
    vkGetPipelineCacheData(device, pipelineCache, &size, data.data());

    std::ofstream file("pipeline_cache.bin", std::ios::binary);
    file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(size));
}

#ifndef _NDEBUG
VkBool32 VKAPI_CALL VkRenderer::DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *) {
    std::cerr << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

VkResult VkRenderer::CreateDebugUtilsMessengerEXT(VkInstance instance,
                                                  const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
                                                  const VkAllocationCallbacks *pAllocator,
                                                  VkDebugUtilsMessengerEXT *pDebugMessenger) {
    if (const auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(
        instance, "vkCreateDebugUtilsMessengerEXT")); func != VK_NULL_HANDLE) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }

    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void VkRenderer::DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                               const VkAllocationCallbacks *pAllocator) {
    if (const auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(
        instance, "vkDestroyDebugUtilsMessengerEXT")); func != VK_NULL_HANDLE) {
        func(instance, debugMessenger, pAllocator);
    }
}
#endif
