#ifndef D3D12_STUFF_VK_RENDERER_H
#define D3D12_STUFF_VK_RENDERER_H

#define GLFW_INCLUDE_VULKAN
#define GLM_FORCE_RADIANS

#define MAX_LIGHTS 1024

#include <glfw/glfw3.h>
#include <memory>
#include <optional>
#include <vector>
#include <detail/type_half.hpp>

#include "camera.h"
#include "objects/material.h"
#include "objects/render_object.h"
#include "vk/memory/vk_memory.h"
#include "vk/vk_descriptor_layout.h"
#include "objects/gltf.h"

struct EngineStats;
struct MeshAsset;
static bool isVkRunning = false;

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
using hvec4 = glm::vec<4, glm::detail::hdata>;

namespace glm {
    template<>
    template<>
    GLM_FUNC_QUALIFIER GLM_CTOR_DECL vec<4, detail::hdata>::vec(float x, float y, float z, float w)
        : x(detail::toFloat16(x))
        , y(detail::toFloat16(y))
        , z(detail::toFloat16(z))
        , w(detail::toFloat16(w))
    {}
}

struct FrameData {
    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;

    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;

    DescriptorAllocator frameDescriptors;
    std::vector<std::function<void()>> frameCallbacks;
};

struct SceneData {
    glm::mat4 worldMatrix;
};

struct Light {
    uint32_t lightCount;
    alignas(8) struct {
        hvec4 position;
        hvec4 color;
    } lights[1024];
};

struct alignas(16) LightVisibility {
    glm::vec4 minPoint;
    glm::vec4 maxPoint;
    uint32_t visibleLightCount;
    uint32_t indices[1024];
};

struct ViewFrustum {
    glm::vec4 planes[4];
};

//struct FragmentPushConstants {
//};

struct ComputePushConstants {
    alignas(16) glm::mat4 viewMatrix;
};

struct FrustumPushConstants {
    alignas(16) glm::mat4 viewMatrix;
    alignas(16) glm::ivec2 viewportSize;
};

// TODO: Replace error handling with a dialog box
class VkRenderer {
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> computeFamily;
        std::optional<uint32_t> presentFamily;

        [[nodiscard]] bool IsComplete() const {
            return graphicsFamily.has_value() && computeFamily.has_value() && presentFamily.has_value();
        }
    } queueFamilyIndices;
public:
    explicit VkRenderer(GLFWwindow *window, Camera *camera, bool dynamicRendering = true, bool asyncCompute = true);
    ~VkRenderer();
    VkRenderer(const VkRenderer &) = delete;
    VkRenderer &operator=(const VkRenderer &) = delete;
    VkRenderer &operator=(VkRenderer &&) = delete;

    void Render(EngineStats &stats);
    void Draw(const VkCommandBuffer &commandBuffer, uint32_t imageIndex, EngineStats &stats);
    void DrawIndirect(const VkCommandBuffer &commandBuffer, uint32_t imageIndex, EngineStats &stats);
    void Shutdown();
    void RecreateSwapChain();

    Mesh CreateMesh(const std::span<VkVertex> &vertices, const std::span<uint32_t> &indices);

    static void BlitImage(const VkCommandBuffer &commandBuffer, const VulkanImage &srcImage, const VulkanImage &dstImage, VkImageLayout srcLayout, VkImageLayout dstLayout, VkImageAspectFlags aspectFlags);

    [[nodiscard]] const QueueFamilyIndices &queue_family_indices() const {
        return queueFamilyIndices;
    }

    [[nodiscard]] VkInstance vk_instance() const {
        return instance;
    }

    [[nodiscard]] VkPhysicalDevice physical_device() const {
        return physicalDevice;
    }

    [[nodiscard]] VkDevice logical_device() const {
        return device;
    }

    [[nodiscard]] VkPipelineCache pipeline_cache() const {
        return pipelineCache;
    }

    [[nodiscard]] VkRenderPass render_pass() const {
        return renderPass;
    }

    [[nodiscard]] VkQueue graphics_queue() const {
        return graphicsQueue;
    }

    [[nodiscard]] VkSurfaceFormatKHR surface_format() const {
        return surfaceFormat;
    }

    [[nodiscard]] VkDescriptorSetLayout main_descriptor_set_layout() const {
        return mainDescriptorSetLayout;
    }

    [[nodiscard]] VkDescriptorSetLayout scene_descriptor_set_layout() const {
        return sceneDescriptorSetLayout;
    }

    [[nodiscard]] VkSampleCountFlagBits msaa_samples() const {
        return msaaSamples;
    }

    [[nodiscard]] VkMemoryManager *memory_manager() const {
        return memoryManager.get();
    }

    [[nodiscard]] VulkanImage default_image() const {
        return defaultImage;
    }

    [[nodiscard]] VkSampler default_sampler_linear() const  {
        return textureSamplerLinear;
    }

    [[nodiscard]] VkGLTFMetallic_Roughness metal_rough_material() const {
        return metalRoughMaterial;
    }

    [[nodiscard]] VkPhysicalDeviceProperties device_properties() const {
        return deviceProperties;
    }

    [[nodiscard]] bool is_integrated_gpu() const {
        return isIntegratedGPU;
    }

    [[nodiscard]] bool needs_resizing() const {
        return framebufferResized;
    }

    void ImmediateSubmit(std::function<void(const VkCommandBuffer &)> &&callback) const {
        const VkCommandBufferAllocateInfo allocInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            VK_NULL_HANDLE,
            immediateCommandPool,
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

        vkFreeCommandBuffers(device, immediateCommandPool, 1, &commandBuffer);
    }

    void FramebufferNeedsResizing();
private:
    uint32_t currentFrame = 0;

    bool dynamicRendering = true;
    bool asyncCompute = true;
    bool raytracingCapable = false;
    bool framebufferResized = false;
    bool isIntegratedGPU = false;

    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    std::vector<const char *> deviceExtensions;

#ifndef NDEBUG
    const std::array<const char *, 1> validationLayers = {
            "VK_LAYER_KHRONOS_validation"
    };

    VkDebugUtilsMessengerEXT debugMessenger{};
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData);
    static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger);
    static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks *pAllocator);
#endif

    inline void InitializeInstance();
    inline void PickPhysicalDevice();
    inline void CreateLogicalDevice();
    inline void CreatePipelineCache();
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

    inline void UpdateScene();

    inline void DrawObject(const VkCommandBuffer &commandBuffer, const VkRenderObject &draw, const VkDescriptorSet &sceneDescriptorSet, VkMaterialPipeline &lastPipeline, VkMaterialInstance &lastMaterialInstance, VkBuffer &lastIndexBuffer);
    inline void DrawDepthPrepass(const std::vector<size_t> &drawIndices);

    inline void ComputeFrustum();

    inline void CreateSkybox();

    GLFWwindow *glfwWindow;
    Camera *camera;

    VkViewport viewport{};
    VkRect2D scissor{};

    VkInstance instance{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physicalDevice{};
    VkDevice device{};
    VkPipelineCache pipelineCache{};
    VkQueue graphicsQueue{};
    VkQueue computeQueue{};
    VkQueue presentQueue{};
    VkSwapchainKHR swapChain{};
    VkSurfaceFormatKHR surfaceFormat{};
    VkRenderPass renderPass{};

    VulkanImage depthImage{};

    DescriptorAllocator mainDescriptorAllocator{};
    VkDescriptorSet mainDescriptorSet{};
    VkDescriptorSetLayout mainDescriptorSetLayout{};
    VkDescriptorSetLayout sceneDescriptorSetLayout{};

    VkPipeline depthPrepassPipeline{};
    VkPipelineLayout depthPrepassPipelineLayout{};
    VkDescriptorSet depthPrepassDescriptorSet{};
    VkRenderPass depthPrepassRenderPass{};
    VkFramebuffer depthPrepassFramebuffer{};
    VkSemaphore depthPrepassSemaphore{};
    VkFence depthPrepassFence{};
    VkCommandBuffer depthPrepassCommandBuffer{};

    VkPipeline skyboxPipeline{};
    VkPipelineLayout skyboxPipelineLayout{};
    VkDescriptorSetLayout skyboxDescriptorSetLayout{};
    VkDescriptorSet skyboxDescriptorSet{};

    VkPipeline computePipeline{};
    VkPipelineLayout computePipelineLayout{};
    VkDescriptorSetLayout lightDescriptorSetLayout{};
    VkDescriptorSet lightDescriptorSet{};

    VkPipeline frustumPipeline{};
    VkPipelineLayout frustumPipelineLayout{};
    VkDescriptorSetLayout frustumDescriptorSetLayout{};

    VkSemaphore computeFinishedSemaphore{};
    VkFence computeFinishedFence{};
    VkCommandBuffer computeCommandBuffer{};

    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> frames;
    VkCommandPool immediateCommandPool{};
    VkCommandPool computeCommandPool{};

    std::vector<VkFramebuffer> swapChainFramebuffers{};
    std::vector<VkImage> swapChainImages{};
    std::vector<VkImageView> swapChainImageViews{};

    VkPresentModeKHR presentMode{};
    VkExtent2D swapChainExtent{};

    VkPhysicalDeviceProperties deviceProperties{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};

    VulkanImage defaultImage{};
    VulkanImage skyboxImage{};

    VulkanBuffer lightUniformBuffer{};
    VulkanBuffer visibleLightBuffer{};

    VkSampler textureSamplerLinear{};
    VkSampler textureSamplerNearest{};

    VkGLTFMetallic_Roughness metalRoughMaterial{};

    VkDrawContext mainDrawContext{};
    std::unordered_map<std::string, LoadedGLTF> loadedScenes{};
    SceneData sceneData{};

    std::unique_ptr<VkMemoryManager> memoryManager;

    std::unique_ptr<Light> totalLights;

    inline void CreateDepthImage();

    inline void UpdateDepthComputeDescriptorSets();
};


#endif
