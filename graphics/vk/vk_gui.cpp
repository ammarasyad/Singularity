#include "vk_gui.h"

#include <chrono>
#include <../../third_party/imgui/backends/imgui_impl_glfw.h>
#include <../../third_party/imgui/backends/imgui_impl_vulkan.h>
#include "../vk_renderer.h"

static constexpr uint32_t MIN_IMAGE_COUNT = 2;

VkGui::VkGui(const int width, const int height, const bool dynamicRendering, const bool asyncCompute): imguiDescriptorPool(VK_NULL_HANDLE) {
    // GLFW initialization
    glfwSetErrorCallback(errorCallback);
    if (!glfwInit())
        throw std::runtime_error("Failed to initialize GLFW");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    window = glfwCreateWindow(width, height, "Vulkan", nullptr, nullptr);
    if (!glfwVulkanSupported())
        throw std::runtime_error("Vulkan not supported");

    glfwSetFramebufferSizeCallback(window, VkRenderer::FramebufferResizeCallback);

    // VkRenderer initialization
    renderer = std::make_unique<VkRenderer>(window, dynamicRendering, asyncCompute);

    const auto instance = renderer->vk_instance();
    const auto physicalDevice = renderer->physical_device();
    const auto logicalDevice = renderer->logical_device();
    const auto queueFamily = renderer->queue_family_indices().graphicsFamily.value();
    const auto queue = renderer->graphics_queue();
    const auto renderPass = renderer->render_pass();
    const auto msaaSamples = renderer->msaa_samples();
    const auto pipelineCache = renderer->pipeline_cache();

    const auto format = renderer->surface_format().format;

    CreateImGuiDescriptorPool();

    // ImGui initialization
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkan_InitInfo initInfo{
        instance,
        physicalDevice,
        logicalDevice,
        queueFamily,
        queue,
        imguiDescriptorPool,
        renderPass,
        MIN_IMAGE_COUNT,
        MIN_IMAGE_COUNT,
        msaaSamples,
        pipelineCache,
        0,
        dynamicRendering
    };

    if (dynamicRendering) {
        initInfo.PipelineRenderingCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &format
        };
    }

    ImGui_ImplVulkan_Init(&initInfo);
    ImGui_ImplVulkan_CreateFontsTexture();
}

void VkGui::Loop() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (renderer->needs_resizing()) {
            renderer->RecreateSwapChain();
            continue;
        }

        auto start = std::chrono::high_resolution_clock::now();

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        ImGui::NewFrame();

        {
            ImGui::Begin("Title or whatever");
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Frame time: %.2f ms", stats.frameTime);
            ImGui::Text("Mesh Draw Time: %.2f ms", stats.meshDrawTime);
            ImGui::Text("Draw call count: %d", stats.drawCallCount);
            ImGui::Text("Triangle count: %d", stats.triangleCount);

            ImGui::SliderFloat("FOV", &renderer->fov, 30.f, 120.f);

            ImGui::End();
        }

        ImGui::Render();

        renderer->Render(stats);

        auto end = std::chrono::high_resolution_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        stats.frameTime = static_cast<float>(elapsed) / 1000.f;
    }
}

void VkGui::Shutdown() const {
    vkDeviceWaitIdle(renderer->logical_device());

    ImGui_ImplVulkan_Shutdown();

    vkDestroyDescriptorPool(renderer->logical_device(), imguiDescriptorPool, nullptr);

    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    renderer->Shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();
}


void VkGui::errorCallback(const int error, const char *description) {
    fprintf(stderr, "Error %d: %s\n", error, description);
}

void VkGui::CreateImGuiDescriptorPool() {
    std::array<VkDescriptorPoolSize, 1> poolSizes{
        {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1
        }};

    VkDescriptorPoolCreateInfo poolinfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        VK_NULL_HANDLE,
        VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        1,
        static_cast<uint32_t>(poolSizes.size()),
        poolSizes.data()
    };

    VK_CHECK(vkCreateDescriptorPool(renderer->logical_device(), &poolinfo, nullptr, &imguiDescriptorPool));
}
