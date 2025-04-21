#include <set>
#include <algorithm>
#include <random>
#include <ktx.h>
#include <thread>
#include <imgui_impl_vulkan.h>
#include <meshoptimizer.h>

#include "vk_renderer.h"

#include <ranges>

#include "vk/memory/vk_mesh_assets.h"
#include "common/file.h"
#include "vk/vk_gui.h"
#include "vk/vk_pipeline_builder.h"
#include "ext/matrix_transform.hpp"
#include "ext/matrix_clip_space.inl"

#ifdef _WIN32
#include <d3d12.h>
#include <dxgi1_6.h>
#define HrToString(x) std::string("HRESULT: ") + std::to_string(x)
#define ThrowIfFailed(x) do { HRESULT hr = (x); if(FAILED(hr)) { throw std::runtime_error(HrToString(hr)); } } while(0)
#endif

static constexpr uint32_t MAX_MESHLET_PRIMITIVES = 124;
static constexpr uint32_t MAX_MESHLET_VERTICES = 64;

PFN_vkCmdDrawMeshTasksEXT fn_vkCmdDrawMeshTasksEXT = nullptr;

// #ifdef _WIN32
// VkRenderer::VkRenderer(HINSTANCE hinstance, HWND hwnd, Camera *camera, const bool dynamicRendering, const bool asyncCompute, const bool meshShader)
// {
//     InitializeInstance(hinstance, hwnd);
// }
// #else
VkRenderer::VkRenderer(GLFWwindow *window, Camera *camera, const bool dynamicRendering, const bool asyncCompute, const bool meshShader)
    : dynamicRendering(dynamicRendering),
      asyncCompute(asyncCompute),
      meshShader(meshShader),
      glfwWindow(window),
      camera(camera)
{
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

    if (meshShader)
        fn_vkCmdDrawMeshTasksEXT = reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(vkGetInstanceProcAddr(instance, "vkCmdDrawMeshTasksEXT"));

    memoryManager = new VkMemoryManager{this};

    // Reuse pipeline cache
    const auto pipelineCacheData = ReadFile<char>("pipeline_cache.bin");
    if (!pipelineCacheData.empty())
    {
        VkPipelineCacheCreateInfo pipelineCacheCreateInfo{
            VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
            VK_NULL_HANDLE,
            0,
            pipelineCacheData.size(),
            pipelineCacheData.data()
        };

        VK_CHECK(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, VK_NULL_HANDLE, &pipelineCache));
    }
    else
    {
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

    if (!dynamicRendering)
    {
        CreateRenderPass();
    }

    CreateDescriptors();
    CreatePipelineLayout();
    CreateGraphicsPipeline();
    CreateComputePipeline();

    if (!dynamicRendering)
    {
        CreateFramebuffers();
    }

    CreateShadowCascades();

    CreateCommandBuffers();
    CreateDefaultTexture();
    CreateSyncObjects();

    const auto start = std::chrono::high_resolution_clock::now();
    const auto structureFile = LoadGLTF(this, true, "../assets/Sponza/Sponza.gltf", "../assets/Sponza/");
    const auto end = std::chrono::high_resolution_clock::now();

    printf("Loading time: %lld ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

    assert(structureFile.has_value());

    loadedScene = structureFile.value();

    sceneDataBuffer = memoryManager->createManagedBuffer(
        {
            sizeof(SceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        });

    CreateRandomLights();
    CreateSkybox();
    ComputeFrustum();
    UpdateDescriptorSets();

    UpdateCascades();
    isVkRunning = true;
}
// #endif

VkRenderer::~VkRenderer() {
    Shutdown();
}

void VkRenderer::Screenshot() {
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, surfaceFormat.format, &formatProperties);

    bool supportBlit = formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT;

    vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProperties);
    supportBlit = supportBlit && (formatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);

    !supportBlit && printf("Blit not supported, using copy instead");

    VulkanImage srcImage = {
            swapChainImages[currentFrame],
            swapChainImageViews[currentFrame],
            VK_NULL_HANDLE,
            {swapChainExtent.width, swapChainExtent.height, 1},
            surfaceFormat.format,
    };

    VulkanImage dstImage = memoryManager->createUnmanagedImage({
        .imageFormat = VK_FORMAT_R8G8B8A8_UNORM,
        .imageExtent = {swapChainExtent.width, swapChainExtent.height, 1},
        .imageTiling = VK_IMAGE_TILING_LINEAR,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .allocationUsage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    });

    ImmediateSubmit([&](auto &commandBuffer) {
        TransitionImage(commandBuffer, dstImage, VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        TransitionImage(commandBuffer, srcImage, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_MEMORY_READ_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        if (supportBlit) {
            BlitImage(commandBuffer, srcImage, dstImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        } else {
            VkImageCopy2 copyRegion{
                VK_STRUCTURE_TYPE_IMAGE_COPY_2,
                VK_NULL_HANDLE,
                {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                {0, 0, 0},
                {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                {0, 0, 0},
                {swapChainExtent.width, swapChainExtent.height, 1}
            };

            VkCopyImageInfo2 copyImageInfo{
                VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2,
                VK_NULL_HANDLE,
                srcImage.image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                dstImage.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &copyRegion
            };

            vkCmdCopyImage2(commandBuffer, &copyImageInfo);
        }

        TransitionImage(commandBuffer, dstImage, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        TransitionImage(commandBuffer, srcImage, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    });

    VkImageSubresource subresource{VK_IMAGE_ASPECT_COLOR_BIT};
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(device, dstImage.image, &subresource, &layout);

    void *data;
    memoryManager->mapImage(dstImage, &data);
    data = static_cast<uint8_t *>(data) + layout.offset;

    constexpr auto filename = "screenshot.bmp";
    SaveToBitmap(filename, static_cast<char *>(data), swapChainExtent.width, swapChainExtent.height, layout.rowPitch);

    printf("Screenshot saved to %s\n", filename);

    memoryManager->unmapImage(dstImage);
    memoryManager->destroyImage(dstImage);
}

// #ifdef _WIN32
// void VkRenderer::InitializeInstance(HINSTANCE hInstance, HWND hWnd)
// {
//     this->hInstance = hInstance;
//     this->hWnd = hWnd;
// }
//
// #else
void VkRenderer::InitializeInstance() {
    constexpr VkApplicationInfo appInfo{
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        VK_NULL_HANDLE,
        "Vulkan Stuff",
        VK_MAKE_VERSION(1, 0, 0),
        "Vulkan Stuff",
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
    for (const char *validationLayerName : validationLayers) {
        for (const auto &[layerName, specVersion, implementationVersion, description] : availableLayers) {
            if (strcmp(validationLayerName, layerName) == 0) {
                layerFound = true;
                break;
            }
        }
    }

    if (!layerFound) [[unlikely]] {
        throw std::runtime_error("validation layers requested, but not available!");
    }

    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

#ifndef NDEBUG
    constexpr auto validationSize = static_cast<uint32_t>(validationLayers.size());
    constexpr auto validationData = validationLayers.data();
#else
    constexpr uint32_t validationSize = 0;
    constexpr auto validationData = VK_NULL_HANDLE;
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
// #endif

bool isObjectVisible(const VkRenderObject &obj, const glm::mat4 &viewProjection) {
    static constexpr std::array corners{
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

// static std::vector<size_t> drawIndices;

uint16_t VkRenderer::GetFPSLimit() const
{
    return 1000 / fpsLimit;
}

void VkRenderer::SetFPSLimit(const uint16_t fps)
{
    fpsLimit = 1000 / fps;
}

void VkRenderer::Render(EngineStats &stats) {
    stats.drawCallCount = 0;
    stats.triangleCount = 0;

    UpdateScene(stats);

    static std::array<VkFence, 3> fences;
    fences[0] = frames[currentFrame].inFlightFence;

    if (meshShader)
    {
        fences[1] = computeFinishedFence;
    }
    else
    {
        fences[1] = depthPrepassFence;
        fences[2] = computeFinishedFence;
    }

    const auto size = fences.size() - !asyncCompute - meshShader;
    VK_CHECK(vkWaitForFences(device, size, fences.data(), VK_TRUE, UINT64_MAX));

    uint32_t imageIndex;
    auto result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, frames[currentFrame].imageAvailableSemaphore,
                                        VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        framebufferResized = true;
        return;
    }

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) [[unlikely]] {
        throw std::runtime_error("Failed to acquire swap chain image!");
    }

    VK_CHECK(vkResetFences(device, size, fences.data()));
    VK_CHECK(vkResetCommandBuffer(frames[currentFrame].commandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT));
    VK_CHECK(vkResetCommandBuffer(depthPrepassCommandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT));

    if (asyncCompute) {
        VK_CHECK(vkResetCommandBuffer(computeCommandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT));

        static constexpr VkCommandBufferBeginInfo beginInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            VK_NULL_HANDLE,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };

        VK_CHECK(vkBeginCommandBuffer(computeCommandBuffer, &beginInfo));

        vkCmdBindPipeline(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
        vkCmdBindDescriptorSets(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &mainDescriptorSet,0, VK_NULL_HANDLE);

        ComputePushConstants pushConstants{
                camera->ViewMatrix()
        };

        vkCmdPushConstants(computeCommandBuffer, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants),&pushConstants);
        vkCmdDispatch(computeCommandBuffer, swapChainExtent.width / 32, swapChainExtent.height / 32, 1);

        VK_CHECK(vkEndCommandBuffer(computeCommandBuffer));

        VkSubmitInfo submitInfo{
                VK_STRUCTURE_TYPE_SUBMIT_INFO,
                VK_NULL_HANDLE,
                0,
                VK_NULL_HANDLE,
                VK_NULL_HANDLE,
                1,
                &computeCommandBuffer,
                1,
                &computeFinishedSemaphore
        };

        VK_CHECK(vkQueueSubmit(computeQueue, 1, &submitInfo, computeFinishedFence));
    }

    const auto start = std::chrono::high_resolution_clock::now();

    // drawIndices.reserve(mainDrawContext.opaqueSurfaces.size());
    // for (size_t i = 0; i < mainDrawContext.opaqueSurfaces.size(); i++) {
    //     if (isObjectVisible(mainDrawContext.opaqueSurfaces[i], sceneData.worldMatrix))
    //         drawIndices.push_back(i);
    // }
    //
    // std::ranges::sort(drawIndices, [&](const uint16_t &a, const uint16_t &b) {
    //     const auto &surfaceA = mainDrawContext.opaqueSurfaces[a];
    //     const auto &surfaceB = mainDrawContext.opaqueSurfaces[b];
    //     if (surfaceA.materialInstance == surfaceB.materialInstance) {
    //         return surfaceA.indexBuffer < surfaceB.indexBuffer;
    //     }
    //
    //     return surfaceA.materialInstance < surfaceB.materialInstance;
    // });

    if (meshShader) {
        DrawMesh(frames[currentFrame].commandBuffer, imageIndex, stats);
    } else {
        DrawDepthPrepass(/*drawIndices*/);
        Draw(frames[currentFrame].commandBuffer, imageIndex, stats);
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    stats.meshDrawTime = static_cast<float>(elapsed) / 1000.f;

    static constexpr VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT};
    static std::array<VkSemaphore, 3> waitSemaphores;
    waitSemaphores[0] = frames[currentFrame].imageAvailableSemaphore;
    if (meshShader)
    {
        waitSemaphores[1] = computeFinishedSemaphore;
    }
    else
    {
        waitSemaphores[1] = depthPrepassSemaphore;
        waitSemaphores[2] = computeFinishedSemaphore;
    }

    VkSubmitInfo submitInfo{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        VK_NULL_HANDLE,
        static_cast<uint32_t>(waitSemaphores.size() - !asyncCompute - meshShader),
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
    } else if (result != VK_SUCCESS) [[unlikely]] {
        throw std::runtime_error("Failed to acquire swap chain image!");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

    // std::this_thread::sleep_for(std::chrono::milliseconds(fpsLimit));
    // Sleep(fpsLimit);
}

void VkRenderer::DrawObject(const VkCommandBuffer &commandBuffer, const VkRenderObject &draw, VkMaterialPipeline &lastPipeline, VkMaterialInstance &lastMaterialInstance, VkBuffer &lastIndexBuffer) {
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
            camera->position,
            {viewport.width, viewport.height},
            cascadeSplits.vec4
    };

    vkCmdPushConstants(commandBuffer, draw.materialInstance->pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &pushConstants);
    vkCmdPushConstants(commandBuffer, draw.materialInstance->pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(MeshPushConstants), sizeof(FragmentPushConstants), &fragmentPushConstants);
    vkCmdDrawIndexed(commandBuffer, draw.indexCount, 1, draw.firstIndex, 0, 0);
}

void VkRenderer::DrawDepthPrepass(/*const std::vector<size_t> &drawIndices*/) {
    static constexpr VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, VK_NULL_HANDLE, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};

    VK_CHECK(vkBeginCommandBuffer(depthPrepassCommandBuffer, &beginInfo));

    static constexpr VkClearValue depthPrepassClearValue{
        .depthStencil = {1.0f, 0}
    };

    static constexpr VkViewport depthViewport{
        0.0f,
        0.0f,
        static_cast<float>(SHADOW_MAP_SIZE),
        static_cast<float>(SHADOW_MAP_SIZE),
        0.0f,
        1.0f
    };

    static constexpr VkRect2D depthScissor{
        {0, 0},
        {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}
    };

    vkCmdSetViewport(depthPrepassCommandBuffer, 0, 1, &depthViewport);
    vkCmdSetScissor(depthPrepassCommandBuffer, 0, 1, &depthScissor);

    VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

    TransitionImage(depthPrepassCommandBuffer, shadowCascadeImage, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, 0, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, -1, SHADOW_MAP_CASCADE_COUNT);

    vkCmdBindPipeline(depthPrepassCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, depthPrepassPipeline);
    VkRenderingAttachmentInfo attachmentInfo{
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_RESOLVE_MODE_NONE,
        VK_NULL_HANDLE,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_ATTACHMENT_LOAD_OP_CLEAR,
        VK_ATTACHMENT_STORE_OP_STORE,
        depthPrepassClearValue
    };

    VkRenderingInfo renderingInfo{
        VK_STRUCTURE_TYPE_RENDERING_INFO,
        VK_NULL_HANDLE,
        {},
        {0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE},
        1,
        0,
        0,
        VK_NULL_HANDLE,
        &attachmentInfo
    };

    for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; i++) {
        if (dynamicRendering) {
            attachmentInfo.imageView = shadowCascades[i].shadowImageView;
            vkCmdBeginRendering(depthPrepassCommandBuffer, &renderingInfo);
        } else {
            VkRenderPassBeginInfo renderPassInfo{
                    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                    VK_NULL_HANDLE,
                    depthPrepassRenderPass,
                    shadowCascades[i].shadowMapFramebuffer,
                    {{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}},
                    1,
                    &depthPrepassClearValue
            };

            vkCmdBeginRenderPass(depthPrepassCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        }

        vkCmdBindDescriptorSets(depthPrepassCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, depthPrepassPipelineLayout, 0, 1, &sceneDescriptorSet, 0, VK_NULL_HANDLE);

        for (const auto &draw : mainDrawContext.opaqueSurfaces) {
            if (draw.indexBuffer != lastIndexBuffer) {
                lastIndexBuffer = draw.indexBuffer;
                vkCmdBindIndexBuffer(depthPrepassCommandBuffer, draw.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            }

            DepthPassPushConstants depthPushConstants{
                draw.vertexBufferAddress,
                i
            };

            vkCmdPushConstants(depthPrepassCommandBuffer, depthPrepassPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(DepthPassPushConstants), &depthPushConstants);
            vkCmdDrawIndexed(depthPrepassCommandBuffer, draw.indexCount, 1, draw.firstIndex, 0, 0);
        }

        for (const auto &r : std::ranges::reverse_view(mainDrawContext.transparentSurfaces)) {
            if (r.indexBuffer != lastIndexBuffer) {
                lastIndexBuffer = r.indexBuffer;
                vkCmdBindIndexBuffer(depthPrepassCommandBuffer, r.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            }

            DepthPassPushConstants depthPushConstants{
                r.vertexBufferAddress,
                i
            };

            vkCmdPushConstants(depthPrepassCommandBuffer, depthPrepassPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(DepthPassPushConstants), &depthPushConstants);
            vkCmdDrawIndexed(depthPrepassCommandBuffer, r.indexCount, 1, r.firstIndex, 0, 0);
        }

        if (dynamicRendering) {
            vkCmdEndRendering(depthPrepassCommandBuffer);
        } else {
            vkCmdEndRenderPass(depthPrepassCommandBuffer);
        }
    }

    TransitionImage(depthPrepassCommandBuffer, shadowCascadeImage, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, -1, SHADOW_MAP_CASCADE_COUNT);
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

    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, depthPrepassFence));
}

void VkRenderer::DrawSkybox(const VkCommandBuffer &commandBuffer, EngineStats &stats) const {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipelineLayout, 0, 1, &skyboxDescriptorSet, 0, VK_NULL_HANDLE);
    const struct {
        alignas(16) glm::vec3 front;
        alignas(16) glm::vec3 right;
        alignas(16) glm::vec3 up;
    } skyboxPushConstant{
        camera->front,
        camera->right,
        camera->up
    };
    vkCmdPushConstants(commandBuffer, skyboxPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec4) * 3, &skyboxPushConstant);
    vkCmdDraw(commandBuffer, 6, 1, 0, 0);
    stats.drawCallCount++;
    stats.triangleCount += 12;
}

void VkRenderer::BeginDraw(const VkCommandBuffer &commandBuffer, const uint32_t imageIndex) const {
    static constexpr VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, VK_NULL_HANDLE, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    if (!asyncCompute) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &mainDescriptorSet, 0, VK_NULL_HANDLE);

        ComputePushConstants pushConstants{
                camera->ViewMatrix()
        };

        vkCmdPushConstants(commandBuffer, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants),&pushConstants);
        vkCmdDispatch(commandBuffer, swapChainExtent.width / 32, swapChainExtent.height / 32, 1);
    }

    VulkanImage swapChainImage{swapChainImages[imageIndex], swapChainImageViews[imageIndex], VK_NULL_HANDLE, {swapChainExtent.width, swapChainExtent.height, 1}, surfaceFormat.format};

    if (dynamicRendering) {
        TransitionImage(commandBuffer, swapChainImage, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        TransitionImage(commandBuffer, depthImage, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, 0, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        VkRenderingAttachmentInfo colorAttachment{
            VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            VK_NULL_HANDLE,
            swapChainImageViews[imageIndex],
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_RESOLVE_MODE_NONE,
            VK_NULL_HANDLE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_STORE_OP_STORE,
            {0.0f, 0.0f, 0.0f, 1.0f}
        };

        VkRenderingAttachmentInfo depthAttachment{
            VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            VK_NULL_HANDLE,
            depthImage.imageView,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_RESOLVE_MODE_NONE,
            VK_NULL_HANDLE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_STORE_OP_STORE,
            {.depthStencil = {1.0f, 0}}
        };

        VkRenderingInfo renderingInfo{
            VK_STRUCTURE_TYPE_RENDERING_INFO,
            VK_NULL_HANDLE,
            {},
            {0, 0, swapChainExtent.width, swapChainExtent.height},
            1,
            0,
            1,
            &colorAttachment,
            &depthAttachment
        };

        vkCmdBeginRendering(commandBuffer, &renderingInfo);
    } else {
        static constexpr std::array<VkClearValue, 2> clearValues{
                {
                        {.color = {0.0f, 0.0f, 0.0f, 1.0f}},
                        {.depthStencil = {1.0f, 0}}
                }
        };
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
}

void VkRenderer::EndDraw(const VkCommandBuffer &commandBuffer, const uint32_t imageIndex) {
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    const VulkanImage swapChainImage{swapChainImages[imageIndex], swapChainImageViews[imageIndex], VK_NULL_HANDLE, {swapChainExtent.width, swapChainExtent.height, 1}, surfaceFormat.format};
    if (dynamicRendering) {
        vkCmdEndRendering(commandBuffer);
        TransitionImage(commandBuffer, swapChainImage, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    } else {
        vkCmdEndRenderPass(commandBuffer);
    }

    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    mainDrawContext.opaqueSurfaces.clear();
    mainDrawContext.transparentSurfaces.clear();
    // drawIndices.clear();
}

void VkRenderer::Draw(const VkCommandBuffer &commandBuffer, uint32_t imageIndex, EngineStats &stats) {
    BeginDraw(commandBuffer, imageIndex);
    DrawSkybox(commandBuffer, stats);

    VkMaterialPipeline lastPipeline{};
    VkMaterialInstance lastMaterialInstance{};
    VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

    for (const auto &draw : mainDrawContext.opaqueSurfaces) {
        DrawObject(commandBuffer, draw, lastPipeline, lastMaterialInstance, lastIndexBuffer);
        stats.drawCallCount++;
        stats.triangleCount += draw.indexCount / 3;
    }

    for (const auto &r : std::ranges::reverse_view(mainDrawContext.transparentSurfaces)) {
        DrawObject(commandBuffer, r, lastPipeline, lastMaterialInstance, lastIndexBuffer);
        stats.drawCallCount++;
        stats.triangleCount += r.indexCount / 3;
    }

    EndDraw(commandBuffer, imageIndex);
}

void VkRenderer::DrawMesh(const VkCommandBuffer &commandBuffer, const uint32_t imageIndex, EngineStats &stats) {
    BeginDraw(commandBuffer, imageIndex);
    DrawSkybox(commandBuffer, stats);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, metalRoughMaterial.opaquePipeline.pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, metalRoughMaterial.opaquePipeline.layout, 0, 1, &sceneDescriptorSet, 0, VK_NULL_HANDLE);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, metalRoughMaterial.opaquePipeline.layout, 2, 1, &mainDescriptorSet, 0, VK_NULL_HANDLE);

    MeshShaderPushConstants pushConstants{
        loadedScene.rootNodes[0]->worldTransform,
    };

    vkCmdPushConstants(commandBuffer, metalRoughMaterial.opaquePipeline.layout, VK_SHADER_STAGE_MESH_BIT_EXT, 0, sizeof(MeshShaderPushConstants), &pushConstants);
    fn_vkCmdDrawMeshTasksEXT(commandBuffer, meshletStats.meshletCount / 32 + 1, 1, 1);
    stats.drawCallCount++;
    stats.triangleCount += meshletStats.primitiveCount;

    EndDraw(commandBuffer, imageIndex);
}

void VkRenderer::DrawIndirect(VkCommandBuffer const &commandBuffer, uint32_t imageIndex, EngineStats &stats) {

}

void VkRenderer::Shutdown() {
    if (!isVkRunning)
        return;

    VK_CHECK(vkDeviceWaitIdle(device));

    loadedScene.Clear(this);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(device, frames[i].renderFinishedSemaphore, nullptr);
        vkDestroySemaphore(device, frames[i].imageAvailableSemaphore, nullptr);
        vkDestroyFence(device, frames[i].inFlightFence, nullptr);

        vkDestroyCommandPool(device, frames[i].commandPool, nullptr);
        frames[i].frameDescriptors.Destroy(device);
    }

    if (asyncCompute) {
        vkDestroySemaphore(device, computeFinishedSemaphore, nullptr);
        vkDestroyFence(device, computeFinishedFence, nullptr);
        vkDestroyCommandPool(device, computeCommandPool, nullptr);
    }

    vkDestroySemaphore(device, depthPrepassSemaphore, nullptr);
    vkDestroyFence(device, depthPrepassFence, nullptr);

    vkDestroyCommandPool(device, graphicsCommandPool, nullptr);
    vkDestroyCommandPool(device, transferCommandPool, nullptr);

    for (const auto &shadowCascade : shadowCascades)
    {
        vkDestroyImageView(device, shadowCascade.shadowImageView, nullptr);
    }

    CleanupSwapChain();

    vkDestroySampler(device, textureSamplerLinear, VK_NULL_HANDLE);
    vkDestroySampler(device, textureSamplerNearest, VK_NULL_HANDLE);

    delete memoryManager;
    delete totalLights;

    mainDescriptorAllocator.Destroy(device);
    vkDestroyDescriptorSetLayout(device, mainDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, sceneDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, frustumDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, skyboxDescriptorSetLayout, nullptr);

    vkDestroyPipeline(device, depthPrepassPipeline, nullptr);
    vkDestroyPipeline(device, shadowMapPipeline, nullptr);
    vkDestroyPipeline(device, computePipeline, nullptr);
    vkDestroyPipeline(device, frustumPipeline, nullptr);
    vkDestroyPipeline(device, skyboxPipeline, nullptr);
    vkDestroyPipelineLayout(device, depthPrepassPipelineLayout, nullptr);
    vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);
    vkDestroyPipelineLayout(device, frustumPipelineLayout, nullptr);
    vkDestroyPipelineLayout(device, skyboxPipelineLayout, nullptr);

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
        UpdateDescriptorSets();

        viewport.width = static_cast<float>(swapChainExtent.width);
        viewport.height = static_cast<float>(swapChainExtent.height);
        scissor.extent = swapChainExtent;
    }

    framebufferResized = false;
}

Mesh VkRenderer::CreateMesh(const std::span<VkVertex> vertices, const std::span<uint32_t>indices) const {
    Mesh mesh{};

    const auto verticesSize = vertices.size() * sizeof(vertices[0]);
    const auto indicesSize = indices.size() * sizeof(indices[0]);
    const auto stagingBufferMappedTask = [&](auto &, void *mappedMemory) {
        memcpy(mappedMemory, vertices.data(), verticesSize);
        memcpy(static_cast<char *>(mappedMemory) + verticesSize, indices.data(), indicesSize);
    };

    const auto stagingBufferUnmappedTask = [&](const VkBuffer &stagingBuffer) {
        mesh.vertexBuffer = memoryManager->createManagedBuffer({verticesSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                                                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                                                0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT}).buffer;
        const VkBufferDeviceAddressInfo deviceAddressInfo{
            VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            VK_NULL_HANDLE,
            mesh.vertexBuffer
        };
        mesh.vertexBufferDeviceAddress = vkGetBufferDeviceAddress(device, &deviceAddressInfo);
        mesh.indexBuffer = memoryManager->createManagedBuffer(
                {indicesSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 0,
                 VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT}).buffer;

        TransferSubmit([&](auto &commandBuffer) {
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

void VkRenderer::CreateFromMeshlets(const std::vector<VkVertex> &vertices, const std::vector<uint32_t> &indices) {
    std::vector<meshopt_Meshlet> meshlets;
    std::vector<uint32_t> meshletVertices;
    std::vector<uint8_t> meshletPrimitives;

    const auto maxMeshlets = meshopt_buildMeshletsBound(indices.size(), MAX_MESHLET_VERTICES, MAX_MESHLET_PRIMITIVES);

    meshlets.resize(maxMeshlets);
    meshletVertices.resize(maxMeshlets * MAX_MESHLET_VERTICES);
    meshletPrimitives.resize(maxMeshlets * MAX_MESHLET_PRIMITIVES);

    printf("Resizing vertex position data: %llu\n", vertices.size() * 4);
    std::vector<float> vertexPositionData(vertices.size() * 4);
    // vertexPositionData.resize(vertices.size() * 3);

    for (size_t i = 0; i < vertices.size(); i += 2)
    {
        if (i + 1 < vertices.size()) [[likely]] {
            // ReSharper disable CppCStyleCast
            const auto ymm = _mm256_loadu2_m128((float *) &vertices[i + 1].pos, (float *) &vertices[i].pos);
            _mm256_storeu_ps(vertexPositionData.data() + i * 4, ymm);
        }
        else
        {
            const auto xmm = _mm_load_ps((float *) &vertices[i].pos);
            _mm_storeu_ps(vertexPositionData.data() + i * 4, xmm);
            // ReSharper restore CppCStyleCast
        }
    }

    constexpr float coneWeight = 0.0f;
    const auto meshletCount = meshopt_buildMeshlets(meshlets.data(), meshletVertices.data(), meshletPrimitives.data(),
                                              indices.data(), indices.size(), vertexPositionData.data(),
                                              vertices.size(), sizeof(glm::vec4), MAX_MESHLET_VERTICES,
                                              MAX_MESHLET_PRIMITIVES, coneWeight);

    const auto &[vertex_offset, triangle_offset, vertex_count, triangle_count] = meshlets[meshletCount - 1];
    meshletVertices.resize(vertex_offset + vertex_count);
    meshletPrimitives.resize(triangle_offset + triangle_count * 3);
    meshlets.resize(meshletCount);

    std::vector<uint32_t> meshletPrimitivesU32;
    for (auto &[vertex_offset, triangle_offset, vertex_count, triangle_count] : meshlets) {
        const auto primitiveOffset = static_cast<uint32_t>(meshletPrimitivesU32.size());

        for (uint32_t i = 0; i < triangle_count; i++) {
            const auto i1 = i * 3 + 0 + triangle_offset;
            const auto i2 = i * 3 + 1 + triangle_offset;
            const auto i3 = i * 3 + 2 + triangle_offset;

            // const auto vIdx0 = meshletVertices[i1];
            // const auto vIdx1 = meshletVertices[i2];
            // const auto vIdx2 = meshletVertices[i3];
            const auto vIdx0 = meshletPrimitives[i1];
            const auto vIdx1 = meshletPrimitives[i2];
            const auto vIdx2 = meshletPrimitives[i3];

            const auto packed = vIdx0 & 0xFF | (vIdx1 & 0xFF) << 8 | (vIdx2 & 0xFF) << 16;

            meshletPrimitivesU32.push_back(packed);
        }

        triangle_offset = primitiveOffset;
    }

    meshletStats.positionCount = vertexPositionData.size();
    meshletStats.meshletCount = meshletCount;
    meshletStats.verticesCount = meshletVertices.size();
    meshletStats.primitiveCount = meshletPrimitivesU32.size();

    const auto vertexPositionDataBytes = vertexPositionData.size() * sizeof(float);
    const auto meshletBufferBytes = meshlets.size() * sizeof(meshopt_Meshlet);
    const auto meshletVerticesBytes = meshletVertices.size() * sizeof(uint32_t);
    const auto meshletPrimitivesBytes = meshletPrimitivesU32.size() * sizeof(uint32_t);

    positionBuffer = memoryManager->createManagedBuffer({vertexPositionDataBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT});
    meshletBuffer = memoryManager->createManagedBuffer({meshletBufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT});
    meshletVerticesBuffer = memoryManager->createManagedBuffer({meshletVerticesBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT});
    meshletPrimitivesBuffer = memoryManager->createManagedBuffer({meshletPrimitivesBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT});

    const auto stagingBufferSize = vertexPositionDataBytes
                                                + meshletBufferBytes
                                                + meshletVerticesBytes
                                                + meshletPrimitivesBytes;

    const auto stagingBufferMappedTask = [&](auto &, void *data) {
        auto memory = static_cast<char *>(data);
        memcpy(memory, vertexPositionData.data(), vertexPositionDataBytes);

        memory += vertexPositionDataBytes;
        memcpy(memory, meshlets.data(), meshletBufferBytes);

        memory += meshletBufferBytes;
        memcpy(memory, meshletVertices.data(), meshletVerticesBytes);

        memory += meshletVerticesBytes;
        memcpy(memory, meshletPrimitivesU32.data(), meshletPrimitivesBytes);
    };

    const auto stagingBufferUnmappedTask = [&](auto &stagingBuffer) {
        TransferSubmit([&](auto &commandBuffer) {
            VkBufferCopy positionCopyRegion{
                0,
                0,
                vertexPositionDataBytes
            };

            VkBufferCopy meshletCopyRegion{
                vertexPositionDataBytes,
                0,
                meshletBufferBytes
            };

            VkBufferCopy meshletVerticesCopyRegion{
                vertexPositionDataBytes + meshletBufferBytes,
                0,
                meshletVerticesBytes
            };

            VkBufferCopy meshletPrimitivesCopyRegion{
                vertexPositionDataBytes + meshletBufferBytes + meshletVerticesBytes,
                0,
                meshletPrimitivesBytes
            };

            vkCmdCopyBuffer(commandBuffer, stagingBuffer, positionBuffer.buffer, 1, &positionCopyRegion);
            vkCmdCopyBuffer(commandBuffer, stagingBuffer, meshletBuffer.buffer, 1, &meshletCopyRegion);
            vkCmdCopyBuffer(commandBuffer, stagingBuffer, meshletVerticesBuffer.buffer, 1, &meshletVerticesCopyRegion);
            vkCmdCopyBuffer(commandBuffer, stagingBuffer, meshletPrimitivesBuffer.buffer, 1, &meshletPrimitivesCopyRegion);
        });
    };

    printf("Creating staging buffer of size: %llu\n", stagingBufferSize);
    memoryManager->stagingBuffer(stagingBufferSize, stagingBufferMappedTask, stagingBufferUnmappedTask);
}

void VkRenderer::PickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, VK_NULL_HANDLE);
    if (deviceCount == 0) [[unlikely]] {
        throw std::runtime_error("Failed to find GPUs with Vulkan support");
    }

    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());

#ifdef _WIN32

    {
        ComPtr<IDXGIFactory6> dxgiFactory;
        ComPtr<IDXGIAdapter1> dxgiAdapter;

        ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory)));
        ThrowIfFailed(dxgiFactory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&dxgiAdapter)));

        DXGI_ADAPTER_DESC1 desc;
        dxgiAdapter->GetDesc1(&desc);

        for (const auto &gpu : physicalDevices) {
            VkPhysicalDeviceIDProperties idProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES, nullptr};
            VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &idProperties};
            vkGetPhysicalDeviceProperties2(gpu, &properties2);

            // const uint64_t dxgiLUID = *reinterpret_cast<uint64_t *>(&desc.AdapterLuid);
            // const uint64_t deviceLUID = *reinterpret_cast<uint64_t *>(&idProperties.deviceLUID);

            if (memcmp(&desc.AdapterLuid, &idProperties.deviceLUID, sizeof(desc.AdapterLuid)) == 0) {
                physicalDevice = gpu;
                deviceProperties = properties2.properties;
                break;
            }
        }

        ThrowIfFailed(D3D12CreateDevice(dxgiAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d3dDevice)));
    }
#else
    physicalDevice = physicalDevices[0];
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);

    isIntegratedGPU = deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
#endif

    // uint32_t maxScore = 0;
    // for (const auto &gpu: physicalDevices) {
    //     uint32_t score = 0;
    //     vkGetPhysicalDeviceProperties(gpu, &deviceProperties);
    //
    //     if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
    //         score += 1000;
    //     }
    //
    //     score += deviceProperties.limits.maxImageDimension2D;
    //
    //     if (score > maxScore) {
    //         maxScore = score;
    //         physicalDevice = gpu;
    //         isIntegratedGPU = deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    //     }
    // }

    printf("Using device: %256s\n", deviceProperties.deviceName);

    VkPhysicalDeviceMeshShaderPropertiesEXT meshShaderProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT};
    VkPhysicalDeviceMaintenance3Properties maintenance3Properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES, &meshShaderProperties};
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR raytracingProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR, &maintenance3Properties};

    VkPhysicalDeviceProperties2 deviceProperties2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        &raytracingProperties,
        deviceProperties
    };

    vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties2);
    maxMemoryAllocationSize = maintenance3Properties.maxMemoryAllocationSize;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    raytracingCapable = raytracingProperties.shaderGroupHandleSize > 0;
    // Workaround for Intel Mesa drivers
    const auto isIntelIGPU = isIntegratedGPU && strncmp(deviceProperties.deviceName, "Intel(R)", 8) == 0;
    meshShader = meshShader && !isIntelIGPU && meshShaderProperties.maxMeshWorkGroupCount[0] > 0;

    !meshShader && printf("Mesh shader not supported or turned off, falling back to VTG rendering\n");

    raytracingCapable = !isIntelIGPU && raytracingProperties.shaderGroupHandleSize > 0;
}

void VkRenderer::CreateLogicalDevice() {
    FindQueueFamilies(physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set uniqueQueueFamilies = {
        queueFamilyIndices.graphicsFamily.value(), queueFamilyIndices.transferFamily.value(), queueFamilyIndices.computeFamily.value(), queueFamilyIndices.presentFamily.value()
    };
    size_t queueCount = uniqueQueueFamilies.size();
    queueCreateInfos.reserve(queueCount);

    auto *queuePriorities = new float[queueCount];
    for (size_t i = 0; i < queueCount; i++) {
        queuePriorities[i] = 1.0f;
    }

    for (uint32_t queueFamily : uniqueQueueFamilies) {
        queueCreateInfos.emplace_back(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,VK_NULL_HANDLE, 0, queueFamily, 1,
                                      queuePriorities);
    }

    VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
        VK_NULL_HANDLE,
        meshShader,
        meshShader
    };

    VkPhysicalDeviceVulkan11Features vulkan11Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = meshShader ? &meshShaderFeatures : VK_NULL_HANDLE,
        .storageBuffer16BitAccess = VK_TRUE,
        .uniformAndStorageBuffer16BitAccess = VK_TRUE
    };

    VkPhysicalDeviceVulkan12Features vulkan12Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &vulkan11Features,
        .shaderFloat16 = VK_TRUE,
        .descriptorIndexing = VK_TRUE,
        .bufferDeviceAddress = VK_TRUE,
    };

    VkPhysicalDeviceVulkan13Features vulkan13Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &vulkan12Features,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };

    VkPhysicalDeviceFeatures2 deviceFeatures2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        &vulkan13Features,
        {.multiDrawIndirect = VK_TRUE, .depthClamp = VK_TRUE, .samplerAnisotropy = VK_TRUE, .shaderInt16 = VK_TRUE }
    };

    const char * deviceExtensions[9] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
        VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_EXT_MESH_SHADER_EXTENSION_NAME
    };

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
        meshShaderFeatures.pNext = &rayTracingPipelineFeatures;
    }

    VkDeviceCreateInfo createInfo{
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        &deviceFeatures2,
        0,
        static_cast<uint32_t>(queueCreateInfos.size()),
        queueCreateInfos.data(),
        0,
        VK_NULL_HANDLE,
        static_cast<uint32_t>(9 - !raytracingCapable * 4 - !meshShader),
        deviceExtensions
    };

#ifndef NDEBUG
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
#endif

    VK_CHECK(vkCreateDevice(physicalDevice, &createInfo, VK_NULL_HANDLE, &device));

    vkGetDeviceQueue(device, queueFamilyIndices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, queueFamilyIndices.transferFamily.value(), 0, &transferQueue);
    if (asyncCompute)
        vkGetDeviceQueue(device, queueFamilyIndices.computeFamily.value(), 0, &computeQueue);

    vkGetDeviceQueue(device, queueFamilyIndices.presentFamily.value(), 0, &presentQueue);

    delete[] queuePriorities;
}

void VkRenderer::CreatePipelineCache() {
    constexpr VkPipelineCacheCreateInfo pipelineCacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    VK_CHECK(vkCreatePipelineCache(device, &pipelineCacheInfo, VK_NULL_HANDLE, &pipelineCache));
}

void VkRenderer::CreateSwapChain() {
    const auto &[capabilities, formats, presentModes] = QuerySwapChainSupport(physicalDevice);

    surfaceFormat = ChooseSwapSurfaceFormat(formats);
    presentMode = ChooseSwapPresentMode(presentModes);
    swapChainExtent = ChooseSwapExtent(capabilities);

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

#ifdef _WIN32
    {
        ComPtr<IDXGIFactory6> dxgiFactory;
        ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory)));

        ComPtr<ID3D12CommandQueue> d3dQueue;
        D3D12_COMMAND_QUEUE_DESC queueDesc{
            .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
            .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
        };

        ThrowIfFailed(d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&d3dQueue)));

        ComPtr<IDXGISwapChain1> dxgiSwapChain;
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc{
            .Width = swapChainExtent.width,
            .Height = swapChainExtent.height,
            .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
            .SampleDesc = {.Count = 1},
            .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
            .BufferCount = imageCount,
            .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        };

        auto hwnd = glfwGetWin32Window(glfwWindow);
        ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(d3dQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, &dxgiSwapChain));
        ThrowIfFailed(dxgiSwapChain.As(&d3dSwapChain));
        ThrowIfFailed(dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));
    }

    {
        ComPtr<ID3D12Resource> d3dFramebuffers[imageCount]{};
        for (uint32_t i = 0; i < imageCount; i++) {
            ThrowIfFailed(d3dSwapChain->GetBuffer(i, IID_PPV_ARGS(&d3dFramebuffers[i])));
        }

        static constexpr VkExternalMemoryImageCreateInfo externalMemoryImageCreateInfo{
            VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
            VK_NULL_HANDLE,
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,
        };

        // static constexpr VkImageCreateInfo imageInfo{
        //     VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        //     &externalMemoryImageCreateInfo,
        //     0,
        //     VK_IMAGE_TYPE_2D,
        //     VK_FORMAT_B8G8R8A8_UNORM,
        //     {swapChainExtent.width, swapChainExtent.height, 1},
        //     1,
        //     1,
        //     VK_SAMPLE_COUNT_1_BIT,
        //     VK_IMAGE_TILING_OPTIMAL,
        //     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        //     VK_SHARING_MODE_EXCLUSIVE,
        //     0,
        //     VK_NULL_HANDLE,
        //     VK_IMAGE_LAYOUT_UNDEFINED,
        // };

        for (uint32_t i = 0; i < imageCount; i++) {
            memoryManager->createExternalImage(imageInfo, d3dFramebuffers[i].Get());
        }
    }
#else
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

    if (queueFamilyIndices.graphicsFamily != queueFamilyIndices.presentFamily) {
        const uint32_t qfi[] = {queueFamilyIndices.graphicsFamily.value(), queueFamilyIndices.presentFamily.value()};

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
#endif
}

void VkRenderer::CreateDepthImage() {
    ImageViewCreateInfo viewCreateInfo{
        .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        .format = VK_FORMAT_D16_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, SHADOW_MAP_CASCADE_COUNT}
    };

    shadowCascadeImage = memoryManager->createUnmanagedImage(
            {0, VK_FORMAT_D16_UNORM, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1}, VK_IMAGE_TILING_OPTIMAL,
             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
             VK_IMAGE_LAYOUT_UNDEFINED, 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
             false, &viewCreateInfo});

    viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCreateInfo.subresourceRange.layerCount = 1;

    depthImage = memoryManager->createUnmanagedImage(
            {0, VK_FORMAT_D16_UNORM, {swapChainExtent.width, swapChainExtent.height, 1}, VK_IMAGE_TILING_OPTIMAL,
             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
             VK_IMAGE_LAYOUT_UNDEFINED, 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
             false, &viewCreateInfo});

    if (!dynamicRendering) {
        ImmediateSubmit([&](auto &cmd) {
            TransitionImage(cmd, shadowCascadeImage, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE,
                            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

            TransitionImage(cmd, depthImage, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE,
                            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        });
    }

    constexpr VkSamplerCreateInfo samplerInfo{
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
            1.0f,
            VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
            VK_FALSE
    };

    VK_CHECK(vkCreateSampler(device, &samplerInfo, VK_NULL_HANDLE, &shadowCascadeImage.sampler));
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
        VK_FORMAT_D16_UNORM,
        msaaSamples,
        VK_ATTACHMENT_LOAD_OP_LOAD,
        VK_ATTACHMENT_STORE_OP_NONE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    constexpr VkAttachmentReference colorAttachmentRef{
        0,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    constexpr VkAttachmentReference mainDepthAttachmentRef{
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

    constexpr VkSubpassDependency computeDependency{
        VK_SUBPASS_EXTERNAL,
        0,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT
    };

    constexpr VkSubpassDependency mainDepthPrepassDependency{
            VK_SUBPASS_EXTERNAL,
            0,
            VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    std::array attachments = {swapChainImageDescription, mainDepthImageDescription};
    constexpr std::array dependencies = {computeDependency, mainDepthPrepassDependency};
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

    static constexpr VkAttachmentDescription depthImageDescription{
            0,
            VK_FORMAT_D16_UNORM,
            msaaSamples,
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
    };

    static constexpr VkAttachmentReference depthAttachmentRef{
            0,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    static constexpr VkSubpassDescription depthPrepassSubpass{
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

    constexpr VkSubpassDependency depthPrepassDependency{
        VK_SUBPASS_EXTERNAL,
        0,
        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_DEPENDENCY_BY_REGION_BIT
    };

    constexpr VkSubpassDependency depthPrepassPostDependency{
            0,
            VK_SUBPASS_EXTERNAL,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_DEPENDENCY_BY_REGION_BIT
    };

    static constexpr std::array depthDependencies = {depthPrepassDependency, depthPrepassPostDependency};
    static constexpr VkRenderPassCreateInfo depthPrepassRenderPassInfo{
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        1,
        &depthImageDescription,
        1,
        &depthPrepassSubpass,
        depthDependencies.size(),
        depthDependencies.data()
    };

    VK_CHECK(vkCreateRenderPass(device, &depthPrepassRenderPassInfo, VK_NULL_HANDLE, &depthPrepassRenderPass));
}

void VkRenderer::CreatePipelineLayout() {
//    static constexpr VkPushConstantRange depthPassPushConstantRange{
//        VK_SHADER_STAGE_VERTEX_BIT,
//        0,
//        sizeof(DepthPassPushConstants)
//    };

    constexpr VkPushConstantRange computePushConstantRange{
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(ComputePushConstants)
    };

    constexpr VkPushConstantRange frustumPushConstantRange{
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(FrustumPushConstants)
    };

    constexpr VkPushConstantRange skyboxPushConstantRange{
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(glm::vec4) * 3
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        1,
        &mainDescriptorSetLayout, // it has the same layout
        1,
        &computePushConstantRange
    };

//    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, VK_NULL_HANDLE, &depthPrepassPipelineLayout));

//    pipelineLayoutInfo.pSetLayouts = &mainDescriptorSetLayout;
//    pipelineLayoutInfo.pPushConstantRanges = &computePushConstantRange;

    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, VK_NULL_HANDLE, &computePipelineLayout));

    pipelineLayoutInfo.pSetLayouts = &frustumDescriptorSetLayout;
    pipelineLayoutInfo.pPushConstantRanges = &frustumPushConstantRange;
    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, VK_NULL_HANDLE, &frustumPipelineLayout));

    pipelineLayoutInfo.pSetLayouts = &skyboxDescriptorSetLayout;
    pipelineLayoutInfo.pPushConstantRanges = &skyboxPushConstantRange;
    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, VK_NULL_HANDLE, &skyboxPipelineLayout));
}

void VkRenderer::CreateGraphicsPipeline() {
    metalRoughMaterial.buildPipelines(this);

    constexpr VkPushConstantRange depthPassPushConstantRange{
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(DepthPassPushConstants)
    };

    std::array layouts = {sceneDescriptorSetLayout/*, metalRoughMaterial.materialLayout*/};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            VK_NULL_HANDLE,
            0,
            layouts.size(),
            layouts.data(), // it has the same layout
            1,
            &depthPassPushConstantRange
    };

    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, VK_NULL_HANDLE, &depthPrepassPipelineLayout));

    VkGraphicsPipelineBuilder builder;
    builder.SetPipelineLayout(depthPrepassPipelineLayout);
    builder.CreateShaderModules(device, "shaders/depth_prepass.vert.spv", "");
    builder.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    builder.SetPolygonMode(VK_POLYGON_MODE_FILL);
    builder.SetCullingMode(VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_CLOCKWISE);
    builder.EnableClampMode();
    builder.EnableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);

    VkSpecializationMapEntry specializationMapEntry{
        0,
        0,
        sizeof(uint32_t)
    };

    VkSpecializationInfo specializationInfo{
        1,
        &specializationMapEntry,
        sizeof(uint32_t),
        &SHADOW_MAP_CASCADE_COUNT
    };

    if (dynamicRendering)
        builder.SetDepthFormat(VK_FORMAT_D16_UNORM);

    depthPrepassPipeline = builder.Build(dynamicRendering, device, pipelineCache, depthPrepassRenderPass, {VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, specializationInfo});

    builder.DestroyShaderModules(device);

    builder.Clear();
    builder.SetPipelineLayout(depthPrepassPipelineLayout);
    builder.CreateShaderModules(device, "shaders/shadowmap.vert.spv", "shaders/shadowmap.frag.spv");
    builder.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    builder.SetPolygonMode(VK_POLYGON_MODE_FILL);
    builder.SetCullingMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
    builder.EnableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);

    if (dynamicRendering) {
        builder.SetColorAttachmentFormat(surfaceFormat.format);
        builder.SetDepthFormat(VK_FORMAT_D16_UNORM);
    }

    shadowMapPipeline = builder.Build(dynamicRendering, device, pipelineCache, depthPrepassRenderPass);

    builder.DestroyShaderModules(device);

    builder.Clear();
    builder.SetPipelineLayout(skyboxPipelineLayout);
    builder.CreateShaderModules(device, "shaders/skybox.vert.spv", "shaders/skybox.frag.spv");
    builder.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    builder.SetPolygonMode(VK_POLYGON_MODE_FILL);
    builder.SetCullingMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
    builder.EnableDepthTest(false, VK_COMPARE_OP_LESS);

    if (dynamicRendering) {
        builder.SetColorAttachmentFormat(surfaceFormat.format);
        builder.SetDepthFormat(VK_FORMAT_D16_UNORM);
    }

    skyboxPipeline = builder.Build(dynamicRendering, device, pipelineCache, renderPass);

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

    VK_CHECK(vkCreateCommandPool(device, &poolInfo, VK_NULL_HANDLE, &graphicsCommandPool));

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VK_CHECK(vkCreateCommandPool(device, &poolInfo, VK_NULL_HANDLE, &frames[i].commandPool));
    }

//    commandPools.resize(std::thread::hardware_concurrency());
//    for (auto & commandPool : commandPools) {
//        VK_CHECK(vkCreateCommandPool(device, &poolInfo, VK_NULL_HANDLE, &commandPool));
//    }

    if (asyncCompute) {
        poolInfo.queueFamilyIndex = queueFamilyIndices.computeFamily.value();
        VK_CHECK(vkCreateCommandPool(device, &poolInfo, VK_NULL_HANDLE, &computeCommandPool));
    }

    poolInfo.queueFamilyIndex = queueFamilyIndices.transferFamily.value();
    VK_CHECK(vkCreateCommandPool(device, &poolInfo, VK_NULL_HANDLE, &transferCommandPool));
}

void VkRenderer::CreateCommandBuffers() {
    VkCommandBufferAllocateInfo allocInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            VK_NULL_HANDLE,
            graphicsCommandPool,
            VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            1
    };

    VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &depthPrepassCommandBuffer));

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        allocInfo.commandPool = frames[i].commandPool;
        VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &frames[i].commandBuffer));
    }

    if (asyncCompute) {
        allocInfo.commandPool = computeCommandPool;
        VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &computeCommandBuffer));
    }
}

void VkRenderer::CreateDefaultTexture() {

    constexpr uint32_t textureSize = 16;
    static constexpr std::array<uint32_t, textureSize * textureSize> pixels{UINT32_MAX};

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
    constexpr VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    constexpr VkFenceCreateInfo fenceInfo{
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        VK_NULL_HANDLE,
        VK_FENCE_CREATE_SIGNALED_BIT
    };

    if (asyncCompute) {
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, VK_NULL_HANDLE, &computeFinishedSemaphore));
        VK_CHECK(vkCreateFence(device, &fenceInfo, VK_NULL_HANDLE, &computeFinishedFence));
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, VK_NULL_HANDLE, &frames[i].imageAvailableSemaphore));
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, VK_NULL_HANDLE, &frames[i].renderFinishedSemaphore));
        VK_CHECK(vkCreateFence(device, &fenceInfo, VK_NULL_HANDLE, &frames[i].inFlightFence));
    }

    VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, VK_NULL_HANDLE, &depthPrepassSemaphore));
    VK_CHECK(vkCreateFence(device, &fenceInfo, VK_NULL_HANDLE, &depthPrepassFence));
}

void VkRenderer::CreateDescriptors() {
    static constexpr DescriptorAllocator::PoolSizeRatio sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
    };

    mainDescriptorAllocator.InitPool(device, 10, sizes);

    DescriptorLayoutBuilder builder;
    builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    builder.AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    builder.AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    mainDescriptorSetLayout = builder.Build(device);

    builder.Clear();
    builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_MESH_BIT_EXT);
    builder.AddBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT);
    builder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    builder.AddBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_MESH_BIT_EXT);

    if (meshShader) {
        builder.AddBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.AddBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.AddBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
    }

    sceneDescriptorSetLayout = builder.Build(device);

    builder.Clear();
    builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    frustumDescriptorSetLayout = builder.Build(device);

    builder.Clear();
    builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    skyboxDescriptorSetLayout = builder.Build(device);

    VkDescriptorSetLayout layouts[] = {mainDescriptorSetLayout};
    mainDescriptorSet = mainDescriptorAllocator.Allocate(device, layouts);

    layouts[0] = sceneDescriptorSetLayout;
    sceneDescriptorSet = mainDescriptorAllocator.Allocate(device, layouts);

    layouts[0] = skyboxDescriptorSetLayout;
    skyboxDescriptorSet = mainDescriptorAllocator.Allocate(device, layouts);

    static constexpr DescriptorAllocator::PoolSizeRatio frameSizes[] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          3 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         3 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         3 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 },
    };

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        frames[i].frameDescriptors = DescriptorAllocator{};
        frames[i].frameDescriptors.InitPool(device, 1000, frameSizes);
    }
}

void VkRenderer::FindQueueFamilies(const VkPhysicalDevice &gpu) {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamilyCount, VK_NULL_HANDLE);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto &queueFamily: queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queueFamilyIndices.graphicsFamily = i;
        }

        if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT && !(queueFamily.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))) {
            queueFamilyIndices.transferFamily = i;
        }

        if (asyncCompute) {
            if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT && !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                queueFamilyIndices.computeFamily = i;
            }
        } else if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            queueFamilyIndices.computeFamily = queueFamilyIndices.graphicsFamily;
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

    if (!queueFamilyIndices.computeFamily.has_value()) {
        printf("Compute queue family not found, using graphics queue family\n");
        queueFamilyIndices.computeFamily = queueFamilyIndices.graphicsFamily;
        asyncCompute = false;
    }

    if (!queueFamilyIndices.transferFamily.has_value())
    {
        printf("Async transfer queue not found, using graphics queue family\n");
        queueFamilyIndices.transferFamily = queueFamilyIndices.graphicsFamily;
    }

    assert(queueFamilyIndices.IsComplete());
}

VkRenderer::SwapChainSupportDetails VkRenderer::QuerySwapChainSupport(const VkPhysicalDevice &gpu) const {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, VK_NULL_HANDLE);

    if (formatCount != 0) [[likely]] {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &presentModeCount, VK_NULL_HANDLE);

    if (presentModeCount != 0) [[likely]] {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}
VkSurfaceFormatKHR VkRenderer::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats) {
    for (const auto &availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace ==
            VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    fprintf(stderr, "No preferred format found, using first available\n");
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

    vkDestroyFramebuffer(device, depthPrepassFramebuffer, nullptr);
    memoryManager->destroyImage(shadowCascadeImage, false);
    memoryManager->destroyImage(depthImage, false);

    vkDestroySwapchainKHR(device, swapChain, nullptr);
}

void VkRenderer::SavePipelineCache() const {
    size_t size;
    vkGetPipelineCacheData(device, pipelineCache, &size, nullptr);

    std::vector<char> data(size);
    vkGetPipelineCacheData(device, pipelineCache, &size, data.data());

    WriteFile("pipeline_cache.bin", data.data(), static_cast<std::streamsize>(size));
}

void VkRenderer::UpdateCascades() {
    const float nearClip = camera->nearPlane;
    const float farClip = camera->farPlane;
    const float clipRange = farClip - nearClip;

    const float minZ = nearClip;
    const float maxZ = farClip;

    const float range = maxZ - minZ;
    const float ratio = maxZ / minZ;

    float lastSplitDist = 0.0;
    for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; i++) {
        constexpr float cascadeSplitLambda = 0.95f;
        float p = static_cast<float>(i + 1) / static_cast<float>(SHADOW_MAP_CASCADE_COUNT);
        float log = minZ * std::pow(ratio, p);
        float uniform = minZ + range * p;
        float d = cascadeSplitLambda * (log - uniform) + uniform;

        float splitDist = (d - nearClip) / clipRange;

        glm::vec3 corners[]{
                glm::vec3(-1.0f, 1.0f, 0.0f),
                glm::vec3( 1.0f, 1.0f, 0.0f),
                glm::vec3( 1.0f,-1.0f, 0.0f),
                glm::vec3(-1.0f,-1.0f, 0.0f),
                glm::vec3(-1.0f, 1.0f, 1.0f),
                glm::vec3( 1.0f, 1.0f, 1.0f),
                glm::vec3( 1.0f,-1.0f, 1.0f),
                glm::vec3(-1.0f,-1.0f, 1.0f),
        };
        auto inv = inverse(camera->ProjectionMatrix() * camera->ViewMatrix());

        for (auto &corner : corners) {
            auto invCorner = inv * glm::vec4(corner, 1.0f);
            corner = invCorner / invCorner.w;
        }

        for (uint32_t j = 0; j < 4; j++) {
            auto dist = corners[j + 4] - corners[j];
            corners[j + 4] = corners[j] + dist * splitDist;
            corners[j] += dist * lastSplitDist;
        }

        auto frustumCenter = glm::vec3(0.0f);
        for (auto &corner : corners) {
            frustumCenter += corner;
        }
        frustumCenter /= 8.0f;

        float radius = 0.0f;
        for (auto &corner : corners) {
            float distance = length(corner - frustumCenter);
            radius = std::max(radius, distance);
        }

        radius = std::ceil(radius * 16.0f) / 16.0f;

        auto maxExtents = glm::vec3(radius);
        auto minExtents = -maxExtents;

        const glm::vec4 &lightPosition = totalLights->lights[0].position;
        auto lightDir = normalize(glm::vec3(lightPosition.x, -lightPosition.y, lightPosition.z));
        auto lightView = lookAt(frustumCenter - lightDir * maxExtents.z, frustumCenter, camera->worldUp);
        auto lightOrtho = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.0f, maxExtents.z - minExtents.z);

        lightOrtho[1][1] *= -1.0f;

        cascadeSplits.arr[i] = d * -1.0f;
        cascadeViewProjections[i] = lightOrtho * lightView;

        lastSplitDist = splitDist;
    }

    memoryManager->copyToBuffer(cascadeViewProjectionBuffer, cascadeViewProjections.data(), sizeof(glm::mat4) * SHADOW_MAP_CASCADE_COUNT);
}

void VkRenderer::UpdateScene(EngineStats &stats) {
    const auto view = camera->ViewMatrix();
    const auto proj = camera->ProjectionMatrix();

    sceneData.worldMatrix = proj * view;
    memoryManager->copyToBuffer(sceneDataBuffer, &sceneData, sizeof(SceneData));
    memoryManager->copyToBuffer(viewMatrix, &view, sizeof(glm::mat4));

    if (!meshShader)
        loadedScene.Draw(glm::mat4{1.f}, mainDrawContext);

    // UpdateCascades();
}

#ifndef NDEBUG
VkBool32 VKAPI_CALL VkRenderer::DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *) {
    fprintf(stderr, "%s\n\n", pCallbackData->pMessage);
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

constexpr int multiplier = 16 * 9 * 6;

void VkRenderer::CreateRandomLights() {
    totalLights = new Light;
    totalLights->lightCount = MAX_LIGHTS;

    std::random_device rd;
    std::mt19937 gen(rd());

//    std::uniform_real_distribution<float> disXZ(-9.f, 9.f);
//    std::uniform_real_distribution<float> disY(0.5f, 7.f);
//
//    std::uniform_real_distribution<float> disColor(0.f, 1.f);

    for (size_t i = 0; i < totalLights->lightCount; i++) {
        totalLights->lights[i].position = {20.0f, 20.0f, 0.0f, 500.f};
        totalLights->lights[i].color = {1.0f, 1.0f, 1.0f, 1.0f};
    }

    lightBuffer = memoryManager->createManagedBuffer(
            {sizeof(Light), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
             VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT});

    const auto mappedMemoryTask = [&](auto &, auto *stagingBuffer) {
        memcpy(stagingBuffer, totalLights, sizeof(Light));
    };

    const auto unmappedMemoryTask = [&](auto &buf) {
        TransferSubmit([&](auto &cmd) {
            VkBufferCopy copyRegion{
                0,
                0,
                sizeof(Light)
            };

            vkCmdCopyBuffer(cmd, buf, lightBuffer.buffer, 1, &copyRegion);
        });
    };

    memoryManager->stagingBuffer(sizeof(Light), mappedMemoryTask, unmappedMemoryTask);

    visibleLightBuffer = memoryManager->createManagedBuffer({sizeof(LightVisibility) * multiplier,
                                                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                                             VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0,
                                                             VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT});

    lightCountUniform = memoryManager->createManagedBuffer({sizeof(uint16_t) * MAX_LIGHTS_VISIBLE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                            0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT});

    viewMatrix = memoryManager->createManagedBuffer({sizeof(glm::mat4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VMA_MEMORY_USAGE_AUTO,
                                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT});

    TransferSubmit([&](auto &cmd) {
        vkCmdFillBuffer(cmd, visibleLightBuffer.buffer, 0, sizeof(LightVisibility) * multiplier, 0);
    });
}

void VkRenderer::UpdateDescriptorSets() {
    DescriptorWriter writer;
    writer.WriteBuffer(0, lightBuffer.buffer, 0, sizeof(Light), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writer.WriteBuffer(1, visibleLightBuffer.buffer, 0, sizeof(LightVisibility) * multiplier, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writer.WriteBuffer(2, lightCountUniform.buffer, 0, sizeof(uint16_t) * MAX_LIGHTS_VISIBLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writer.UpdateSet(device, mainDescriptorSet);

    writer.Clear();
    writer.WriteImage(0, skyboxImage.imageView, skyboxImage.sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.UpdateSet(device, skyboxDescriptorSet);

    writer.Clear();
    writer.WriteBuffer(0, sceneDataBuffer.buffer, 0, sizeof(SceneData), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.WriteBuffer(1, cascadeViewProjectionBuffer.buffer, 0, sizeof(glm::mat4) * SHADOW_MAP_CASCADE_COUNT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.WriteImage(2, shadowCascadeImage.imageView, shadowCascadeImage.sampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.WriteBuffer(3, viewMatrix.buffer, 0, sizeof(glm::mat4), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

    if (meshShader) {
        writer.WriteBuffer(4, positionBuffer.buffer, 0, sizeof(float) * meshletStats.positionCount, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.WriteBuffer(5, meshletBuffer.buffer, 0, sizeof(meshopt_Meshlet) * meshletStats.meshletCount, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.WriteBuffer(6, meshletVerticesBuffer.buffer, 0, sizeof(uint32_t) * meshletStats.verticesCount, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.WriteBuffer(7, meshletPrimitivesBuffer.buffer, 0, sizeof(uint32_t) * meshletStats.primitiveCount, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    writer.UpdateSet(device, sceneDescriptorSet);
}

void VkRenderer::ComputeFrustum() {
    std::array layouts = {frustumDescriptorSetLayout};
    auto frustumDescriptorSet = mainDescriptorAllocator.Allocate(device, layouts);

    DescriptorWriter writer;
    writer.WriteBuffer(0, visibleLightBuffer.buffer, 0, sizeof(LightVisibility) * multiplier, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writer.UpdateSet(device, frustumDescriptorSet);

    FrustumPushConstants pushConstants {
        inverse(camera->ProjectionMatrix()),
        {swapChainExtent.width, swapChainExtent.height}
    };

    ImmediateSubmit([&](auto &cmd) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frustumPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frustumPipelineLayout, 0, 1, &frustumDescriptorSet, 0, VK_NULL_HANDLE);
        vkCmdPushConstants(cmd, frustumPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FrustumPushConstants), &pushConstants);
        vkCmdDispatch(cmd, 16, 9, 24);
    });
}

void VkRenderer::CreateSkybox() {
    ktxTexture *skyboxTexture;
    const auto result = ktxTexture_CreateFromNamedFile("../assets/cubemap_vulkan.ktx", KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &skyboxTexture);
    assert(result == KTX_SUCCESS);

    skyboxImage = memoryManager->createKtxCubemap(skyboxTexture, this, VK_FORMAT_R8G8B8A8_UNORM);

    ktxTexture_Destroy(skyboxTexture);
}

void VkRenderer::CreateShadowCascades() {
    cascadeViewProjectionBuffer = memoryManager->createManagedBuffer({sizeof(glm::mat4) * SHADOW_MAP_CASCADE_COUNT,
                                                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                                                                      VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                                                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT});

    for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; i++) {
        VkImageViewCreateInfo imageViewCreateInfo{
                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                VK_NULL_HANDLE,
                0,
                shadowCascadeImage.image,
                VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                VK_FORMAT_D16_UNORM,
                {
                        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                        VK_COMPONENT_SWIZZLE_IDENTITY
                },
                {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, i, 1}
        };

        VK_CHECK(vkCreateImageView(device, &imageViewCreateInfo, VK_NULL_HANDLE, &shadowCascades[i].shadowImageView));

        if (!dynamicRendering) {
            VkFramebufferCreateInfo framebufferCreateInfo{
                VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                VK_NULL_HANDLE,
                0,
                depthPrepassRenderPass,
                1,
                &shadowCascades[i].shadowImageView,
                SHADOW_MAP_SIZE,
                SHADOW_MAP_SIZE,
                1
            };

            VK_CHECK(vkCreateFramebuffer(device, &framebufferCreateInfo, VK_NULL_HANDLE, &shadowCascades[i].shadowMapFramebuffer));
        }
    }
}
