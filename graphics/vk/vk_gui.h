#ifndef D3D12_STUFF_VK_GUI_H
#define D3D12_STUFF_VK_GUI_H

#include <memory>
#include <../../third_party/glfw-3.4/include/glfw/glfw3.h>

#include "file.h"

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
    void CreateImGuiDescriptorPool();

    GLFWwindow *window;

    VkDescriptorPool imguiDescriptorPool;
    EngineStats stats{};

    std::unique_ptr<VkRenderer> renderer;
};

#endif //D3D12_STUFF_VK_GUI_H
