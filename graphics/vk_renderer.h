#ifndef D3D12_STUFF_VK_RENDERER_H
#define D3D12_STUFF_VK_RENDERER_H

#define GLFW_INCLUDE_VULKAN
#define GLM_FORCE_RADIANS

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <glfw/glfw3.h>
#include <glfw/glfw3native.h>
#include <wrl/client.h>
#include <meshoptimizer.h>

using Microsoft::WRL::ComPtr;
#else
// use system glfw
#include <GLFW/glfw3.h>
#endif

#include <dxgi1_5.h>
#include <optional>
#include <vector>
#include <detail/type_half.hpp>

#include "engine/camera.h"
#include "engine/objects/material.h"
#include "engine/objects/render_object.h"
#include "vk/memory/vk_memory.h"
#include "vk/vk_descriptor_layout.h"
#include "engine/objects/gltf.h"

#define USE_DXGI_SWAPCHAIN

static constexpr uint32_t SHADOW_MAP_CASCADE_COUNT = 4;
static constexpr uint32_t SHADOW_MAP_SIZE = 4096;

struct EngineStats;
struct MeshAsset;
static bool isVkRunning = false;

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
using hvec4 = glm::vec<4, glm::detail::hdata>;

extern PFN_vkCmdDrawMeshTasksEXT fn_vkCmdDrawMeshTasksEXT;

namespace glm {
    template<>
    template<>
    GLM_FUNC_QUALIFIER GLM_CTOR_DECL vec<4, detail::hdata>::vec(float x, float y, float z, float w)
        : x(detail::toFloat16(x))
        , y(detail::toFloat16(y))
        , z(detail::toFloat16(z))
        , w(detail::toFloat16(w))
    {}

    template<>
    template<>
    GLM_FUNC_QUALIFIER GLM_CTOR_DECL vec<4, detail::hdata>::vec(vec3 const& xyz, float w)
            : x(detail::toFloat16(xyz.x))
            , y(detail::toFloat16(xyz.y))
            , z(detail::toFloat16(xyz.z))
            , w(detail::toFloat16(w))
    {}
}

struct FrameData {
#if !defined(_WIN32) || !defined(USE_DXGI_SWAPCHAIN)
    VkSemaphore imageAvailableSemaphore{};
    VkSemaphore renderFinishedSemaphore{};
#endif
    VkFence inFlightFence{};

    VkCommandPool commandPool{};
    VkCommandBuffer commandBuffer{};

    DescriptorAllocator frameDescriptors;
};

struct SceneData {
    glm::mat4 worldMatrix;
};

struct DepthPassPushConstants {
    VkDeviceAddress vertexBufferDeviceAddress;
    uint32_t cascadeIndex;
};

#define MAX_LIGHTS 1
#define MAX_LIGHTS_VISIBLE 256

struct Light {
    uint32_t lightCount;
    alignas(8) struct {
        glm::vec4 position;
        glm::vec4 color;
    } lights[MAX_LIGHTS];
};

struct LightVisibility {
    glm::vec4 minPoints;
    glm::vec4 maxPoints;
    uint16_t indices[MAX_LIGHTS_VISIBLE];
};

struct ComputePushConstants {
    alignas(16) glm::mat4 viewMatrix;
};

struct FrustumPushConstants {
    alignas(16) glm::mat4 viewMatrix;
    alignas(16) glm::ivec2 viewportSize;
};

// TODO: Replace error handling with a dialog box
class VkRenderer {
public:
// #ifdef _WIN32
//     explicit VkRenderer(HINSTANCE hinstance, HWND hwnd, Camera *camera, bool dynamicRendering = true, bool asyncCompute = true, bool meshShader = false);
// #else
    // explicit VkRenderer(GLFWwindow *window, Camera *camera, bool dynamicRendering = true, bool asyncCompute = true, bool meshShader = false);
    explicit VkRenderer();
    void Initialize(GLFWwindow *, Camera *, bool dynamicRendering = true, bool asyncCompute = true, bool meshShader = false);
// #endif
    ~VkRenderer();
    VkRenderer(const VkRenderer &) = delete;
    VkRenderer &operator=(const VkRenderer &) = delete;
    VkRenderer &operator=(VkRenderer &&) = delete;

    uint16_t GetFPSLimit() const;
    void SetFPSLimit(uint16_t fps);

    void Render(EngineStats &stats);
    void Draw(const VkCommandBuffer &commandBuffer, uint32_t imageIndex, EngineStats &stats);
    void DrawMesh(const VkCommandBuffer &commandBuffer, uint32_t imageIndex, EngineStats &stats);
    void DrawIndirect(const VkCommandBuffer &commandBuffer, uint32_t imageIndex, EngineStats &stats);
    void Shutdown();
    void RecreateSwapChain();
    void ReloadShaders();

    Mesh CreateMesh(std::span<VkVertex> vertices, std::span<uint32_t> indices);
    void CreateFromMeshlets(const std::vector<VkVertex> &vertices, const std::vector<uint32_t> &indices);
    void CreateMeshletBuffers();

    void ImmediateSubmit(std::function<void(const VkCommandBuffer &)> &&callback) const {
        const VkCommandBufferAllocateInfo allocInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            VK_NULL_HANDLE,
            graphicsCommandPool,
            VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            1
        };

        VkCommandBuffer commandBuffer;
        VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

        constexpr VkCommandBufferBeginInfo beginInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            VK_NULL_HANDLE,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            VK_NULL_HANDLE
        };

        VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
        callback(commandBuffer);
        VK_CHECK(vkEndCommandBuffer(commandBuffer));

        const VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = VK_NULL_HANDLE,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffer
        };

        VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
        VK_CHECK(vkQueueWaitIdle(graphicsQueue));

        vkFreeCommandBuffers(device, graphicsCommandPool, 1, &commandBuffer);
    }

    void TransferSubmit(std::function<void(const VkCommandBuffer &)> &&callback) const {
        const VkCommandBufferAllocateInfo allocInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            VK_NULL_HANDLE,
            transferCommandPool,
            VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            1
        };

        VkCommandBuffer commandBuffer;
        VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

        constexpr VkCommandBufferBeginInfo beginInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            VK_NULL_HANDLE,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            VK_NULL_HANDLE
        };

        VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
        callback(commandBuffer);
        VK_CHECK(vkEndCommandBuffer(commandBuffer));

        const VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = VK_NULL_HANDLE,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffer
        };

        VK_CHECK(vkQueueSubmit(transferQueue, 1, &submitInfo, VK_NULL_HANDLE));
        VK_CHECK(vkQueueWaitIdle(transferQueue));

        vkFreeCommandBuffers(device, transferCommandPool, 1, &commandBuffer);
    }

//    const std::vector<VkCommandPool> &threaded_command_pools() {
//        return commandPools;
//    }
//
//    void SubmitMultithreadedCommandBuffers(const std::vector<VkCommandBuffer> &commandBuffers) const {
//        const VkSubmitInfo submitInfo{
//                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
//                .pNext = VK_NULL_HANDLE,
//                .commandBufferCount = static_cast<uint32_t>(commandBuffers.size()),
//                .pCommandBuffers = commandBuffers.data()
//        };
//
//        VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
//        VK_CHECK(vkQueueWaitIdle(graphicsQueue));
//
//        for (size_t i = 0; i < commandBuffers.size(); i++) {
//            vkResetCommandPool(device, commandPools[i], 0);
//        }
//    }

    void Screenshot();

    uint8_t currentFrame = 0;
    struct {
        bool dynamicRendering : 1;
        bool asyncCompute : 1;
        bool raytracingCapable : 1;
        bool framebufferResized : 1;
        bool isIntegratedGPU : 1;
        bool meshShader : 1;
        bool allowTearing : 1;
        bool isShaderInvalidated: 1;
    };
    int32_t cascadeIndex = 0;

    static constexpr VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    GLFWwindow *glfwWindow;
    Camera *camera;

    VkViewport viewport{};
    VkRect2D scissor{};

#if defined(_WIN32) && defined(USE_DXGI_SWAPCHAIN)
    ComPtr<ID3D12Device> d3dDevice;
    ComPtr<IDXGISwapChain3> d3dSwapChain;
    ComPtr<ID3D12CommandQueue> d3dQueue;
    // ComPtr<ID3D12Resource> d3dFramebuffers[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore timelineSemaphore;
    HANDLE timelineSemaphoreHandle;
    ComPtr<ID3D12Fence> sharedFence;
    uint64_t waitValue = 1;
    uint64_t signalValue = 2;
    // TODO: switch this
    std::vector<VkDeviceMemory> swapChainMemory;
#else
    VkSwapchainKHR swapChain{};
#endif
//     HINSTANCE hInstance;
//     HWND hWnd;
// #else
    VkInstance instance{};
    VkSurfaceKHR surface{};
// #endif
    VkPhysicalDevice physicalDevice{};
    VkDevice device{};
    VkPipelineCache pipelineCache{};
    VkQueue graphicsQueue{};
    VkQueue computeQueue{};
    VkQueue transferQueue{};
    VkQueue presentQueue{};
    // VkSwapchainKHR swapChain{};
    VkSurfaceFormatKHR surfaceFormat{};
    VkRenderPass renderPass{};

    VkMemoryManager memoryManager;

    VkPhysicalDeviceProperties deviceProperties{};
    VkDeviceSize maxMemoryAllocationSize{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};

    VulkanImage depthImage{};
    VulkanImage shadowCascadeImage{};

    DescriptorAllocator mainDescriptorAllocator{};
    VkDescriptorSet mainDescriptorSet{};
    VkDescriptorSetLayout mainDescriptorSetLayout{};
    VkDescriptorSetLayout sceneDescriptorSetLayout{};

    VkDescriptorSet sceneDescriptorSet{};
    VulkanBuffer sceneDataBuffer{};

    VkPipeline depthPrepassPipeline{};
    VkPipelineLayout depthPrepassPipelineLayout{};
    VkRenderPass depthPrepassRenderPass{};
    VkFramebuffer depthPrepassFramebuffer{};
    VkSemaphore depthPrepassSemaphore{};
    VkFence depthPrepassFence{};
    VkCommandBuffer depthPrepassCommandBuffer{};

    VkPipeline shadowMapPipeline{};

    VkPipeline skyboxPipeline{};
    VkPipelineLayout skyboxPipelineLayout{};
    VkDescriptorSetLayout skyboxDescriptorSetLayout{};
    VkDescriptorSet skyboxDescriptorSet{};

    VkPipeline computePipeline{};
    VkPipelineLayout computePipelineLayout{};

    VkPipeline frustumPipeline{};
    VkPipelineLayout frustumPipelineLayout{};
    VkDescriptorSetLayout frustumDescriptorSetLayout{};

    VkSemaphore computeFinishedSemaphore{};
    VkFence computeFinishedFence{};
    VkCommandBuffer computeCommandBuffer{};

    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> frames;
    VkCommandPool graphicsCommandPool{};
    VkCommandPool transferCommandPool{};
    VkCommandPool computeCommandPool{};

    std::vector<VkFramebuffer> swapChainFramebuffers{};
    std::vector<VkImage> swapChainImages{};
    std::vector<VkImageView> swapChainImageViews{};

    VkPresentModeKHR presentMode{};
    VkExtent2D swapChainExtent{};
    uint32_t swapChainImageCount{};

    VulkanImage defaultImage{};
    VulkanImage skyboxImage{};

    VulkanBuffer lightBuffer{};
    VulkanBuffer visibleLightBuffer{};
    VulkanBuffer lightCountUniform{};
    VulkanBuffer viewMatrix{};

    VkSampler textureSamplerLinear{};
    VkSampler textureSamplerNearest{};

    VkGLTFMetallic_Roughness metalRoughMaterial{};

    VkDrawContext mainDrawContext{};
    LoadedGLTF loadedScene{};
    SceneData sceneData{};

    Light *totalLights;

    struct ShadowCascade {
        VkImageView shadowImageView;
        VkFramebuffer shadowMapFramebuffer;
        float splitDepth;
    };

    std::array<ShadowCascade, SHADOW_MAP_CASCADE_COUNT> shadowCascades{};
    VulkanBuffer cascadeViewProjectionBuffer{};
    std::array<glm::mat4, SHADOW_MAP_CASCADE_COUNT> cascadeViewProjections{};
    union
    {
        std::array<float, SHADOW_MAP_CASCADE_COUNT> arr;
        glm::vec4 vec4;
    } cascadeSplits{};

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> transferFamily;
        std::optional<uint32_t> computeFamily;
        std::optional<uint32_t> presentFamily;

        [[nodiscard]] bool IsComplete() const {
            return graphicsFamily.has_value() && transferFamily.has_value() && computeFamily.has_value() && presentFamily.has_value();
        }
    } queueFamilyIndices;

    size_t meshCount;
    struct MeshletStats {
        uint32_t positionCount;
        uint32_t meshletCount;
        uint32_t verticesCount;
        uint32_t primitiveCount;
    };
    // TODO: optimize this
    std::vector<MeshletStats> meshletsStats;
    std::vector<meshopt_Meshlet> loadedMeshlets; // TODO: supposed to be meshopt_Meshlet
    std::vector<float> vertexPositionsData;
    std::vector<uint32_t> meshletsVerticesData;
    std::vector<uint32_t> meshletsPrimitivesData;

    struct MeshOffsets
    {
        uint32_t positionOffset;
        uint32_t vertexOffset;
        uint32_t primitiveOffset;
    };
    std::vector<MeshOffsets> meshOffsets;
    // struct LoadedMeshlet
    // {
    //     std::vector<MeshletStats> stats;
    //     std::vector<glm::vec4> vertices;
    //     std::vector<uint32_t> indices;
    //     std::vector<uint32_t> primitives;
    // };
    // std::vector<LoadedMeshlet> loadedMeshlets{};
    // std::vector<MeshletStats> meshletStats;
    VulkanBuffer positionBuffer{};
    VulkanBuffer meshletBuffer{};
    VulkanBuffer meshletVerticesBuffer{};
    VulkanBuffer meshletPrimitivesBuffer{};
    // std::vector<VulkanBuffer> positionBuffers{};
    // std::vector<VulkanBuffer> meshletBuffers{};
    // std::vector<VulkanBuffer> meshletVerticesBuffers{};
    // std::vector<VulkanBuffer> meshletPrimitivesBuffers{};

private:
    uint16_t fpsLimit = 60;
#ifndef NDEBUG
    static constexpr std::array<const char *, 1> validationLayers = {
            "VK_LAYER_KHRONOS_validation"
    };

    VkDebugUtilsMessengerEXT debugMessenger{};
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData);
    static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger);
    static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks *pAllocator);
#endif

// #ifdef _WIN32
//     inline void InitializeInstance(HINSTANCE hInstance, HWND hWnd);
// #else
    inline void InitializeInstance();
// #endif
    inline void PickPhysicalDevice();
    inline void CreateLogicalDevice();
    inline void CreatePipelineCache();
#if defined(_WIN32) && defined(USE_DXGI_SWAPCHAIN)
    inline void CreateDXGISwapChain();
#endif
    inline void CreateSwapChain();
    inline void CreateRenderPass();
    inline void CreatePipelineLayout();
    inline void CreateGraphicsPipeline();
    inline void CreateComputePipeline();
    inline void CreateFramebuffers();
    inline void CreateCommandPool();
    inline void CreateCommandBuffers();
    inline void CreateDefaultTexture();
    inline void CreateSyncObjects();
    inline void CreateDescriptors();
    inline void CreateRandomLights();

    inline void FindQueueFamilies(const VkPhysicalDevice &gpu);
    [[nodiscard]] inline SwapChainSupportDetails QuerySwapChainSupport(const VkPhysicalDevice &gpu) const;
    inline static VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats);
    inline static VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes);
    [[nodiscard]] inline VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) const;
    inline void CleanupSwapChain();

    inline void SavePipelineCache() const;

    inline void UpdateScene(EngineStats &stats);

    inline void DrawObject(const VkCommandBuffer &commandBuffer, const VkRenderObject &draw, VkMaterialPipeline &lastPipeline, VkMaterialInstance &lastMaterialInstance, VkBuffer &lastIndexBuffer);
    inline void DrawDepthPrepass(/*const std::vector<size_t> &drawIndices*/);
    inline void DrawSkybox(const VkCommandBuffer &commandBuffer, EngineStats &stats) const;
    inline void BeginDraw(const VkCommandBuffer &commandBuffer, uint32_t imageIndex) const;
    inline void EndDraw(const VkCommandBuffer &commandBuffer, uint32_t imageIndex);

    inline void ComputeFrustum();

    inline void CreateSkybox();
    inline void CreateShadowCascades();

    inline void CreateDepthImage();

    inline void UpdateDescriptorSets();

    void UpdateCascades();
};

#endif
