#include "App.h"
#include "Panels.h"
#include "Theme.h"
#include "TrayIcon.h"
#include "UiState.h"
#include "storagecleaner/ConfigStore.h"
#include "storagecleaner/Utils.h"

#include "resource.h"

#include <d3d11.h>
#include <tchar.h>

#include <optional>

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

void SaveWindowPlacementToDisk(HWND hwnd);

// Só chamada por ação explícita do usuário (menu "Abrir" ou clique num balão
// da bandeja) — o app nunca rouba o foco por conta própria.
void ShowMainWindow(HWND hwnd) {
    bool maximize = g_uiState && g_uiState->showMaximizedOnOpen;
    if (g_uiState) g_uiState->showMaximizedOnOpen = false;
    // SW_SHOW não tira uma janela do estado minimizado na barra de tarefas.
    if (::IsIconic(hwnd))
        ::ShowWindow(hwnd, SW_RESTORE);
    else
        ::ShowWindow(hwnd, maximize ? SW_SHOWMAXIMIZED : SW_SHOW);
    ::SetForegroundWindow(hwnd);
    if (g_uiState) g_uiState->windowVisible = true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;

    switch (msg) {
        case WM_ENDSESSION:
            // Desligar/logoff encerra o processo sem passar pelo fim de
            // RunApp — e, com o app na bandeja, esse é o jeito mais comum
            // de ele terminar.
            if (wParam) SaveWindowPlacementToDisk(hwnd);
            return 0;

        case WM_SIZE:
            if (g_uiState) g_uiState->windowMinimized = (wParam == SIZE_MINIMIZED);
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
            // Clique explícito do usuário num balão (varredura agendada pronta,
            // pouco espaço etc.): só aqui a janela vem para frente sozinha.
            else if (lParam == NIN_BALLOONUSERCLICK) ShowMainWindow(hwnd);
            return 0;

        case WM_COMMAND: {
            TrayCommand cmd = HandleTrayCommand(wParam);
            if (cmd == TrayCommand::Open) {
                ShowMainWindow(hwnd);
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

// Aplica a posição salva, se ela ainda cair em algum monitor (monitor
// desconectado ou resolução menor deixariam a janela fora da tela).
// Retorna false quando não aplicou e o chamador deve mostrar a janela.
// Aplicada, ela vence o "Executar: minimizada/maximizada" de um atalho
// (STARTUPINFO só vale no SW_SHOWDEFAULT) — a última posição real do usuário
// é a informação mais recente.
bool RestoreWindowPlacement(HWND hwnd, const WindowPlacement& saved, bool startHidden) {
    if (!saved.valid) return false;
    RECT rect{saved.x, saved.y, saved.x + saved.width, saved.y + saved.height};
    // rcNormalPosition é em coordenadas de área de trabalho (deslocadas pela
    // barra de tarefas no topo/esquerda) e MonitorFromRect espera tela: a
    // checagem é aproximada, mas basta para pegar monitor desconectado,
    // que é o caso que importa.
    if (::MonitorFromRect(&rect, MONITOR_DEFAULTTONULL) == nullptr) return false;

    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    wp.rcNormalPosition = rect;
    wp.showCmd = startHidden ? SW_HIDE : (saved.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    return ::SetWindowPlacement(hwnd, &wp) != FALSE;
}

std::optional<WindowPlacement> CaptureWindowPlacement(HWND hwnd, bool pendingMaximize) {
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (!::GetWindowPlacement(hwnd, &wp)) return std::nullopt;

    WindowPlacement placement;
    placement.valid = true;
    placement.x = wp.rcNormalPosition.left;
    placement.y = wp.rcNormalPosition.top;
    placement.width = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
    placement.height = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
    // IsZoomed lê o estilo WS_MAXIMIZE, que continua valendo com a janela
    // escondida na bandeja; minimizada, vale o "restaurar para maximizada".
    placement.maximized = pendingMaximize || ::IsZoomed(hwnd) ||
                          (::IsIconic(hwnd) && (wp.flags & WPF_RESTORETOMAXIMIZED));
    return placement;
}

void SaveWindowPlacementToDisk(HWND hwnd) {
    auto placement = CaptureWindowPlacement(hwnd, g_uiState && g_uiState->showMaximizedOnOpen);
    if (!placement) return;
    // Só este campo vai para o disco (edições não salvas das Configurações
    // continuam não salvas), e nunca por cima de um config.json ilegível.
    if (auto onDisk = TryLoadConfig()) {
        onDisk->window = *placement;
        SaveConfig(*onDisk);
    }
}

} // namespace

int RunApp(bool startMinimized, bool dryRun, bool testBalloons) {
    UiState state;
    state.config = LoadConfig();
    state.dryRun = dryRun;
    state.testBalloons = testBalloons;
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

    if (!RestoreWindowPlacement(hwnd, state.config.window, startMinimized))
        ::ShowWindow(hwnd, startMinimized ? SW_HIDE : SW_SHOWDEFAULT);
    // Com --startup a janela nasce escondida; SetWindowPlacement com SW_HIDE
    // não guarda "maximizada", então o primeiro "Abrir" da bandeja aplica.
    state.showMaximizedOnOpen = startMinimized && state.config.window.valid &&
                                state.config.window.maximized;
    ::UpdateWindow(hwnd);
    state.windowVisible = !startMinimized;

    CreateTrayIcon(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // A janela ImGui é fixa (ocupa a área cliente inteira), então o
    // imgui.ini não teria nada útil — e o padrão o grava na pasta de
    // trabalho, ao lado do .exe. Posição/tamanho reais vão no config.json.
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ApplyTheme();
    // Fontes precisam existir no atlas antes do upload de textura em
    // ImGui_ImplDX11_Init logo abaixo.
    LoadFonts(io, state.fonts);

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

        ImGui::PushFont(state.fonts.hero);
        ImGui::TextUnformatted("Otimizador de Armazenamento");
        ImGui::PopFont();
        if (state.dryRun)
            ImGui::TextColored(theme::kAccentGold, "MODO SIMULAÇÃO - nada será excluído");
        {
            LightScanTotals headerTotals = state.GetLightTotals();
            std::uint64_t detectable = headerTotals.recycleBinBytes + headerTotals.tempBytes;
            ImGui::TextColored(theme::kTextMuted, "Espaço detectável agora: %s",
                               util::FormatSize(detectable).c_str());
        }
        ImGui::Separator();

        if (ImGui::BeginTabBar("MainTabs")) {
            if (ImGui::BeginTabItem("Painel")) {
                DrawDashboardPanel(state);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Resultados")) {
                DrawResultsPanel(state);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Configurações")) {
                DrawSettingsPanel(state);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Histórico")) {
                DrawHistoryPanel(state);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();

        ImGui::Render();
        const float clearColor[4] = {theme::kBg.x, theme::kBg.y, theme::kBg.z, theme::kBg.w};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_swapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    SaveWindowPlacementToDisk(hwnd);

    RemoveTrayIcon();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    g_uiState = nullptr;
    return 0;
}

} // namespace storagecleaner::ui
