#ifdef _WIN32

#if !defined(NOMINMAX)
#define NOMINMAX
#endif

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif

#endif

#include <windows.h>
#include <fstream>
#include <iostream>
#include "graphics/d3d12_renderer.h"
#include "graphics/vk_renderer.h"
#include "graphics/vk/vk_gui.h"

enum class RendererType {
    D3D12,
    VK
};

static constexpr auto rendererType = RendererType::VK;

#ifndef NDEBUG
static void RedirectIOOutput() {
    AllocConsole();
    FILE *stream;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
}
#endif

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
#ifndef NDEBUG
    RedirectIOOutput();
#else
    std::ofstream logFile("log.txt");
    std::cout.rdbuf(logFile.rdbuf());
#endif

    switch (rendererType) {
        case RendererType::D3D12: {
            D3D12Renderer renderer(1920, 1080);
            return renderer.InitWindow(hInstance, nCmdShow, "D3D12 Renderer");
        }
        case RendererType::VK: {
            VkGui gui(2560, 1440, false);
            gui.Loop();
            gui.Shutdown();
        }
    }

    return 0;
}
