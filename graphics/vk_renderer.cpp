#include <iostream>
#include <set>
#include <algorithm>
#include <chrono>
#include "vk_renderer.h"

#include <imgui_impl_vulkan.h>
#include <ranges>
#include "vk/memory/vk_mesh_assets.h"

#include "file.h"
#include "vk/vk_gui.h"
#include "vk/vk_pipeline_builder.h"
#include <random>

VkRenderer::VkRenderer(GLFWwindow *window, Camera *camera, const bool dynamicRendering, const bool asyncCompute)
    : dynamicRendering(dynamicRendering),
      asyncCompute(asyncCompute),
#ifndef NDEBUG
      debugMessenger(VK_NULL_HANDLE),
#endif
      glfwWindow(window),
      camera(camera),
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
      mainDescriptorSetLayout(VK_NULL_HANDLE),
      depthPrepassPipelineLayout(VK_NULL_HANDLE),
      depthPrepassPipeline(VK_NULL_HANDLE),
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

    InitializeInstance();

#ifndef NDEBUG
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
    CreateCommandPool();
    // CreateQueryPool();

    memoryManager = std::make_shared<VkMemoryManager>(instance, physicalDevice, device, isIntegratedGPU);

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
    CreateDepthImage();

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

    if (!dynamicRendering) {
        CreateRenderPass();
    }

    CreateDescriptors();
    CreatePipelineLayout();
    CreateGraphicsPipeline();
    CreateComputePipeline();

    if (!dynamicRendering) {
        CreateFramebuffers();
    }

    CreateCommandBuffers();
    CreateDefaultTexture();
    CreateSyncObjects();

    ComputeFrustum();

    auto structureFile = LoadGLTF(this, "../assets/Sponza/Sponza.gltf");

    assert(structureFile.has_value());

    loadedScenes["structure"] = structureFile.value();

    CreateRandomLights();
    UpdateDepthComputeDescriptorSets();

    isVkRunning = true;
}

VkRenderer::~VkRenderer() {
    Shutdown();
}

void VkRenderer::FramebufferNeedsResizing() {
    framebufferResized = true;
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
#ifndef NDEBUG
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

#ifndef NDEBUG
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

int lightSceneTime = 0.f;

void VkRenderer::Render(EngineStats &stats) {
    stats.drawCallCount = 0;
    stats.triangleCount = 0;

    if (lightSceneTime++ > 2000) {
        lightSceneTime = 0;
    }

    UpdateScene();

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
    vkQueueWaitIdle(graphicsQueue);
    vkResetCommandBuffer(frames[currentFrame].commandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
    vkResetCommandBuffer(depthPrepassCommandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

    const auto start = std::chrono::high_resolution_clock::now();

    Draw(frames[currentFrame].commandBuffer, imageIndex, stats);

    const auto end = std::chrono::high_resolution_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    stats.meshDrawTime = static_cast<float>(elapsed) / 1000.f;

//    if (asyncCompute) {
//        AsyncComputeDispatch(frames[currentFrame].commandBuffer, imageIndex);
//    }

    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};
    std::array waitSemaphores = {frames[currentFrame].imageAvailableSemaphore, depthPrepassSemaphore};
    VkSubmitInfo submitInfo{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        VK_NULL_HANDLE,
        static_cast<uint32_t>(waitSemaphores.size()),
        waitSemaphores.data(),
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

bool isObjectVisible(const VkRenderObject &obj, const glm::mat4 &viewProjection) {
    static constexpr std::array<glm::vec3, 8> corners{
            glm::vec3 {1, 1, 1},
            glm::vec3 {1, 1, -1},
            glm::vec3 {1, -1, 1},
            glm::vec3 {1, -1, -1},
            glm::vec3 {-1, 1, 1},
            glm::vec3 {-1, 1, -1},
            glm::vec3 {-1, -1, 1},
            glm::vec3 {-1, -1, -1}
    };

    glm::mat4 matrix = viewProjection * obj.transform;

    glm::vec3 min{1.5, 1.5, 1.5};
    glm::vec3 max = -min;

    for (auto &c : corners) {
        glm::vec4 transformed = matrix * glm::vec4{obj.bounds.origin + (c * obj.bounds.extents), 1};
        transformed /= transformed.w;

        min = glm::min(min, glm::vec3{transformed});
        max = glm::max(max, glm::vec3{transformed});
    }

    return min.z < 1.f && max.z > 0.f && min.x < 1.f && max.x > -1.f && min.y < 1.f && max.y > -1.f;
}

void VkRenderer::DrawObject(const VkCommandBuffer &commandBuffer, const VkRenderObject &draw, const VkDescriptorSet &sceneDescriptorSet, VkMaterialPipeline &lastPipeline, VkMaterialInstance &lastMaterialInstance, VkBuffer &lastIndexBuffer) {
    if (lastMaterialInstance != *draw.materialInstance) {
        lastMaterialInstance = *draw.materialInstance;

        if (draw.materialInstance->pipeline != lastPipeline) {
            lastPipeline = lastMaterialInstance.pipeline;
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lastMaterialInstance.pipeline.pipeline);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lastMaterialInstance.pipeline.layout, 0, 1, &sceneDescriptorSet, 0, VK_NULL_HANDLE);
        }

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.materialInstance->pipeline.layout, 1, 1, &draw.materialInstance->descriptorSet, 0, VK_NULL_HANDLE);

    }

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.materialInstance->pipeline.layout, 2, 1, &mainDescriptorSet, 0, VK_NULL_HANDLE);

    if (draw.indexBuffer != lastIndexBuffer) {
        lastIndexBuffer = draw.indexBuffer;
        vkCmdBindIndexBuffer(commandBuffer, draw.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    }

    MeshPushConstants pushConstants{
            draw.transform,
            draw.vertexBufferAddress
    };

    FragmentPushConstants fragmentPushConstants{
            draw.transform,
            camera->Position()
    };

    vkCmdPushConstants(commandBuffer, draw.materialInstance->pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &pushConstants);
    vkCmdPushConstants(commandBuffer, draw.materialInstance->pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(MeshPushConstants) + 8, sizeof(FragmentPushConstants), &fragmentPushConstants);
    vkCmdDrawIndexed(commandBuffer, draw.indexCount, 1, draw.firstIndex, 0, 0);
}

void VkRenderer::DrawDepthPrepass(const std::vector<size_t> &drawIndices) {
    VkCommandBufferBeginInfo beginInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        VK_NULL_HANDLE,
        0,
        VK_NULL_HANDLE
    };

    VK_CHECK(vkBeginCommandBuffer(depthPrepassCommandBuffer, &beginInfo));

    VkClearValue depthPrepassClearValue{
        .depthStencil = {1.0f, 0}
    };

    VkRenderPassBeginInfo renderPassInfo{
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        VK_NULL_HANDLE,
        depthPrepassRenderPass,
        depthPrepassFramebuffer,
        {{0, 0}, swapChainExtent},
        1,
        &depthPrepassClearValue
    };

    vkCmdBeginRenderPass(depthPrepassCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(depthPrepassCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, depthPrepassPipeline);
    vkCmdSetViewport(depthPrepassCommandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(depthPrepassCommandBuffer, 0, 1, &scissor);
    vkCmdBindDescriptorSets(depthPrepassCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, depthPrepassPipelineLayout, 0, 1, &depthPrepassDescriptorSet, 0, VK_NULL_HANDLE);

    VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

    for (const auto &i : drawIndices) {
        const VkRenderObject &draw = mainDrawContext.opaqueSurfaces[i];
        if (draw.indexBuffer != lastIndexBuffer) {
            lastIndexBuffer = draw.indexBuffer;
            vkCmdBindIndexBuffer(depthPrepassCommandBuffer, draw.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }

        MeshPushConstants pushConstants{
                draw.transform,
                draw.vertexBufferAddress
        };

        vkCmdPushConstants(depthPrepassCommandBuffer, draw.materialInstance->pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &pushConstants);
        vkCmdDrawIndexed(depthPrepassCommandBuffer, draw.indexCount, 1, draw.firstIndex, 0, 0);
    }

//    for (const auto &r : mainDrawContext.transparentSurfaces) {
//        if (r.indexBuffer != lastIndexBuffer) {
//            lastIndexBuffer = r.indexBuffer;
//            vkCmdBindIndexBuffer(depthPrepassCommandBuffer, r.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
//        }
//
//        MeshPushConstants pushConstants{
//                r.transform,
//                r.vertexBufferAddress
//        };
//
//        vkCmdPushConstants(depthPrepassCommandBuffer, r.materialInstance->pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &pushConstants);
//        vkCmdDrawIndexed(depthPrepassCommandBuffer, r.indexCount, 1, r.firstIndex, 0, 0);
//    }

    vkCmdEndRenderPass(depthPrepassCommandBuffer);
    VK_CHECK(vkEndCommandBuffer(depthPrepassCommandBuffer));

    VkSubmitInfo submitInfo{
            VK_STRUCTURE_TYPE_SUBMIT_INFO,
            VK_NULL_HANDLE,
            0,
            VK_NULL_HANDLE,
            VK_NULL_HANDLE,
            1,
            &depthPrepassCommandBuffer,
            1,
            &depthPrepassSemaphore
    };

    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
}

void VkRenderer::Draw(const VkCommandBuffer &commandBuffer, uint32_t imageIndex, EngineStats &stats) {
    static std::vector<size_t> drawIndices;
    static bool sorted = false;
    drawIndices.reserve(mainDrawContext.opaqueSurfaces.size());
    for (size_t i = 0; i < mainDrawContext.opaqueSurfaces.size(); i++) {
        if (isObjectVisible(mainDrawContext.opaqueSurfaces[i], sceneData.worldMatrix))
            drawIndices.push_back(i);
    }

    std::sort(drawIndices.begin(), drawIndices.end(), [&](const uint16_t &a, const uint16_t &b) {
        const auto &surfaceA = mainDrawContext.opaqueSurfaces[a];
        const auto &surfaceB = mainDrawContext.opaqueSurfaces[b];
        if (surfaceA.materialInstance == surfaceB.materialInstance) {
            return surfaceA.indexBuffer < surfaceB.indexBuffer;
        } else {
            return surfaceA.materialInstance < surfaceB.materialInstance;
        }
    });

    auto buffer = memoryManager->createUnmanagedBuffer(sizeof(SceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VMA_MEMORY_USAGE_AUTO);
    frames[currentFrame].frameCallbacks.emplace_back([&, buffer] { memoryManager->destroyBuffer(buffer); });

    SceneData *data;
    memoryManager->mapBuffer(buffer, reinterpret_cast<void **>(&data));
    memcpy(data, &sceneData, sizeof(SceneData));
    memoryManager->unmapBuffer(buffer);

    static std::array layouts = {sceneDescriptorSetLayout};
    auto sceneDescriptorSet = frames[currentFrame].frameDescriptors.Allocate(device, layouts);

    {
        DescriptorWriter writer;
        writer.WriteBuffer(0, buffer.buffer, 0, sizeof(SceneData), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        writer.UpdateSet(device, sceneDescriptorSet);
        writer.UpdateSet(device, depthPrepassDescriptorSet);
    }

    DrawDepthPrepass(drawIndices);

    VkCommandBufferBeginInfo beginInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        VK_NULL_HANDLE,
        0,
        VK_NULL_HANDLE
    };

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &lightDescriptorSet,
                            0, VK_NULL_HANDLE);

    auto view = camera->ViewMatrix();
    auto proj = camera->ProjectionMatrix();

    ComputePushConstants pushConstants{
            glm::inverse(view * proj),
            camera->Position(),
            {viewport.width, viewport.height}
    };

    vkCmdPushConstants(commandBuffer, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants),
                       &pushConstants);
    vkCmdDispatch(commandBuffer, (swapChainExtent.width - 1) / 16 + 1, (swapChainExtent.height - 1) / 16 + 1, 1);

    if (dynamicRendering) {
        VkRenderingAttachmentInfo colorAttachment{
            VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            VK_NULL_HANDLE,
            swapChainImageViews[imageIndex],
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };

        VkRenderingInfo renderingInfo{
            VK_STRUCTURE_TYPE_RENDERING_INFO,
            VK_NULL_HANDLE,
            {},
            {0, 0, swapChainExtent.width, swapChainExtent.height},
            1,
            0,
            1,
            &colorAttachment
        };

        vkCmdBeginRendering(commandBuffer, &renderingInfo);
    } else {
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
        clearValues[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo renderPassInfo{
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            VK_NULL_HANDLE,
            renderPass,
            swapChainFramebuffers[imageIndex],
            {{0, 0}, swapChainExtent},
            clearValues.size(),
            clearValues.data()
        };

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    }

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    VkMaterialPipeline lastPipeline{};
    VkMaterialInstance lastMaterialInstance{};
    VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

    for (const auto &i : drawIndices) {
        const VkRenderObject &draw = mainDrawContext.opaqueSurfaces[i];
        DrawObject(commandBuffer, draw, sceneDescriptorSet, lastPipeline, lastMaterialInstance, lastIndexBuffer);
        stats.drawCallCount++;
        stats.triangleCount += draw.indexCount / 3;
    }

    for (const auto &r : mainDrawContext.transparentSurfaces) {
        DrawObject(commandBuffer, r, sceneDescriptorSet, lastPipeline, lastMaterialInstance, lastIndexBuffer);
        stats.drawCallCount++;
        stats.triangleCount += r.indexCount / 3;
    }

    if (dynamicRendering) {
        vkCmdEndRendering(commandBuffer);

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
            {0, 0, swapChainExtent.width, swapChainExtent.height},
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

    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    mainDrawContext.opaqueSurfaces.clear();
    mainDrawContext.transparentSurfaces.clear();
    drawIndices.clear();
}

void VkRenderer::DrawIndirect(VkCommandBuffer const &commandBuffer, uint32_t imageIndex, EngineStats &stats) {

}

void VkRenderer::Shutdown() {
    if (!isVkRunning)
        return;

    vkDeviceWaitIdle(device);

    loadedScenes.clear();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(device, frames[i].renderFinishedSemaphore, nullptr);
        vkDestroySemaphore(device, frames[i].imageAvailableSemaphore, nullptr);
        vkDestroyFence(device, frames[i].inFlightFence, nullptr);

        vkDestroyCommandPool(device, frames[i].commandPool, nullptr);
        frames[i].frameDescriptors.Destroy(device);

        for (auto &callback : frames[i].frameCallbacks)
            callback();
    }

    if (!asyncCompute)
        vkDestroySemaphore(device, computeFinishedSemaphore, nullptr);

    vkDestroySemaphore(device, depthPrepassSemaphore, nullptr);

    vkDestroyCommandPool(device, immediateCommandPool, nullptr);

    CleanupSwapChain();

    vkDestroySampler(device, textureSamplerLinear, VK_NULL_HANDLE);
    vkDestroySampler(device, textureSamplerNearest, VK_NULL_HANDLE);

    // All buffers and images will be destroyed by the memory manager (EXCEPT UNMANAGED BUFFERS AND IMAGES), no need for manual management
    memoryManager.reset();

    mainDescriptorAllocator.Destroy(device);
    vkDestroyDescriptorSetLayout(device, mainDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, sceneDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, lightDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, frustumDescriptorSetLayout, nullptr);

    vkDestroyPipeline(device, depthPrepassPipeline, nullptr);
    vkDestroyPipeline(device, computePipeline, nullptr);
    vkDestroyPipeline(device, frustumPipeline, nullptr);
    vkDestroyPipelineLayout(device, depthPrepassPipelineLayout, nullptr);
    vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);
    vkDestroyPipelineLayout(device, frustumPipelineLayout, nullptr);

    SavePipelineCache();

    vkDestroyPipelineCache(device, pipelineCache, nullptr);

    if (!dynamicRendering) {
        vkDestroyRenderPass(device, renderPass, nullptr);
        vkDestroyRenderPass(device, depthPrepassRenderPass, nullptr);
    }

    metalRoughMaterial.clearResources(device);

    vkDestroyDevice(device, nullptr);

#ifndef NDEBUG
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
        CreateDepthImage();
        CreateFramebuffers();
        UpdateDepthComputeDescriptorSets();

        viewport.width = static_cast<float>(swapChainExtent.width);
        viewport.height = static_cast<float>(swapChainExtent.height);
        scissor.extent = swapChainExtent;

        framebufferResized = false;
    }
}

Mesh VkRenderer::CreateMesh(const std::span<VkVertex> &vertices, const std::span<uint32_t> &indices) {
    Mesh mesh{};

    const auto verticesSize = vertices.size() * sizeof(vertices[0]);
    const auto indicesSize = indices.size() * sizeof(indices[0]);
    const auto stagingBufferMappedTask = [&](auto &, void *mappedMemory) {
        memcpy(mappedMemory, vertices.data(), verticesSize);
        memcpy(static_cast<char *>(mappedMemory) + verticesSize, indices.data(), indicesSize);
    };

    const auto stagingBufferUnmappedTask = [&](const VkBuffer &stagingBuffer) {
        mesh.vertexBuffer = memoryManager->createManagedBuffer(verticesSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT).buffer;
        const VkBufferDeviceAddressInfo deviceAddressInfo{
            VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            VK_NULL_HANDLE,
            mesh.vertexBuffer
        };
        mesh.vertexBufferDeviceAddress = vkGetBufferDeviceAddress(device, &deviceAddressInfo);
        mesh.indexBuffer = memoryManager->createManagedBuffer(indicesSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT).buffer;

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

    memoryManager->stagingBuffer(verticesSize + indicesSize, stagingBufferMappedTask, stagingBufferUnmappedTask);

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
        srcImage.format == VK_FORMAT_D32_SFLOAT ? VK_FILTER_NEAREST : VK_FILTER_LINEAR
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
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    raytracingCapable = raytracingProperties.shaderGroupHandleSize > 0;
}

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

    // VkPhysicalDeviceHostQueryResetFeatures hostQueryResetFeatures{
    //     VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
    //     &vulkan13Features,
    //     VK_TRUE
    // };

    VkPhysicalDeviceFeatures2 deviceFeatures2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        &vulkan13Features,
        {.multiDrawIndirect = VK_TRUE }
    };

    deviceExtensions.push_back(VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME);
    deviceExtensions.push_back(VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME);

    if (dynamicRendering)
        deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);

    deviceExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

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

    if (raytracingCapable) {
        deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
        deviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);

        vulkan12Features.descriptorIndexing = VK_TRUE;

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

#ifdef NDEBUG
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

    const uint32_t qfi[] = {queueFamilyIndices.graphicsFamily.value(), queueFamilyIndices.presentFamily.value()};

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

    camera->UpdateAspectRatio(static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height));

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
}

void VkRenderer::CreateDepthImage() {
    ImageViewCreateInfo viewCreateInfo{
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}
    };

    depthImage = memoryManager->createManagedImage(0, VK_FORMAT_D32_SFLOAT, {swapChainExtent.width, swapChainExtent.height, 1}, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_LAYOUT_UNDEFINED, 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false, &viewCreateInfo);

    ImmediateSubmit([&](auto &cmd) {
        TransitionImage(cmd, depthImage, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    });

    VkSamplerCreateInfo samplerInfo{
            VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            VK_NULL_HANDLE,
            0,
            VK_FILTER_LINEAR,
            VK_FILTER_LINEAR,
            VK_SAMPLER_MIPMAP_MODE_LINEAR,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            0.0f,
            VK_FALSE,
            1.0f,
            VK_FALSE,
            VK_COMPARE_OP_NEVER,
            0.0f,
            0.0f,
            VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
            VK_FALSE
    };

    VK_CHECK(vkCreateSampler(device, &samplerInfo, VK_NULL_HANDLE, &depthSampler));
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

    VkAttachmentDescription mainDepthImageDescription{
        0,
        VK_FORMAT_D32_SFLOAT,
        msaaSamples,
        VK_ATTACHMENT_LOAD_OP_LOAD,
        VK_ATTACHMENT_STORE_OP_NONE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentReference colorAttachmentRef{
        0,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkAttachmentReference mainDepthAttachmentRef{
        1,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
    };

    VkSubpassDescription graphicsSubpass{
        0,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        0,
        VK_NULL_HANDLE,
        1,
        &colorAttachmentRef,
        VK_NULL_HANDLE,
        &mainDepthAttachmentRef,
        0,
        VK_NULL_HANDLE
    };

    VkSubpassDependency computeDependency{
        VK_SUBPASS_EXTERNAL,
        0,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT
    };

    VkSubpassDependency mainDepthPrepassDependency{
            VK_SUBPASS_EXTERNAL,
            0,
            VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    std::array attachments = {swapChainImageDescription, mainDepthImageDescription};
    std::array dependencies = {computeDependency, mainDepthPrepassDependency};
    const VkRenderPassCreateInfo renderPassInfo{
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        attachments.size(),
        attachments.data(),
        1,
        &graphicsSubpass,
        dependencies.size(),
        dependencies.data()
    };

    VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, VK_NULL_HANDLE, &renderPass));

    VkAttachmentDescription depthImageDescription{
            0,
            VK_FORMAT_D32_SFLOAT,
            msaaSamples,
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
    };

    VkAttachmentReference depthAttachmentRef{
            0,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    VkSubpassDescription depthPrepassSubpass{
        0,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        0,
        VK_NULL_HANDLE,
        0,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        &depthAttachmentRef,
        0,
        VK_NULL_HANDLE
    };

    VkSubpassDependency depthPrepassDependency{
        VK_SUBPASS_EXTERNAL,
        0,
        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
    };

    VkSubpassDependency depthPrepassPostDependency{
            0,
            VK_SUBPASS_EXTERNAL,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT
    };

    dependencies = {depthPrepassDependency, depthPrepassPostDependency};
    VkRenderPassCreateInfo depthPrepassRenderPassInfo{
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        1,
        &depthImageDescription,
        1,
        &depthPrepassSubpass,
        dependencies.size(),
        dependencies.data()
    };

    VK_CHECK(vkCreateRenderPass(device, &depthPrepassRenderPassInfo, VK_NULL_HANDLE, &depthPrepassRenderPass));
}

void VkRenderer::CreatePipelineLayout() {
    VkPushConstantRange mainPushConstantRange{
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(MeshPushConstants)
    };

    VkPushConstantRange computePushConstantRange{
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(ComputePushConstants)
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        1,
        &sceneDescriptorSetLayout, // it has the same layout
        1,
        &mainPushConstantRange
    };

    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, VK_NULL_HANDLE, &depthPrepassPipelineLayout));

    pipelineLayoutInfo.pSetLayouts = &lightDescriptorSetLayout;
    pipelineLayoutInfo.pPushConstantRanges = &computePushConstantRange;

    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, VK_NULL_HANDLE, &computePipelineLayout));

    pipelineLayoutInfo.pSetLayouts = &frustumDescriptorSetLayout;
    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, VK_NULL_HANDLE, &frustumPipelineLayout));
}

void VkRenderer::CreateGraphicsPipeline() {
    metalRoughMaterial.buildPipelines(this);

    VkGraphicsPipelineBuilder builder;
    builder.SetPipelineLayout(depthPrepassPipelineLayout);
    builder.CreateShaderModules(device, "shaders/depth_prepass.vert.spv", "");
    builder.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    builder.SetPolygonMode(VK_POLYGON_MODE_FILL);
    builder.SetCullingMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    builder.EnableDepthTest(true, VK_COMPARE_OP_LESS);

    depthPrepassPipeline = builder.Build(false, device, pipelineCache, depthPrepassRenderPass);

    builder.DestroyShaderModules(device);
}

void VkRenderer::CreateComputePipeline() {
    auto computeShaderCode = ReadFile<uint32_t>("shaders/light_culling.comp.spv");

    VkShaderModuleCreateInfo computeShaderModuleCreateInfo{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        computeShaderCode.size(),
        computeShaderCode.data()
    };

    VkShaderModule computeShaderModule;

    VK_CHECK(vkCreateShaderModule(device, &computeShaderModuleCreateInfo, VK_NULL_HANDLE, &computeShaderModule));

    VkPipelineShaderStageCreateInfo computeShaderStageInfo{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        VK_SHADER_STAGE_COMPUTE_BIT,
        computeShaderModule,
        "main",
        VK_NULL_HANDLE
    };

    VkComputePipelineCreateInfo pipelineInfo{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        computeShaderStageInfo,
        computePipelineLayout,
        VK_NULL_HANDLE,
        -1
    };

    VK_CHECK(vkCreateComputePipelines(device, pipelineCache, 1, &pipelineInfo, VK_NULL_HANDLE, &computePipeline));

    vkDestroyShaderModule(device, computeShaderModule, VK_NULL_HANDLE);

//    VkShaderModule frustumShaderModule;

    auto frustumShaderCode = ReadFile<uint32_t>("shaders/frustum.comp.spv");

    computeShaderModuleCreateInfo.codeSize = frustumShaderCode.size();
    computeShaderModuleCreateInfo.pCode = frustumShaderCode.data();

    VK_CHECK(vkCreateShaderModule(device, &computeShaderModuleCreateInfo, VK_NULL_HANDLE, &computeShaderModule));

    computeShaderStageInfo.module = computeShaderModule;

    pipelineInfo.stage = computeShaderStageInfo;
    pipelineInfo.layout = frustumPipelineLayout;

    VK_CHECK(vkCreateComputePipelines(device, pipelineCache, 1, &pipelineInfo, VK_NULL_HANDLE, &frustumPipeline));

    vkDestroyShaderModule(device, computeShaderModule, VK_NULL_HANDLE);
}

void VkRenderer::CreateFramebuffers() {
    swapChainFramebuffers.resize(swapChainImageViews.size());
    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
        std::array attachments = {swapChainImageViews[i], depthImage.imageView};
        VkFramebufferCreateInfo framebufferInfo{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            VK_NULL_HANDLE,
            0,
            renderPass,
            attachments.size(),
            attachments.data(),
            swapChainExtent.width,
            swapChainExtent.height,
            1
        };
        VK_CHECK(vkCreateFramebuffer(device, &framebufferInfo, VK_NULL_HANDLE, &swapChainFramebuffers[i]));
    }

    VkFramebufferCreateInfo depthPrepassFramebufferInfo{
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        depthPrepassRenderPass,
        1,
        &depthImage.imageView,
        swapChainExtent.width,
        swapChainExtent.height,
        1
    };

    VK_CHECK(vkCreateFramebuffer(device, &depthPrepassFramebufferInfo, VK_NULL_HANDLE, &depthPrepassFramebuffer));
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
    VkCommandBufferAllocateInfo allocInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            VK_NULL_HANDLE,
            immediateCommandPool,
            VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            1
    };

    VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &depthPrepassCommandBuffer));

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        allocInfo.commandPool = frames[i].commandPool;
        VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &frames[i].commandBuffer));
    }
}

void VkRenderer::CreateDefaultTexture() {
    // const uint32_t purple = packUnorm4x8(glm::vec4{1, 0, 1, 1});

    constexpr uint32_t textureSize = 16;
    std::array<uint32_t, textureSize * textureSize> pixels{};
    std::fill_n(pixels.begin(), pixels.size(), UINT32_MAX);
    // for (size_t i = 0; i < textureSize * textureSize; i++) {
    //     pixels[i] = (i / textureSize + i % textureSize) % 2 == 0 ? 0 : purple;
    // }

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

    if (!asyncCompute)
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, VK_NULL_HANDLE, &computeFinishedSemaphore));

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, VK_NULL_HANDLE, &frames[i].imageAvailableSemaphore));
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, VK_NULL_HANDLE, &frames[i].renderFinishedSemaphore));
        VK_CHECK(vkCreateFence(device, &fenceInfo, VK_NULL_HANDLE, &frames[i].inFlightFence));
    }

    VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, VK_NULL_HANDLE, &depthPrepassSemaphore));
}

void VkRenderer::CreateDescriptors() {
    std::array<DescriptorAllocator::PoolSizeRatio, 2> sizes{
        {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
        }
    };

    mainDescriptorAllocator.InitPool(device, 10, sizes);

    DescriptorLayoutBuilder builder;
    builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    builder.AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);
    builder.AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);
    mainDescriptorSetLayout = builder.Build(device);

    builder.Clear();
    builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
    sceneDescriptorSetLayout = builder.Build(device);

    builder.Clear();
    builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    builder.AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    builder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT);
    builder.AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    lightDescriptorSetLayout = builder.Build(device);

    builder.Clear();
    builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    frustumDescriptorSetLayout = builder.Build(device);

    std::array layouts = {mainDescriptorSetLayout};
    mainDescriptorSet = mainDescriptorAllocator.Allocate(device, layouts);

    layouts = {lightDescriptorSetLayout};
    lightDescriptorSet = mainDescriptorAllocator.Allocate(device, layouts);

    layouts = {sceneDescriptorSetLayout};
    depthPrepassDescriptorSet = mainDescriptorAllocator.Allocate(device, layouts);

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
        std::cout << "Actual extent: " << capabilities.currentExtent.width << " " << capabilities.currentExtent.height << std::endl;
        return capabilities.currentExtent;
    }

    int w, h;
    glfwGetFramebufferSize(glfwWindow, &w, &h);

    const VkExtent2D actualExtent = {
        static_cast<uint32_t>(w),
        static_cast<uint32_t>(h)
    };

    std::cout << "Actual extent: " << actualExtent.width << " " << actualExtent.height << std::endl;

    return {
        std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
    };
}

void VkRenderer::CleanupSwapChain() {
    for (const auto &f: swapChainFramebuffers) {
        vkDestroyFramebuffer(device, f, nullptr);
    }

    vkDestroyFramebuffer(device, depthPrepassFramebuffer, nullptr);

    for (const auto &imageView: swapChainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }

    vkDestroySampler(device, depthSampler, nullptr);
    memoryManager->destroyImage(depthImage);

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

void VkRenderer::SavePipelineCache() const {
    size_t size;
    vkGetPipelineCacheData(device, pipelineCache, &size, nullptr);

    std::vector<uint8_t> data(size);
    vkGetPipelineCacheData(device, pipelineCache, &size, data.data());

    std::ofstream file("pipeline_cache.bin", std::ios::binary);
    file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(size));
}

void VkRenderer::UpdateScene() {
    auto view = camera->ViewMatrix();
    auto proj = camera->ProjectionMatrix();

    sceneData.worldMatrix = proj * view;

    totalLights.lights[0].position = glm::vec4(-9.5f + 0.01f * static_cast<float>(lightSceneTime), 1.4f, 3.4f, 200.f);
    totalLights.lights[1].position = glm::vec4(8.5f, 1.4f, -3.6f + 0.01f * static_cast<float>(lightSceneTime), 200.f);

    void *data;
    memoryManager->mapBuffer(lightUniformBuffer, &data);
    memcpy(data, &totalLights, sizeof(Light));
    memoryManager->unmapBuffer(lightUniformBuffer);

    loadedScenes["structure"]->Draw(glm::mat4{1.f}, mainDrawContext);
}

#ifndef NDEBUG
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

void VkRenderer::CreateRandomLights() {
    totalLights.lights[0].position = glm::vec4(-9.5f, 1.4f, 3.4f, 200.f);
    totalLights.lights[1].position = glm::vec4(8.5f, 1.4f, -3.6f, 200.f);
//    totalLights.lights[1].position = glm::vec4(-9.5f, 1.4f, -3.6f, 200.f);
//    totalLights.lights[2].position = glm::vec4(8.5f, 1.4f, 3.4f, 200.f);
//    totalLights.lights[3].position = glm::vec4(8.5f, 1.4f, -3.6f, 200.f);

    totalLights.lights[0].color = glm::vec4(1.f, 0.0f, 0.0f, 1.f);
    totalLights.lights[1].color = glm::vec4(0.0f, 0.0f, 1.f, 1.f);
//    totalLights.lights[2].color = glm::vec4(1.f, 0.4f, 0.2f, 1.f);
//    totalLights.lights[3].color = glm::vec4(0.2f, 0.4f, 1.f, 1.f);

    totalLights.lightCount = 2;

    lightUniformBuffer = memoryManager->createManagedBuffer(sizeof(Light), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VMA_MEMORY_USAGE_AUTO);

    const auto mappedMemoryTask = [&](auto &, auto *stagingBuffer) {
        memcpy(stagingBuffer, &totalLights, sizeof(Light));
    };

    const auto unmappedMemoryTask = [&](auto &buf) {
        ImmediateSubmit([&](auto &cmd) {
            VkBufferCopy copyRegion{
                0,
                0,
                sizeof(Light)
            };

            vkCmdCopyBuffer(cmd, buf, lightUniformBuffer.buffer, 1, &copyRegion);
        });
    };

    memoryManager->stagingBuffer(sizeof(Light), mappedMemoryTask, unmappedMemoryTask);

    visibleLightBuffer = memoryManager->createManagedBuffer(sizeof(lightVisibility), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    ImmediateSubmit([&](auto &cmd) {
        vkCmdFillBuffer(cmd, visibleLightBuffer.buffer, 0, sizeof(lightVisibility), 0);
    });
}

void VkRenderer::UpdateDepthComputeDescriptorSets() {
    DescriptorWriter writer;
    writer.WriteImage(0, depthImage.imageView, depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.WriteBuffer(1, lightUniformBuffer.buffer, 0, sizeof(Light), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writer.WriteBuffer(2, visibleLightBuffer.buffer, 0, sizeof(lightVisibility), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writer.UpdateSet(device, mainDescriptorSet);

    writer.Clear();
    writer.WriteBuffer(0, lightUniformBuffer.buffer, 0, sizeof(Light), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writer.WriteBuffer(1, visibleLightBuffer.buffer, 0, sizeof(lightVisibility),
                       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writer.WriteImage(2, depthImage.imageView, depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.WriteBuffer(3, frustumBuffer.buffer, 0, sizeof(ViewFrustum), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writer.UpdateSet(device, lightDescriptorSet);
}

void VkRenderer::ComputeFrustum() {
    if (!frustumBuffer.buffer)
        frustumBuffer = memoryManager->createManagedBuffer(sizeof(ViewFrustum), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    std::array layouts = {frustumDescriptorSetLayout};
    auto frustumDescriptorSet = mainDescriptorAllocator.Allocate(device, layouts);

    DescriptorWriter writer;
    writer.WriteBuffer(0, frustumBuffer.buffer, 0, sizeof(ViewFrustum), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writer.UpdateSet(device, frustumDescriptorSet);

    const auto view = camera->ViewMatrix();
    const auto proj = camera->ProjectionMatrix();
    ComputePushConstants pushConstants {
        glm::inverse(view * proj),
        camera->Position(),
        {swapChainExtent.width, swapChainExtent.height}
    };

    ImmediateSubmit([&](auto &cmd) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frustumPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frustumPipelineLayout, 0, 1, &frustumDescriptorSet, 0, VK_NULL_HANDLE);
        vkCmdPushConstants(cmd, frustumPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &pushConstants);
        vkCmdDispatch(cmd, (swapChainExtent.width - 1) / 16 + 1, (swapChainExtent.height - 1) / 16 + 1, 1);
    });
}

#endif
