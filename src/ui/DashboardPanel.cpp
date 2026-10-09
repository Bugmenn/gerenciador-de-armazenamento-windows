#include "Panels.h"
#include "Theme.h"
#include "storagecleaner/Utils.h"

#include <imgui.h>

#include <algorithm>

namespace storagecleaner::ui {

void DrawDashboardPanel(UiState& state) {
    ImGui::TextUnformatted("Categorias a incluir na próxima varredura:");
    for (Category c : kAllCategories) {
        bool enabled = state.config.perCategory[c].enabled;
        if (ImGui::Checkbox(CategoryName(c), &enabled)) state.config.perCategory[c].enabled = enabled;
        if (IsHeuristicCategory(c)) {
            ImGui::SameLine();
            ImGui::TextDisabled("(heurístico - revise antes de excluir)");
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
        DrawInlineCircularGauge(28.0f, 6.0f, snapshot.fractionComplete,
                               ImGui::ColorConvertFloat4ToU32(theme::kAccentBlue),
                               ImGui::ColorConvertFloat4ToU32(theme::kAccentGold),
                               ImGui::ColorConvertFloat4ToU32(theme::kBorder));
        ImGui::TextUnformatted(PhaseLabel(snapshot.phase));
        ImGui::TextWrapped("%s", util::WideToUtf8(snapshot.currentItem).c_str());
        ImGui::Text("Itens processados: %llu", static_cast<unsigned long long>(snapshot.itemsProcessed));
        ImGui::Text("Encontrado até agora: %s", util::FormatSize(snapshot.bytesFoundSoFar).c_str());
        if (!snapshot.lastError.empty()) ImGui::TextColored(theme::kWarning, "%s", snapshot.lastError.c_str());
    } else if (state.hasScanResult) {
        ImGui::Text("Última varredura: %s no total", util::FormatSize(state.lastScanResult.GrandTotalBytes()).c_str());
        ImGui::TextUnformatted("Veja a aba Resultados para revisar e limpar.");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Varredura leve em segundo plano (atualizada automaticamente):");
    LightScanTotals lightTotals = state.GetLightTotals();
    ImGui::Text("Lixeira: %s", util::FormatSize(lightTotals.recycleBinBytes).c_str());
    ImGui::Text("Temporários (usuário atual): %s", util::FormatSize(lightTotals.tempBytes).c_str());

    if (!lightTotals.drives.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Unidades:");
        const float gaugeRadius = 22.0f;
        for (std::size_t i = 0; i < lightTotals.drives.size(); ++i) {
            const util::DriveSpace& drive = lightTotals.drives[i];
            std::uint64_t used = drive.totalBytes - std::min(drive.freeBytes, drive.totalBytes);
            float usedFraction = static_cast<float>(static_cast<double>(used) /
                                                    static_cast<double>(drive.totalBytes));
            bool low = state.config.lowDiskAlertEnabled && !drive.isRemovable &&
                       (1.0f - usedFraction) * 100.0f < static_cast<float>(state.config.lowDiskAlertPercent);
            ImU32 fill = ImGui::ColorConvertFloat4ToU32(low ? theme::kDanger : theme::kAccentBlue);
            ImU32 fillEnd = ImGui::ColorConvertFloat4ToU32(low ? theme::kDanger : theme::kAccentGold);

            ImGui::PushID(static_cast<int>(i));
            ImGui::BeginGroup();
            DrawInlineCircularGauge(gaugeRadius, 5.0f, usedFraction, fill, fillEnd,
                                    ImGui::ColorConvertFloat4ToU32(theme::kBorder));
            ImGui::EndGroup();
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Text("%s%s", util::WideToUtf8(drive.root).c_str(),
                        drive.isRemovable ? " (removível)" : "");
            ImGui::TextColored(low ? theme::kDanger : theme::kTextMuted, "%s livres de %s",
                               util::FormatSize(drive.freeBytes).c_str(),
                               util::FormatSize(drive.totalBytes).c_str());
            ImGui::EndGroup();
            ImGui::PopID();
        }
    }
}

} // namespace storagecleaner::ui
