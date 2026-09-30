// PDX V1 Menu — Standalone DX11 + Win32 + ImGui
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <windows.h>
#include <vector>

#include "menu_style.h"
#include "input.h"
#include "menu.h"
#include "config_system.h"
#include "pdx_logo_data.h"
#include "user_profile.h"

// ─── D3D11 globals ─────────────────────────────────────────────────────────

static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static bool                     g_SwapChainOccluded = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ─── FontAwesome icon range ─────────────────────────────────────────────────

static const ImWchar fa_icon_ranges[] = { 0xf000, 0xf999, 0 };

// ─── Logo texture creation ──────────────────────────────────────────────────

static ID3D11ShaderResourceView* CreateLogoTexture() {
    // Convert logo to white + original alpha so accent tint gives exact color.
    // Use mipmaps for clean downscaling to title-bar size.
    size_t pixelCount = (size_t)g_pdxLogoWidth * g_pdxLogoHeight;
    std::vector<unsigned char> whiteData(pixelCount * 4);
    for (size_t i = 0; i < pixelCount; i++) {
        whiteData[i * 4 + 0] = 255; // B
        whiteData[i * 4 + 1] = 255; // G
        whiteData[i * 4 + 2] = 255; // R
        whiteData[i * 4 + 3] = g_pdxLogoData[i * 4 + 3]; // A (preserve original)
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width     = g_pdxLogoWidth;
    desc.Height    = g_pdxLogoHeight;
    desc.MipLevels = 0; // auto-generate full mip chain
    desc.ArraySize = 1;
    desc.Format    = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage     = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    ID3D11Texture2D* pTexture = nullptr;
    HRESULT hr = g_pd3dDevice->CreateTexture2D(&desc, nullptr, &pTexture);
    if (FAILED(hr)) return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = desc.Format;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels       = (UINT)-1; // all mip levels

    ID3D11ShaderResourceView* srv = nullptr;
    hr = g_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, &srv);
    if (SUCCEEDED(hr)) {
        // Upload base level + generate mipmaps
        g_pd3dDeviceContext->UpdateSubresource(pTexture, 0, nullptr,
            whiteData.data(), g_pdxLogoWidth * 4, 0);
        g_pd3dDeviceContext->GenerateMips(srv);
    }
    pTexture->Release();

    return srv;
}

// ─── Main ──────────────────────────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // DPI awareness
    ImGui_ImplWin32_EnableDpiAwareness();

    // Create window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L,
                       hInstance, nullptr, nullptr, nullptr, nullptr,
                       L"PDX", nullptr };
    ::RegisterClassExW(&wc);

    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"PDX V1",
        WS_OVERLAPPEDWINDOW, 100, 100, 1000, 600,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // Don't save imgui.ini

    // Setup backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // ─── Logo texture ────────────────────────────────────────────────
    ID3D11ShaderResourceView* logoSRV = CreateLogoTexture();
    MenuStyle::g_logoTexture = (ImTextureID)logoSRV;
    MenuStyle::g_logoW = (float)g_pdxLogoWidth;
    MenuStyle::g_logoH = (float)g_pdxLogoHeight;

    // ─── HV user-profile fetch toggle ────────────────────────────────
    // Flip to TRUE only when this binary is running inside the cheat path
    // with the hypervisor loaded — then UserProfile::Fetch() will pull the
    // real username / avatar / expiry via PDX_CALL_USER_PROFILE_GET. Left
    // FALSE for standalone dev so we don't issue VMCALLs without an HV.
    UserProfile::SetHvFetchEnabled(false);
    UserProfile::SetDevice(g_pd3dDevice);

    // ─── Fonts ───────────────────────────────────────────────────────

    // Regular font — Tahoma 14px
    MenuStyle::g_fontRegular = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\tahoma.ttf", 14.0f);
    if (!MenuStyle::g_fontRegular)
        MenuStyle::g_fontRegular = io.Fonts->AddFontDefault();

    // Bold font — Tahoma Bold 14px
    MenuStyle::g_fontBold = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\tahomabd.ttf", 14.0f);
    if (!MenuStyle::g_fontBold)
        MenuStyle::g_fontBold = MenuStyle::g_fontRegular;

    // Icon font (FontAwesome)
    ImFontConfig iconConfig;
    iconConfig.MergeMode = false;
    iconConfig.PixelSnapH = true;
    iconConfig.GlyphMinAdvanceX = 13.0f;

    // Try multiple paths: exe dir, project root, fonts subdir
    const char* fontPaths[] = {
        "fa-solid-900.ttf",
        "../../fa-solid-900.ttf",       // build/Release -> project root
        "../fa-solid-900.ttf",          // build -> project root
        "fonts/fa-solid-900.ttf",
    };
    for (const char* path : fontPaths) {
        MenuStyle::g_fontIcon = io.Fonts->AddFontFromFileTTF(
            path, 16.0f, &iconConfig, fa_icon_ranges);
        if (MenuStyle::g_fontIcon) break;
    }

    io.Fonts->Build();

    // ─── Style ───────────────────────────────────────────────────────

    MenuStyle::SetupStyle();

    // ─── Init menu ───────────────────────────────────────────────────

    Menu::Init();

    // Set up persistent config storage. The game name decides the folder:
    //   %USERPROFILE%\Documents\pdx\<game>\
    // Change this when distributing for a different title.
    ConfigSystem::Init("pdx v1");

    // ─── Main loop ───────────────────────────────────────────────────

    ImVec4 clearColor(0.04f, 0.04f, 0.05f, 1.0f);
    bool done = false;
    bool insertWasDown = false;

    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done) break;

        // Handle occluded
        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        // Handle resize
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // Update input from hypervisor
        Input::Update();
        ImGui_ImplWin32_UpdateInputFromHV();

        // Toggle menu via the engine-owned gate. PollMenuToggle owns the
        // was-down / armed state internally — we just feed it the live
        // "is the bound key down" boolean.
        if (Menu::PollMenuToggle(Input::IsKeyDown(Menu::GetMenuKey())))
            Menu::ToggleVisibility();

        // Per-frame HV user-profile fetch attempt — idempotent. No-op when
        // the gate is off; one-shot when on (caches after first success).
        // Pre-cache the device on UserProfile so the menu can lazy-upload
        // the avatar SRV without threading the device through Menu::Render.
        UserProfile::Fetch();

        // Start ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        Menu::Render();

        // Rendering
        ImGui::Render();
        const float cc[4] = { clearColor.x, clearColor.y, clearColor.z, clearColor.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_pSwapChain->Present(1, 0); // VSync
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    // Persist any pending window positions / settings before tearing imgui down.
    ConfigSystem::Flush();

    // Cleanup
    UserProfile::ReleaseAvatarTexture();
    if (logoSRV) logoSRV->Release();
    MenuStyle::g_logoTexture = ImTextureID();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// ─── D3D11 helpers ─────────────────────────────────────────────────────────

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            levels, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
