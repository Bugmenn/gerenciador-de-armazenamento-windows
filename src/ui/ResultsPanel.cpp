#include "Panels.h"
#include "storagecleaner/Utils.h"

#include <imgui.h>

namespace storagecleaner::ui {

namespace {

void DrawCategoryTable(UiState& state, std::size_t bucketIdx) {
    const CategoryBucket& bucket = state.lastScanResult.buckets[bucketIdx];
    auto& selected = state.selection[bucketIdx];

    ImGui::PushID(static_cast<int>(bucketIdx));

    if (IsHeuristicCategory(bucket.category)) {
        ImGui::TextColored(ImVec4(1, 0.75f, 0.2f, 1),
                           "Heuristico: pode incluir falsos positivos (ex: apps portateis ou "
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
                ImGui::TextDisabled("mantem: %s", util::WideToUtf8(item.keepPath).c_str());

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
        ImGui::TextUnformatted("Nenhuma varredura ainda. Va para a aba Dashboard e clique em Escanear.");
        return;
    }

    std::uint64_t selectedBytes = 0;
    for (const auto& item : state.CollectSelectedItems()) selectedBytes += item.sizeBytes;

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
        std::vector<ScanItem> selectedItems = state.CollectSelectedItems();
        bool anyPermanent = false;
        for (const auto& item : selectedItems)
            if (IsPermanentCategory(item.category)) anyPermanent = true;

        ImGui::Text("Isso vai remover %zu item(ns), liberando %s.", selectedItems.size(),
                   util::FormatSize(selectedBytes).c_str());
        ImGui::TextUnformatted("Arquivos temporarios, cache, duplicados, logs e dados orfaos vao"
                              " para a Lixeira (podem ser restaurados).");
        if (anyPermanent)
            ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1),
                              "Atencao: Lixeira e/ou pontos de restauracao selecionados serao "
                              "removidos em definitivo, sem opcao de desfazer.");

        if (ImGui::Button("Confirmar e limpar", ImVec2(180, 0))) {
            state.StartClean(selectedItems, state.currentScanIsAutoClean);
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
        ImGui::ProgressBar(snapshot.fractionComplete);
        ImGui::TextUnformatted(PhaseLabel(snapshot.phase));
    }

    {
        std::lock_guard<std::mutex> lock(state.cleanResultMutex);
        if (state.pendingCleanResult.has_value()) {
            const HistoryEntry& entry = *state.pendingCleanResult;
            if (entry.success)
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1), "Limpeza concluida: %s liberados.",
                                  util::FormatSize(entry.totalBytesFreed).c_str());
            else
                ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "Limpeza concluida com erros: %s",
                                  entry.errorSummary.c_str());
        }
    }
}

} // namespace storagecleaner::ui
