#include "Panels.h"
#include "Theme.h"
#include "storagecleaner/HistoryStore.h"
#include "storagecleaner/Utils.h"

#include <imgui.h>

#include <ctime>

namespace storagecleaner::ui {

namespace {

std::string FormatTimestamp(std::chrono::system_clock::time_point tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tmBuf{};
    localtime_s(&tmBuf, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M", &tmBuf);
    return buf;
}

} // namespace

void DrawHistoryPanel(UiState& state) {
    if (!state.historyLoaded) {
        state.history = LoadHistory();
        state.historyLoaded = true;
    }

    if (ImGui::Button("Atualizar")) state.historyLoaded = false;

    if (state.history.empty()) {
        ImGui::TextUnformatted("Nenhuma limpeza registrada ainda.");
        return;
    }

    if (ImGui::BeginTable("history", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Data");
        ImGui::TableSetupColumn("Liberado");
        ImGui::TableSetupColumn("Origem");
        ImGui::TableSetupColumn("Status");
        ImGui::TableHeadersRow();

        // Mais recentes primeiro.
        for (auto it = state.history.rbegin(); it != state.history.rend(); ++it) {
            const HistoryEntry& entry = *it;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(FormatTimestamp(entry.timestamp).c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(util::FormatSize(entry.totalBytesFreed).c_str());
            if (ImGui::IsItemHovered() && !entry.bytesFreedByCategory.empty()) {
                ImGui::BeginTooltip();
                for (const auto& [category, bytes] : entry.bytesFreedByCategory)
                    ImGui::Text("%s: %s", CategoryName(category), util::FormatSize(bytes).c_str());
                ImGui::EndTooltip();
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(entry.wasAutomatic ? "Automatica" : "Manual");

            ImGui::TableSetColumnIndex(3);
            if (entry.success && entry.itemsSkipped == 0) {
                ImGui::TextColored(theme::kSuccess, "OK");
            } else if (entry.success) {
                // Concluida sem erro, mas alguns itens sumiram/mudaram entre
                // o scan e a limpeza (ver Cleaner::SendToRecycleBin) — nao e'
                // uma falha, mas o usuario deve saber que nem tudo que foi
                // selecionado acabou sendo removido.
                ImGui::TextColored(theme::kWarning, "OK (%zu item(s) ignorado(s))",
                                   entry.itemsSkipped);
            } else {
                ImGui::TextColored(theme::kDanger, "%s", entry.errorSummary.c_str());
            }
        }
        ImGui::EndTable();
    }
}

} // namespace storagecleaner::ui
