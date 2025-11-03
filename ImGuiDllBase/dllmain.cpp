#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <thread>
#include <atomic>
#include "MinHook.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "draw.h"

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
extern PresentFn oPresent;

LRESULT CALLBACK WndProcHook(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

bool g_Running = true;
PresentFn oPresent = nullptr;
HWND g_hWnd = NULL;
WNDPROC oWndProc = nullptr;

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
bool g_ImGuiInitialized = false;

void CreateRenderTarget(IDXGISwapChain* swapChain) {
    if (g_mainRenderTargetView) return;
    ID3D11Texture2D* pBackBuffer = nullptr;
    if (SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer))) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

void InitImGuiOnce(IDXGISwapChain* swapChain) {
    if (g_ImGuiInitialized) return;

    if (!g_pd3dDevice || !g_pd3dDeviceContext) {
        swapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice);
        if (g_pd3dDevice) g_pd3dDevice->GetImmediateContext(&g_pd3dDeviceContext);
    }

    DXGI_SWAP_CHAIN_DESC sd;
    if (SUCCEEDED(swapChain->GetDesc(&sd))) {
        g_hWnd = sd.OutputWindow;
    }

    CreateRenderTarget(swapChain);

    // Èíèöèàëèçàöèÿ ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 14.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    io.FontDefault = io.Fonts->Fonts.back();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_hWnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    //io.MouseDrawCursor = false; - Disables cursor
    //ShowCursor(FALSE); - Disables cursor 2

    oWndProc = (WNDPROC)SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)WndProcHook);

    g_ImGuiInitialized = true;
}

void RenderImGui() {
    if (!g_Running || !g_ImGuiInitialized)
        return;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (GetAsyncKeyState(VK_INSERT) & 0x1) {
        gui::enabled = !gui::enabled;
    }
    if (gui::enabled) {
        gui::Draw();
    }

    ImGui::Render();
    if (g_pd3dDeviceContext && g_mainRenderTargetView) {
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
}

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (!g_ImGuiInitialized) {
        InitImGuiOnce(pSwapChain);
    }

    if (g_ImGuiInitialized) {
        RenderImGui();
    }

    return oPresent(pSwapChain, SyncInterval, Flags);
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProcHook(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (g_ImGuiInitialized) {
        ImGuiIO& io = ImGui::GetIO();

        if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
            return true;

        if (io.WantCaptureMouse)
            return true;
    }
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

void* GetPresentAddress() {
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = DefWindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "TempClass";
    wc.hIconSm = NULL;
    RegisterClassExA(&wc);

    HWND hWnd = CreateWindowA(wc.lpszClassName, "Temp", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);

    IDXGISwapChain* pSwapChain = nullptr;
    ID3D11Device* pd3dDevice = nullptr;
    ID3D11DeviceContext* pd3dContext = nullptr;

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.BufferDesc.Width = 100;
    sd.BufferDesc.Height = 100;

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, nullptr, 0, D3D11_SDK_VERSION, &sd, &pSwapChain, &pd3dDevice, &featureLevel, &pd3dContext);

    void* presentAddr = nullptr;
    if (SUCCEEDED(hr) && pSwapChain) {
        void** vtable = *reinterpret_cast<void***>(pSwapChain);
        presentAddr = vtable[8];
    }

    if (pd3dContext) pd3dContext->Release();
    if (pd3dDevice) pd3dDevice->Release();
    if (pSwapChain) pSwapChain->Release();
    DestroyWindow(hWnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    return presentAddr;
}

DWORD WINAPI MainThread(LPVOID lpReserved) {
    if (MH_Initialize() != MH_OK) return false;

    void* presentAddr = GetPresentAddress();
    if (!presentAddr) return false;

    if (MH_CreateHook(presentAddr, &hkPresent, reinterpret_cast<LPVOID*>(&oPresent)) != MH_OK) {
        return false;
    }
    if (MH_EnableHook(presentAddr) != MH_OK) {
        return false;
    }

    return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
    }
    return TRUE;
}

