#include "Panels.h"
#include "Theme.h"
#include "storagecleaner/Utils.h"

#include <imgui.h>

namespace storagecleaner::ui {

namespace {

void DrawCategoryTable(UiState& state, std::size_t bucketIdx) {
    const CategoryBucket& bucket = state.lastScanResult.buckets[bucketIdx];
    auto& selected = state.selection[bucketIdx];

    ImGui::PushID(static_cast<int>(bucketIdx));

    if (IsHeuristicCategory(bucket.category)) {
        ImGui::TextColored(theme::kWarning,
                           "Heurístico: pode incluir falsos positivos (ex.: apps portáteis ou "
                           "pouco usados). Revise cada item antes de excluir.");
    }

    bool allSelected = !selected.empty();
    for (bool s : selected) allSelected &= s;
    // "Selecionar todos" fica disponivel mesmo para a categoria heuristica,
    // mas ela nunca comeca marcada sozinha (ver UiState::Tick).
    if (ImGui::Checkbox("Selecionar todos", &allSelected))
        for (auto&& s : selected) s = allSelected;

    if (ImGui::BeginTable("items", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0, 220))) {
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn("Caminho");
        ImGui::TableSetupColumn("Tamanho");
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < bucket.items.size(); ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(static_cast<int>(i));
            bool isSelected = i < selected.size() && selected[i];
            if (ImGui::Checkbox("", &isSelected) && i < selected.size()) selected[i] = isSelected;
            ImGui::PopID();

            ImGui::TableSetColumnIndex(1);
            const ScanItem& item = bucket.items[i];
            ImGui::TextUnformatted(util::WideToUtf8(item.path).c_str());
            if (item.category == Category::Duplicate && !item.keepPath.empty())
                ImGui::TextDisabled("mantém: %s", util::WideToUtf8(item.keepPath).c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(util::FormatSize(item.sizeBytes).c_str());
        }
        ImGui::EndTable();
    }

    ImGui::PopID();
}

} // namespace

void DrawResultsPanel(UiState& state) {
    if (!state.hasScanResult) {
        ImGui::TextUnformatted("Nenhuma varredura ainda. Vá para a aba Painel e clique em Escanear.");
        return;
    }

    UiState::SelectionSummary selection = state.GetSelectionSummary();
    std::uint64_t selectedBytes = selection.bytes;

    for (Category skipped : state.lastScanResult.skippedNeedAdmin)
        ImGui::TextColored(theme::kWarning, "%s: não verificado - requer executar como Administrador.",
                           CategoryName(skipped));
    for (Category partial : state.lastScanResult.partialNeedAdmin)
        ImGui::TextColored(theme::kWarning,
                           "%s: só os itens do usuário - os do sistema exigem executar como Administrador.",
                           CategoryName(partial));

    for (std::size_t i = 0; i < state.lastScanResult.buckets.size(); ++i) {
        const CategoryBucket& bucket = state.lastScanResult.buckets[i];
        std::string header = std::string(CategoryName(bucket.category)) + " (" +
                             util::FormatSize(bucket.totalBytes) + ")";
        if (ImGui::CollapsingHeader(header.c_str())) DrawCategoryTable(state, i);
    }

    ImGui::Separator();
    ImGui::Text("Total selecionado: %s", util::FormatSize(selectedBytes).c_str());

    bool cleaning = state.cleaning.load();
    ImGui::BeginDisabled(cleaning || selectedBytes == 0);
    if (ImGui::Button("Limpar selecionados", ImVec2(200, 0))) state.showCleanConfirmModal = true;
    ImGui::EndDisabled();

    if (state.showCleanConfirmModal) ImGui::OpenPopup("Confirmar limpeza");
    if (ImGui::BeginPopupModal("Confirmar limpeza", &state.showCleanConfirmModal,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Isso vai remover %zu item(ns), liberando %s.", selection.count,
                   util::FormatSize(selection.bytes).c_str());
        ImGui::TextUnformatted("Os itens vão para a Lixeira e podem ser restaurados, exceto as "
                              "categorias marcadas como permanentes.");
        if (state.dryRun)
            ImGui::TextColored(theme::kAccentGold,
                              "Modo simulação: nada será excluído, só registrado no histórico.");
        if (selection.anyPermanent)
            ImGui::TextColored(theme::kDanger,
                              "Atenção: Lixeira e/ou pontos de restauração selecionados serão "
                              "removidos em definitivo, sem opção de desfazer.");

        // A lista completa de itens (com paths) só precisa existir de fato no
        // clique do botão — não a cada frame enquanto o modal fica aberto.
        if (ImGui::Button("Confirmar e limpar", ImVec2(180, 0))) {
            state.StartClean(state.CollectSelectedItems(), state.currentScanIsAutoClean);
            state.showCleanConfirmModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(120, 0))) {
            state.showCleanConfirmModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (cleaning) {
        ProgressSnapshot snapshot = state.cleanProgress.Read();
        DrawInlineCircularGauge(28.0f, 6.0f, snapshot.fractionComplete,
                               ImGui::ColorConvertFloat4ToU32(theme::kAccentBlue),
                               ImGui::ColorConvertFloat4ToU32(theme::kAccentGold),
                               ImGui::ColorConvertFloat4ToU32(theme::kBorder));
        ImGui::TextUnformatted(PhaseLabel(snapshot.phase));
    }

    {
        std::lock_guard<std::mutex> lock(state.cleanResultMutex);
        if (state.pendingCleanResult.has_value()) {
            const HistoryEntry& entry = *state.pendingCleanResult;
            if (entry.success && entry.wasDryRun)
                ImGui::TextColored(theme::kAccentGold, "Simulação concluída: %s seriam liberados.",
                                  util::FormatSize(entry.totalBytesFreed).c_str());
            else if (entry.success)
                ImGui::TextColored(theme::kSuccess, "Limpeza concluída: %s liberados.",
                                  util::FormatSize(entry.totalBytesFreed).c_str());
            else
                ImGui::TextColored(theme::kDanger, "Limpeza concluída com erros: %s",
                                  entry.errorSummary.c_str());
        }
    }
}

} // namespace storagecleaner::ui
