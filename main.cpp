#include "min_windows.h"
#include "graphics/d3d12_renderer.h"
#include <fstream>

#include "graphics/vk_renderer.h"
#include "graphics/vk/vk_gui.h"

enum class RendererType {
    D3D12,
    VK
};

static constexpr auto rendererType = RendererType::VK;

#if !defined(NDEBUG) && defined(_WIN32)
static void RedirectIOOutput() {
    AllocConsole();
    FILE *stream;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
}
#endif

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
#ifndef NDEBUG
    RedirectIOOutput();
#else
    freopen("log.txt", "w", stdout);
    freopen("log.txt", "w", stderr);
    // std::ofstream logFile("log.txt");
    // std::cout.rdbuf(logFile.rdbuf());
#endif

    switch (rendererType) {
        case RendererType::D3D12: {
            D3D12Renderer renderer(1920, 1080);
            return renderer.InitWindow(hInstance, nCmdShow, "D3D12 Renderer");
        }
        case RendererType::VK: {
            auto start = std::chrono::high_resolution_clock ::now();
            VkGui gui(2560, 1440, true, true);
            auto end = std::chrono::high_resolution_clock ::now();
            printf("Initialization took %lld ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
            gui.Loop();
            gui.Shutdown();
        }
    }

    return 0;
}
#else
int main()
{
    if constexpr (rendererType == RendererType::D3D12) {
        throw std::runtime_error("D3D12 renderer is not supported on this platform");
    } else {
        VkGui gui(1920, 1080, true, true);
        gui.Loop();
        gui.Shutdown();
    }

    return 0;
}
#endif
