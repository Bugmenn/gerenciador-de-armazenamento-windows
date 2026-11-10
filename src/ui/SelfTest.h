#pragma once

#include "UiState.h"

#include <d3d11.h>

#include <cfloat>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace storagecleaner::ui {

// --ui-selftest <pasta>: roteiro automático da UI para validar telas sem
// alguém olhando. Carrega um resultado de varredura fictício (nada do disco
// real), percorre estados (abas, rosca, filtro, grupos, ordenação, seleção),
// salva um PNG de cada tela lido direto do back buffer do DirectX (a captura
// de tela externa não enxerga o conteúdo DX11) e um report.txt com as
// asserções. RunApp devolve o nº de falhas como exit code.
class SelfTest {
public:
    SelfTest(UiState& state, std::wstring outDir);

    // Preenche UiState com o resultado/unidades fictícios.
    void LoadFixture();
    // Entre o NewFrame do backend Win32 e ImGui::NewFrame: aplica a ação do
    // passo atual e injeta a posição do mouse.
    void BeforeNewFrame();
    // Depois de renderizar e antes do Present: confere e captura quando o
    // passo assentou. Devolve true quando o roteiro acabou (report gravado).
    bool AfterRender(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11DeviceContext* context);

    int Failures() const { return failures_; }

private:
    struct Step {
        const char* name;
        std::function<void()> action;
        std::function<void()> check;
        // Frames entre a ação e a captura: o layout do ImGui (abrir
        // categoria, rolar, tooltip) leva alguns frames para assentar.
        int frames = 4;
        bool measureFrameTime = false;
    };

    void BuildScript();
    void Check(bool ok, const std::string& description);
    void Info(const std::string& line);
    std::size_t BucketIndex(Category c) const;
    bool Capture(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11DeviceContext* context,
                 const std::wstring& path);
    void WriteReport();

    UiState& state_;
    std::wstring outDir_;
    std::vector<Step> steps_;
    std::size_t stepIndex_ = 0;
    int framesInStep_ = 0;
    bool actionDone_ = false;
    ImVec2 mousePos_{-FLT_MAX, -FLT_MAX};
    // Rolagem a aplicar na janela principal (< 0 = nenhuma).
    float pendingScrollMainY_ = -1.0f;
    int failures_ = 0;
    std::vector<std::string> report_;
    // Bytes esperados por unidade, somados enquanto o fixture é montado —
    // conferência independente de CleanableBytesByDrive.
    std::map<wchar_t, std::uint64_t> expectedByDrive_;
    std::chrono::steady_clock::time_point frameStart_{};
    double measuredMs_ = 0.0;
    int measuredFrames_ = 0;
};

} // namespace storagecleaner::ui
