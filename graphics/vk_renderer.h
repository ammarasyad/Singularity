#ifndef D3D12_STUFF_VK_RENDERER_H
#define D3D12_STUFF_VK_RENDERER_H

#define GLFW_INCLUDE_VULKAN
#define GLM_FORCE_RADIANS

#include <glfw/glfw3.h>
#include <memory>
#include <optional>
#include <vector>

#include "camera.h"
#include "objects/material.h"
#include "objects/render_object.h"
#include "vk/memory/vk_memory.h"
#include "vk/vk_descriptor_layout.h"

struct EngineStats;
struct MeshAsset;
static bool isVkRunning = false;

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

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
    glm::vec4 ambientColor;
    glm::vec4 sunlightDirection;
    glm::vec4 sunlightColor;
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
    float fov = 45.0f;

    explicit VkRenderer(GLFWwindow *window, Camera *camera, bool dynamicRendering = true, bool asyncCompute = true);
    ~VkRenderer();

    void Render(EngineStats &stats);
    void Draw(const VkCommandBuffer &commandBuffer, uint32_t imageIndex, EngineStats &stats);
    void Shutdown();
    void RecreateSwapChain();

    Mesh CreateMesh(const std::span<VkVertex> &vertices, const std::span<uint16_t> &indices);

    static void BlitImage(const VkCommandBuffer &commandBuffer, const VulkanImage &srcImage, const VulkanImage &dstImage, VkImageLayout srcLayout, VkImageLayout dstLayout, VkImageAspectFlags aspectFlags);

    [[nodiscard]] const QueueFamilyIndices &queue_family_indices() const {
        return queueFamilyIndices;
    }

    [[nodiscard]] GLFWwindow * window() const {
        return glfwWindow;
    }

    [[nodiscard]] VkInstance vk_instance() const {
        return instance;
    }

    [[nodiscard]] VkSurfaceKHR vk_surface() const {
        return surface;
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

    [[nodiscard]] VkSwapchainKHR swap_chain() const {
        return swapChain;
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

    [[nodiscard]] VkPipelineLayout pipeline_layout() const {
        return pipelineLayout;
    }

    [[nodiscard]] VkPipeline graphics_pipeline() const {
        return graphicsPipeline;
    }

    [[nodiscard]] VkPresentModeKHR present_mode() const {
        return presentMode;
    }

    [[nodiscard]] VkSampleCountFlagBits msaa_samples() const {
        return msaaSamples;
    }

    [[nodiscard]] const VkPhysicalDeviceProperties & device_properties() const {
        return deviceProperties;
    }

    [[nodiscard]] const VkPhysicalDeviceMemoryProperties & memory_properties() const {
        return memoryProperties;
    }

    [[nodiscard]] bool is_integrated_gpu() const {
        return isIntegratedGPU;
    }

    [[nodiscard]] bool is_dynamic_rendering() const {
        return dynamicRendering;
    }

    [[nodiscard]] bool isDeviceExtensionAvailable(const char * extension) {
        return std::ranges::any_of(deviceExtensions, [extension](const char *ext) {
            return strcmp(ext, extension) == 0;
        });
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

    // static void FramebufferResizeCallback(GLFWwindow *window, int width, int height);
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

    struct ComputePushConstants {
        glm::vec4 color1;
        glm::vec4 color2;
    };

#ifndef _NDEBUG
    const std::array<const char *, 1> validationLayers = {
            "VK_LAYER_KHRONOS_validation"
    };

    std::vector<const char *> deviceExtensions = {
            13,
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkDebugUtilsMessengerEXT debugMessenger;
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData);
    static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger);
    static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks *pAllocator);
#endif

    // static void FramebufferResizeCallback(GLFWwindow *window, int width, int height);

    // static inline GLFWwindow *CreateVulkanWindow(float width, float height);
    inline void InitializeInstance();
    inline void PickPhysicalDevice();
    inline void CreateLogicalDevice();
    inline void CreateQueryPool();
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
    inline void InitDefaultData();

    inline void FindQueueFamilies(const VkPhysicalDevice &gpu);
    [[nodiscard]] inline SwapChainSupportDetails QuerySwapChainSupport(const VkPhysicalDevice &gpu) const;
    inline static VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats);
    inline static VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes);
    [[nodiscard]] inline VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) const;
    inline void CleanupSwapChain();
    inline static VkBufferCreateInfo CreateBufferCreateInfo(VkDeviceSize size, VkBufferUsageFlags usage, VkSharingMode sharingMode);
    inline static VkImageCreateInfo CreateImageCreateInfo(VkFormat format, VkExtent3D extent, VkImageUsageFlags usage, VkImageTiling tiling, VkImageLayout layout, VkImageCreateFlags flags);

    // inline void UpdatePushConstants(MeshPushConstants &meshPushConstants) const;
    inline void SavePipelineCache() const;

    inline void UpdateScene();

    GLFWwindow *glfwWindow;
    Camera *camera;

    VkViewport viewport;
    VkRect2D scissor;

    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueryPool queryPool;
    VkPipelineCache pipelineCache;
    VkQueue graphicsQueue;
    VkQueue computeQueue;
    VkQueue presentQueue;
    VkSwapchainKHR swapChain;
    VkSurfaceFormatKHR surfaceFormat;
    VkRenderPass renderPass;

    DescriptorAllocator mainDescriptorAllocator;
    VkDescriptorSet mainDescriptorSet;
    VkDescriptorSetLayout mainDescriptorSetLayout;

    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;
    VkPipeline computePipeline;

    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> frames;
    VkCommandPool immediateCommandPool;

    std::vector<VkFramebuffer> swapChainFramebuffers;
    std::vector<VkImage> swapChainImages;
    std::vector<VkImageView> swapChainImageViews;

    VkPresentModeKHR presentMode;
    VkExtent2D swapChainExtent;

    VkPhysicalDeviceProperties deviceProperties;
    VkPhysicalDeviceMemoryProperties memoryProperties;

    std::vector<std::shared_ptr<MeshAsset>> meshAssets;

    VulkanImage drawImage{};
    VulkanImage depthImage{};
    VulkanImage defaultImage{};

    VkSampler textureSamplerLinear;
    VkSampler textureSamplerNearest;

    VkDescriptorSetLayout sceneDescriptorSetLayout;

    VkMaterialInstance defaultMaterialInstance;
    VkGLTFMetallic_Roughness metalRoughMaterial;

    VkDrawContext mainDrawContext;
    std::unordered_map<std::string, std::shared_ptr<Node>> loadedNodes;
    SceneData sceneData{};

    std::shared_ptr<VkMemoryManager> memoryManager;
};


#endif
