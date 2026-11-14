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
            ImGui::TextDisabled("(heurístico - separado pela chance de o app ainda existir)");
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
        DrawProgressRing(snapshot);
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
        const float gaugeRadius = 24.0f;
        for (std::size_t i = 0; i < lightTotals.drives.size(); ++i) {
            const util::DriveSpace& drive = lightTotals.drives[i];
            std::uint64_t used = drive.totalBytes - std::min(drive.freeBytes, drive.totalBytes);
            float usedFraction = static_cast<float>(static_cast<double>(used) /
                                                    static_cast<double>(drive.totalBytes));
            bool low = state.config.lowDiskAlertEnabled && !drive.isRemovable &&
                       (1.0f - usedFraction) * 100.0f < static_cast<float>(state.config.lowDiskAlertPercent);
            // Parte do "usado" que a última varredura achou nesta unidade.
            std::uint64_t cleanable = 0;
            if (state.hasScanResult && !drive.root.empty()) {
                wchar_t letter = drive.root[0];
                if (letter >= L'a' && letter <= L'z') letter = static_cast<wchar_t>(letter - L'a' + L'A');
                auto it = state.cleanableByDrive.find(letter);
                if (it != state.cleanableByDrive.end()) cleanable = std::min(it->second, used);
            }
            DonutSlice slices[] = {
                {used - cleanable, ImGui::ColorConvertFloat4ToU32(low ? theme::kDanger : theme::kAccentBlue)},
                {cleanable, ImGui::ColorConvertFloat4ToU32(theme::kAccentGold)},
                {std::min(drive.freeBytes, drive.totalBytes), ImGui::ColorConvertFloat4ToU32(theme::kBorder)},
            };

            ImGui::PushID(static_cast<int>(i));
            ImGui::BeginGroup();
            int hovered = DrawInlineSegmentedDonut(gaugeRadius, 6.0f, slices, 3,
                                                   ImGui::ColorConvertFloat4ToU32(theme::kBorder));
            ImGui::EndGroup();
            bool gaugeHovered = ImGui::IsItemHovered();
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Text("%s%s", util::WideToUtf8(drive.root).c_str(),
                        drive.isRemovable ? " (removível)" : "");
            ImGui::TextColored(low ? theme::kDanger : theme::kTextMuted, "%s livres de %s",
                               util::FormatSize(drive.freeBytes).c_str(),
                               util::FormatSize(drive.totalBytes).c_str());
            if (cleanable > 0)
                ImGui::TextColored(theme::kAccentGold, "%s podem ser liberados",
                                   util::FormatSize(cleanable).c_str());
            ImGui::EndGroup();
            if (hovered >= 0 || gaugeHovered || ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextColored(low ? theme::kDanger : theme::kAccentBlue, "Em uso: %s",
                                   util::FormatSize(used - cleanable).c_str());
                if (state.hasScanResult)
                    ImGui::TextColored(theme::kAccentGold, "Encontrado para limpar: %s",
                                       util::FormatSize(cleanable).c_str());
                ImGui::TextColored(theme::kTextMuted, "Livre: %s", util::FormatSize(drive.freeBytes).c_str());
                if (state.hasScanResult)
                    ImGui::TextDisabled("Lixeira e pontos de restauração não entram na conta por unidade.");
                else
                    ImGui::TextDisabled("Escaneie para ver quanto dá para liberar nesta unidade.");
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
    }
}

} // namespace storagecleaner::ui
