#include "App.h"
#include "Panels.h"
#include "TrayIcon.h"
#include "UiState.h"
#include "storagecleaner/ConfigStore.h"

#include "resource.h"

#include <d3d11.h>
#include <tchar.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace storagecleaner::ui {

namespace {

// Estado do dispositivo D3D11, boilerplate padrao dos exemplos oficiais do
// Dear ImGui (backend Win32 + DX11) — ver
// imgui/examples/example_win32_directx11.
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
bool g_swapChainOccluded = false;

UiState* g_uiState = nullptr; // acessado pelo WndProc, que e' uma callback C

bool CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                           createDeviceFlags, featureLevelArray, 2,
                                           D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice,
                                           &featureLevel, &g_pd3dDeviceContext);
    if (FAILED(hr)) return false;

    ID3D11Texture2D* backBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    g_pd3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_mainRenderTargetView);
    backBuffer->Release();
    return true;
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

void CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    g_pd3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_mainRenderTargetView);
    backBuffer->Release();
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;

    switch (msg) {
        case WM_SIZE:
            if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
                CleanupRenderTarget();
                g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam),
                                           DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget();
            }
            return 0;

        case WM_SYSCOMMAND:
            // Bloqueia o menu do sistema no ALT (comportamento padrao do
            // exemplo ImGui, evita o app "travar" a UI aguardando o menu nativo).
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;

        case kTrayCallbackMessage:
            if (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP) ShowTrayContextMenu(hwnd);
            return 0;

        case WM_COMMAND: {
            TrayCommand cmd = HandleTrayCommand(wParam);
            if (cmd == TrayCommand::Open) {
                ::ShowWindow(hwnd, SW_SHOW);
                ::SetForegroundWindow(hwnd);
                if (g_uiState) g_uiState->windowVisible = true;
            } else if (cmd == TrayCommand::ScanNow) {
                if (g_uiState) g_uiState->StartScan();
            } else if (cmd == TrayCommand::Exit) {
                if (g_uiState) g_uiState->requestExit = true;
            }
            return 0;
        }

        case WM_CLOSE:
            // Fechar pela barra de titulo so esconde a janela: o processo
            // continua rodando em segundo plano (varredura leve, agendamento
            // de auto-clean) ate o usuario escolher "Sair" no menu da bandeja.
            if (g_uiState && g_uiState->config.minimizeToTrayOnClose) {
                ::ShowWindow(hwnd, SW_HIDE);
                g_uiState->windowVisible = false;
                return 0;
            }
            if (g_uiState) g_uiState->requestExit = true;
            return 0;

        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int RunApp(bool startMinimized) {
    UiState state;
    state.config = LoadConfig();
    g_uiState = &state;

    HINSTANCE hInstance = ::GetModuleHandleW(nullptr);
    // Mesmo icone (resources/app.rc) usado na janela/taskbar e na bandeja
    // (TrayIcon.cpp) — LoadImageW com SM_CX/CYICON e SM_CX/CYSMICON pede ao
    // Win32 o tamanho já mais próximo do necessário em vez de depender de
    // reescala posterior.
    HICON appIcon = static_cast<HICON>(::LoadImageW(
        hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, ::GetSystemMetrics(SM_CXICON),
        ::GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    HICON appIconSmall = static_cast<HICON>(::LoadImageW(
        hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON),
        ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));

    WNDCLASSEXW wc{sizeof(wc),        CS_CLASSDC, WndProc, 0L,   0L,
                  hInstance, appIcon,   nullptr, nullptr, nullptr,
                  L"StorageCleanerWindowClass", appIconSmall};
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Otimizador de Armazenamento",
                               WS_OVERLAPPEDWINDOW, 100, 100, 1024, 720, nullptr, nullptr,
                               wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, startMinimized ? SW_HIDE : SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);
    state.windowVisible = !startMinimized;

    CreateTrayIcon(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        state.Tick();
        if (state.requestExit) break;

        // A janela pode estar escondida na bandeja: nao ha necessidade de
        // desenhar frames de ImGui nesse caso, so processar o estado acima
        // (scan/clean/agendamento continuam rodando normalmente).
        if (!state.windowVisible) {
            ::Sleep(200);
            continue;
        }

        if (g_swapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            ::Sleep(10);
            continue;
        }
        g_swapChainOccluded = false;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("StorageCleanerMain", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        if (ImGui::BeginTabBar("MainTabs")) {
            if (ImGui::BeginTabItem("Dashboard")) {
                DrawDashboardPanel(state);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Resultados")) {
                DrawResultsPanel(state);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Configuracoes")) {
                DrawSettingsPanel(state);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Historico")) {
                DrawHistoryPanel(state);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();

        ImGui::Render();
        const float clearColor[4] = {0.08f, 0.08f, 0.09f, 1.0f};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_swapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    RemoveTrayIcon();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    g_uiState = nullptr;
    return 0;
}

} // namespace storagecleaner::ui
