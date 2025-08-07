#ifndef D3D12_STUFF_VK_GUI_H
#define D3D12_STUFF_VK_GUI_H

#ifdef _WIN32
#include <glfw/glfw3.h>
#else
// use system glfw
#include <GLFW/glfw3.h>
#endif

#include "engine/camera.h"
#include "graphics/vk_renderer.h"

class VkRenderer;
typedef VkDescriptorPool_T * VkDescriptorPool;

struct EngineStats {
    float frameTime;
    uint32_t triangleCount;
    uint32_t drawCallCount;
    float meshDrawTime;
};

class VkGui {
public:
    explicit VkGui(int width, int height, bool dynamicRendering = true, bool asyncCompute = true);
    ~VkGui() = default;

    void Loop();
    void Shutdown() const;
private:
    static void errorCallback(int error, const char *description);
    static void FramebufferResizeCallback(GLFWwindow *window, int width, int height);
    static void MouseButtonCallback(GLFWwindow *window, int button, int action, int mods);
    static void MouseCursorCallback(GLFWwindow *window, double xpos, double ypos);
    static void KeyboardCallback(GLFWwindow *window, int key, int scancode, int action, int mods);
    void CreateImGuiDescriptorPool();

    bool mouseHeldDown = false;

    bool isKeyPressed = false;
    int pressedKeys = 0;

    float deltaTime = 0;

    GLFWwindow *window;
    Camera camera{9, 2, 0};

    VkDescriptorPool imguiDescriptorPool;
    EngineStats stats{};

    VkRenderer renderer;
};

#endif //D3D12_STUFF_VK_GUI_H
