#ifndef D3D12_STUFF_D3D12_RENDERER_H
#define D3D12_STUFF_D3D12_RENDERER_H

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl.h>
#include <vector>
#include <directxmath.h>
#include <stdexcept>

#pragma comment(lib, "d3d12.lib")
using Microsoft::WRL::ComPtr;

#define HrToString(x) std::string("HRESULT: ") + std::to_string(x)
#define ThrowIfFailed(x) do { HRESULT hr = (x); if(FAILED(hr)) { throw std::runtime_error(HrToString(hr)); } } while(0)

class D3D12Renderer{
public:
    explicit D3D12Renderer(FLOAT width, FLOAT height);

    void Init(const HWND &hwnd, const WCHAR *vertexShaderSourceFile, const WCHAR *pixelShaderSourceFile);
    void Render();
    void Shutdown();

    int InitWindow(HINSTANCE hInstance, int nCmdShow, LPCSTR title) {
        if (init)
            return 0;

        WNDCLASSEX wc = {0};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = title;
        RegisterClassEx(&wc);

        RECT rc = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

        auto hwnd = CreateWindow(wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInstance, this);

        Init(hwnd, L"vertex.hlsl", L"vertex.hlsl");
        ShowWindow(hwnd, nCmdShow);

        init = true;

        MSG msg = {};
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        Shutdown();

        return static_cast<int>(msg.wParam);
    }
private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        D3D12Renderer *pRenderer;
        if (uMsg == WM_CREATE) {
            auto pCreate = reinterpret_cast<LPCREATESTRUCT>(lParam);
            pRenderer = reinterpret_cast<D3D12Renderer *>(pCreate->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pRenderer));
        } else {
            pRenderer = reinterpret_cast<D3D12Renderer *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }

        switch (uMsg) {
            case WM_KEYDOWN:
                if (wParam == VK_ESCAPE) {
                    if (pRenderer) {
                        pRenderer->Shutdown();
                    }
                    PostQuitMessage(0);
                }
                break;
            case WM_DESTROY:
                if (pRenderer) {
                    pRenderer->Shutdown();
                }
                PostQuitMessage(0);
                break;
            case WM_PAINT:
                if (pRenderer) {
                    pRenderer->Render();
                }
                break;
            default:
                return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }

        return 0;
    }
    FLOAT width;
    FLOAT height;

    bool init = false;

    struct Vertex {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT4 color;
    };

    static const int frameCount = 2;
    D3D12_VIEWPORT viewport;
    D3D12_RECT scissorRect;
    ComPtr<ID3D12Device> device;
    ComPtr<IDXGISwapChain3> swapChain;
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12Resource> renderTargets[frameCount];
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12PipelineState> pipelineState;

    ComPtr<ID3D12Resource> vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

    ComPtr<ID3D12Fence> fence;
    UINT64 fenceValue;
    HANDLE fenceEvent;
    UINT frameIndex;

    UINT rtvDescriptorSize;

    void LoadPipeline(const HWND &hwnd);
    void LoadAssets(const WCHAR *vertexShaderSourceFile, const WCHAR *pixelShaderSourceFile);
    void PopulateCommandList();
    void WaitForPreviousFrame();
};

#endif //D3D12_STUFF_D3D12_RENDERER_H
