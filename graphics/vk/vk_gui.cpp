#include "vk_gui.h"

#include <chrono>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include "vk_renderer.h"

static constexpr uint32_t MIN_IMAGE_COUNT = 2;

// ImGui helper function
namespace ImGui {
    template<typename Getter, typename Setter>
    void SliderFloat(const char *label, Getter getter, Setter setter, float min, float max, const char *format = "%.3f", ImGuiSliderFlags flags = 0) {
        float temp = getter();
        float newValue = temp;

        ImGui::SliderFloat(label, &newValue, min, max, format, flags);

        if (newValue != temp) {
            setter(newValue);
        }
    }
}

VkGui::VkGui(const int width, const int height, const bool dynamicRendering, const bool asyncCompute) : imguiDescriptorPool(VK_NULL_HANDLE) {
    // GLFW initialization
    glfwSetErrorCallback(errorCallback);
    if (!glfwInit()) [[unlikely]]
        throw std::runtime_error("Failed to initialize GLFW");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    window = glfwCreateWindow(width, height, "Vulkan", nullptr, nullptr);
    if (!glfwVulkanSupported())
        throw std::runtime_error("Vulkan not supported");

    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

    glfwSetInputMode(window, GLFW_STICKY_MOUSE_BUTTONS, GLFW_TRUE);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, FramebufferResizeCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetKeyCallback(window, KeyboardCallback);
    glfwSetWindowCloseCallback(window, [](GLFWwindow *window) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    });
    // glfwSetMouseButtonCallback(window, [](GLFWwindow *window, int button, int action, int mods) {
    //     ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
    // });

    // VkRenderer initialization
    renderer = std::make_unique<VkRenderer>(window, &camera, dynamicRendering, asyncCompute);

    const auto instance = renderer->vk_instance();
    const auto physicalDevice = renderer->physical_device();
    const auto logicalDevice = renderer->logical_device();
    const auto queueFamily = renderer->queue_family_indices().graphicsFamily.value();
    const auto queue = renderer->graphics_queue();
    const auto renderPass = renderer->render_pass();
    const auto msaaSamples = renderer->msaa_samples();
    const auto pipelineCache = renderer->pipeline_cache();

    const auto format = renderer->surface_format().format;
    const auto depthFormat = renderer->depth_format();

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
            .pColorAttachmentFormats = &format,
            .depthAttachmentFormat = depthFormat,
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

        if (mouseHeldDown) {
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);
            camera.ProcessMouseInput(xpos, ypos);
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

            const auto position = camera.Position();
            ImGui::Text("Camera Position: %.2f, %.2f, %.2f", position.x, position.y, position.z);
            ImGui::Text("Camera Pitch: %.2f, Yaw: %.2f", camera.pitch, camera.yaw);

            ImGui::SliderFloat("FOV", [&](){ return camera.Fov(); }, [&](float &newValue){ camera.setFov(newValue); }, 30.f, 120.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::End();
        }

        ImGui::Render();

        renderer->Render(stats);

        auto end = std::chrono::high_resolution_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        stats.frameTime = deltaTime = static_cast<float>(elapsed) / 1000.f;
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

void VkGui::FramebufferResizeCallback(GLFWwindow *window, int, int) {
    const auto gui = static_cast<VkGui *>(glfwGetWindowUserPointer(window));
    gui->renderer->FramebufferNeedsResizing();
}

void VkGui::MouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {
    const auto gui = static_cast<VkGui *>(glfwGetWindowUserPointer(window));

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            gui->mouseHeldDown = true;
        } else if (action == GLFW_RELEASE) {
            gui->mouseHeldDown = false;
        }
    }
}

void VkGui::KeyboardCallback(GLFWwindow *window, int key, int scancode, int action, int mods) {
    const auto gui = static_cast<VkGui *>(glfwGetWindowUserPointer(window));
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        if (key == GLFW_KEY_F2) {
            gui->renderer->Screenshot();
        }
    }

    gui->camera.ProcessKeyboardInput(key, action, gui->deltaTime);
}

void VkGui::CreateImGuiDescriptorPool() {
    static constexpr std::array<VkDescriptorPoolSize, 1> poolSizes{
        {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1
        }};

    static constexpr VkDescriptorPoolCreateInfo poolinfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        VK_NULL_HANDLE,
        VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        1,
        poolSizes.size(),
        poolSizes.data()
    };

    VK_CHECK(vkCreateDescriptorPool(renderer->logical_device(), &poolinfo, nullptr, &imguiDescriptorPool));
}
