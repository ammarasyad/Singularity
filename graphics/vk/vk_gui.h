#ifndef D3D12_STUFF_VK_GUI_H
#define D3D12_STUFF_VK_GUI_H

#include <memory>
#include <glfw/glfw3.h>

#include "camera.h"

class VkRenderer;
typedef struct VkDescriptorPool_T * VkDescriptorPool;

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
    static void KeyboardCallback(GLFWwindow *window, int key, int scancode, int action, int mods);
    void CreateImGuiDescriptorPool();

    bool mouseHeldDown = false;

    float deltaTime = 0;

    GLFWwindow *window;
    Camera camera{0, 2, 0};

    VkDescriptorPool imguiDescriptorPool;
    EngineStats stats{};

    std::unique_ptr<VkRenderer> renderer;
};

#endif //D3D12_STUFF_VK_GUI_H
