#include "Panels.h"
#include "storagecleaner/Utils.h"

#include <imgui.h>

namespace storagecleaner::ui {

void DrawDashboardPanel(UiState& state) {
    ImGui::TextUnformatted("Categorias a incluir na proxima varredura:");
    for (Category c : kAllCategories) {
        bool enabled = state.config.perCategory[c].enabled;
        if (ImGui::Checkbox(CategoryName(c), &enabled)) state.config.perCategory[c].enabled = enabled;
        if (IsHeuristicCategory(c)) {
            ImGui::SameLine();
            ImGui::TextDisabled("(heuristico - revise antes de excluir)");
        }
    }

    ImGui::Separator();

    bool scanning = state.scanEngine.IsRunning();
    ImGui::BeginDisabled(scanning);
    if (ImGui::Button("Escanear", ImVec2(160, 0))) state.StartScan();
    ImGui::EndDisabled();

    if (scanning) {
        ImGui::SameLine();
        if (ImGui::Button("Cancelar")) state.scanEngine.RequestCancel();

        ProgressSnapshot snapshot = state.scanProgress.Read();
        ImGui::ProgressBar(snapshot.fractionComplete);
        ImGui::TextUnformatted(PhaseLabel(snapshot.phase));
        ImGui::TextWrapped("%s", util::WideToUtf8(snapshot.currentItem).c_str());
        ImGui::Text("Itens processados: %llu", static_cast<unsigned long long>(snapshot.itemsProcessed));
        ImGui::Text("Encontrado ate agora: %s", util::FormatSize(snapshot.bytesFoundSoFar).c_str());
        if (!snapshot.lastError.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", snapshot.lastError.c_str());
    } else if (state.hasScanResult) {
        ImGui::Text("Ultima varredura: %s no total", util::FormatSize(state.lastScanResult.GrandTotalBytes()).c_str());
        ImGui::TextUnformatted("Veja a aba Resultados para revisar e limpar.");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Varredura leve em segundo plano (atualizada automaticamente):");
    LightScanTotals lightTotals = state.GetLightTotals();
    ImGui::Text("Lixeira: %s", util::FormatSize(lightTotals.recycleBinBytes).c_str());
    ImGui::Text("Temporarios (usuario atual): %s", util::FormatSize(lightTotals.tempBytes).c_str());
}

} // namespace storagecleaner::ui
