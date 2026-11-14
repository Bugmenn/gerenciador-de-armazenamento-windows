#include "Panels.h"
#include "Theme.h"
#include "storagecleaner/Utils.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace storagecleaner::ui {

namespace {

ImU32 U32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

// Cor e texto do grupo de nível (dados órfãos): Baixa = seguro, Alta = perigo.
ImVec4 ChanceColor(ExistenceChance chance) {
    switch (chance) {
        case ExistenceChance::Low:    return theme::kSuccess;
        case ExistenceChance::Medium: return theme::kWarning;
        case ExistenceChance::High:   return theme::kDanger;
        case ExistenceChance::NotApplicable: break;
    }
    return theme::kTextPrimary;
}

const char* ChanceGroupLabel(ExistenceChance chance) {
    switch (chance) {
        case ExistenceChance::Low:    return "Baixa chance de o app ainda existir";
        case ExistenceChance::Medium: return "Média chance de o app ainda existir - revise";
        case ExistenceChance::High:   return "Alta chance de o app ainda existir - não recomendado limpar";
        case ExistenceChance::NotApplicable: break;
    }
    return "";
}

std::string Percent(std::uint64_t part, std::uint64_t total) {
    char buf[16];
    double pct = total ? 100.0 * static_cast<double>(part) / static_cast<double>(total) : 0.0;
    std::snprintf(buf, sizeof(buf), pct > 0.0 && pct < 0.1 ? "<0.1%%" : "%.1f%%", pct);
    return buf;
}

std::string ItemCountText(std::size_t n) {
    return n == 1 ? std::string("1 item") : std::to_string(n) + " itens";
}

// Corta o começo do texto ("...\pasta\sub") até caber em `maxWidth`: no fim do
// caminho fica a parte que distingue uma pasta da outra.
std::string EllipsizeLeft(const std::string& text, float maxWidth) {
    if (maxWidth <= 0.0f || ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;
    static const char kPrefix[] = "...";
    float prefixWidth = ImGui::CalcTextSize(kPrefix).x;
    std::size_t start = 0;
    while (start < text.size()) {
        ++start;
        // Não parte um caractere UTF-8 ao meio (bytes de continuação 10xxxxxx).
        while (start < text.size() && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) ++start;
        if (prefixWidth + ImGui::CalcTextSize(text.c_str() + start).x <= maxWidth) break;
    }
    return kPrefix + text.substr(start);
}

// Corta o fim ("texto...") — para frases em que o começo é o que importa
// (motivos do mais forte ao mais fraco, rótulo do nível).
std::string EllipsizeRight(const std::string& text, float maxWidth) {
    if (maxWidth <= 0.0f || ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;
    static const char kSuffix[] = "...";
    float suffixWidth = ImGui::CalcTextSize(kSuffix).x;
    std::size_t end = text.size();
    while (end > 0) {
        --end;
        // Não parte um caractere UTF-8 ao meio (bytes de continuação 10xxxxxx).
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
        if (ImGui::CalcTextSize(text.c_str(), text.c_str() + end).x + suffixWidth <= maxWidth) break;
    }
    return text.substr(0, end) + kSuffix;
}

// Checkbox de três estados: o "misto" (alguns marcados) usa o traço do ImGui.
// Devolve true no clique, com o novo valor em `out` (misto -> marca todos).
bool TriCheckbox(const char* label, TriState state, bool& out) {
    bool value = state == TriState::All;
    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, state == TriState::Some);
    bool clicked = ImGui::Checkbox(label, &value);
    ImGui::PopItemFlag();
    if (clicked) out = value;
    return clicked;
}

void ColorSwatch(const ImVec4& color) {
    float lineH = ImGui::GetTextLineHeight();
    float size = lineH * 0.7f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    float y = p.y + (ImGui::GetFrameHeight() - size) * 0.5f;
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + size, y + size), U32(color), 3.0f);
    ImGui::Dummy(ImVec2(size, ImGui::GetFrameHeight()));
}

void CategoryTooltip(const CategoryBucket& bucket, std::uint64_t total) {
    ImGui::BeginTooltip();
    ImGui::TextColored(theme::CategoryColor(bucket.category), "%s", CategoryName(bucket.category));
    ImGui::Text("%s (%s do total)", util::FormatSize(bucket.totalBytes).c_str(),
                Percent(bucket.totalBytes, total).c_str());
    ImGui::TextDisabled("%s - clique para ver", ItemCountText(bucket.items.size()).c_str());
    ImGui::EndTooltip();
}

// Rosca com uma fatia por categoria + legenda ao lado (maior primeiro).
void DrawCategoryDonut(UiState& state) {
    const auto& buckets = state.lastScanResult.buckets;
    std::uint64_t total = state.lastScanResult.GrandTotalBytes();

    std::vector<DonutSlice> slices(buckets.size());
    for (std::size_t i = 0; i < buckets.size(); ++i)
        slices[i] = DonutSlice{buckets[i].totalBytes, U32(theme::CategoryColor(buckets[i].category))};

    const float radius = 72.0f;
    ImVec2 center;
    int hovered = DrawInlineSegmentedDonut(radius, 20.0f, slices.data(), static_cast<int>(slices.size()),
                                           U32(theme::kBorder), &center);
    if (state.selfTest) state.selfTestProbe = SelfTestProbe{center, radius, hovered};

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    std::string totalText = util::FormatSize(total);
    ImGui::PushFont(state.fonts.hero);
    ImVec2 totalSize = ImGui::CalcTextSize(totalText.c_str());
    drawList->AddText(ImVec2(center.x - totalSize.x * 0.5f, center.y - totalSize.y * 0.65f),
                      U32(theme::kTextPrimary), totalText.c_str());
    ImGui::PopFont();
    const char* subtitle = "encontrados";
    ImVec2 subSize = ImGui::CalcTextSize(subtitle);
    drawList->AddText(ImVec2(center.x - subSize.x * 0.5f, center.y + totalSize.y * 0.35f),
                      U32(theme::kTextMuted), subtitle);

    if (hovered >= 0) {
        CategoryTooltip(buckets[hovered], total);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) state.RequestOpenCategory(hovered);
    }

    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < buckets.size(); ++i)
        if (buckets[i].totalBytes > 0) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return buckets[a].totalBytes > buckets[b].totalBytes;
    });

    ImGui::SameLine(0.0f, 28.0f);
    ImGui::BeginGroup();
    if (order.empty()) {
        ImGui::Dummy(ImVec2(0.0f, radius - ImGui::GetTextLineHeight()));
        ImGui::TextDisabled("Nada encontrado nas categorias varridas.");
    }
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    // NoHostExtendX: a tabela tem a largura do conteúdo, senão o destaque da
    // linha vai até a borda da janela.
    if (!order.empty() &&
        ImGui::BeginTable("legenda", 3,
                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX |
                              ImGuiTableFlags_NoSavedSettings)) {
        for (std::size_t idx : order) {
            const CategoryBucket& bucket = buckets[idx];
            ImGui::PushID(static_cast<int>(idx));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ColorSwatch(theme::CategoryColor(bucket.category));
            ImGui::SameLine();
            bool clicked = ImGui::Selectable(CategoryName(bucket.category), hovered == static_cast<int>(idx),
                                             ImGuiSelectableFlags_SpanAllColumns,
                                             ImVec2(0.0f, ImGui::GetFrameHeight()));
            if (ImGui::IsItemHovered()) CategoryTooltip(bucket, total);
            if (clicked) state.RequestOpenCategory(idx);
            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(util::FormatSize(bucket.totalBytes).c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", Percent(bucket.totalBytes, total).c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);
    ImGui::EndGroup();
}

void DrawFilterBar(UiState& state) {
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputTextWithHint("##filtro", "Filtrar por nome ou pasta...", state.resultsFilter,
                             sizeof(state.resultsFilter));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    int& minIdx = state.resultsMinSizeIdx;
    minIdx = std::clamp(minIdx, 0, static_cast<int>(std::size(kMinSizeOptions)) - 1);
    if (ImGui::BeginCombo("##tamanho", kMinSizeOptions[minIdx].label)) {
        for (int i = 0; i < static_cast<int>(std::size(kMinSizeOptions)); ++i)
            if (ImGui::Selectable(kMinSizeOptions[i].label, i == minIdx)) minIdx = i;
        ImGui::EndCombo();
    }
    if (state.IsFilterActive()) {
        ImGui::SameLine();
        if (ImGui::Button("Limpar filtro")) {
            state.resultsFilter[0] = '\0';
            minIdx = 0;
        }
    }
}

void DrawGroupRow(UiState& state, std::size_t bucketIdx, BucketView& view, std::size_t groupIdx) {
    const ItemGroup& group = view.groups[groupIdx];

    ImGui::TableSetColumnIndex(0);
    bool value = false;
    if (TriCheckbox("##grp", state.GroupSelectionState(bucketIdx, groupIdx), value))
        state.SetGroupSelected(bucketIdx, groupIdx, value);

    ImGui::TableSetColumnIndex(1);
    bool open = state.IsGroupOpen(view, groupIdx);
    bool byChance = group.chance != ExistenceChance::NotApplicable;
    std::string folder = byChance ? std::string(ChanceGroupLabel(group.chance))
                         : group.folder.empty() ? std::string("(sem pasta)")
                                                : util::WideToUtf8(group.folder);
    float avail = ImGui::GetContentRegionAvail().x - ImGui::GetTreeNodeToLabelSpacing();
    std::string shown = byChance ? EllipsizeRight(folder, avail) : EllipsizeLeft(folder, avail);
    ImGui::SetNextItemOpen(open, ImGuiCond_Always);
    if (byChance) ImGui::PushStyleColor(ImGuiCol_Text, ChanceColor(group.chance));
    bool nowOpen = ImGui::TreeNodeEx("##node", ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_FramePadding |
                                                   ImGuiTreeNodeFlags_SpanAvailWidth,
                                     "%s", shown.c_str());
    if (byChance) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && shown != folder) ImGui::SetTooltip("%s", folder.c_str());
    if (nowOpen != open) state.ToggleGroup(bucketIdx, groupIdx);

    ImGui::TableSetColumnIndex(2);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(ItemCountText(group.itemIndices.size()).c_str());

    ImGui::TableSetColumnIndex(3);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(util::FormatSize(group.totalBytes).c_str());
}

void DrawItemRow(UiState& state, std::size_t bucketIdx, const BucketView& view, const ResultRow& row) {
    const ScanItem& item = state.lastScanResult.buckets[bucketIdx].items[row.item];
    auto& sel = state.selection[bucketIdx];
    // Grupo de nível sempre tem cabeçalho (ver GetBucketView), mesmo com 1 item.
    bool inGroup = view.groups[row.group].itemIndices.size() > 1 ||
                   view.groups[row.group].chance != ExistenceChance::NotApplicable;

    ImGui::TableSetColumnIndex(0);
    bool isSelected = row.item < sel.size() && sel[row.item];
    if (ImGui::Checkbox("##row", &isSelected) && row.item < sel.size()) sel[row.item] = isSelected;

    ImGui::TableSetColumnIndex(1);
    if (inGroup) ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
    ImGui::AlignTextToFramePadding();
    std::string name = util::WideToUtf8(std::wstring(FileNameOf(item.path)));
    ImGui::TextUnformatted(name.c_str());
    bool hovered = ImGui::IsItemHovered();
    // Fora de grupo (pasta com um item só), a pasta aparece apagada ao lado.
    std::string detail;
    if (item.category == Category::Duplicate && !item.keepPath.empty())
        detail = "mantém: " + util::WideToUtf8(item.keepPath);
    else if (!item.evidence.empty())
        // Pasta-pai curta (Roaming/Local/LocalLow) antes dos motivos: "Foo"
        // de Roaming e de Local apareceriam iguais sem ela.
        detail = util::WideToUtf8(std::wstring(FileNameOf(ParentFolder(item.path)))) + " - " + item.evidence;
    else if (!inGroup && !view.groups[row.group].folder.empty())
        detail = util::WideToUtf8(view.groups[row.group].folder);
    if (!detail.empty()) {
        ImGui::SameLine();
        float avail = ImGui::GetContentRegionAvail().x;
        bool isPath = item.evidence.empty();
        ImGui::TextDisabled("%s", (isPath ? EllipsizeLeft(detail, avail) : EllipsizeRight(detail, avail)).c_str());
        hovered |= ImGui::IsItemHovered();
    }
    if (inGroup) ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
    if (hovered) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(util::WideToUtf8(item.path).c_str());
        if (item.isDirectory) ImGui::TextDisabled("Pasta inteira");
        if (item.category == Category::Duplicate && !item.keepPath.empty())
            ImGui::TextDisabled("Original mantido: %s", util::WideToUtf8(item.keepPath).c_str());
        if (item.existenceChance != ExistenceChance::NotApplicable) {
            ImGui::TextColored(ChanceColor(item.existenceChance), "Chance de o app ainda existir: %s",
                               ExistenceChanceLabel(item.existenceChance));
            if (!item.evidence.empty()) ImGui::TextDisabled("Sinais: %s", item.evidence.c_str());
        }
        ImGui::EndTooltip();
    }

    ImGui::TableSetColumnIndex(2);
    if (item.isDirectory) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("pasta");
    }

    ImGui::TableSetColumnIndex(3);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(util::FormatSize(item.sizeBytes).c_str());
}

void DrawCategoryTable(UiState& state, std::size_t bucketIdx) {
    const CategoryBucket& bucket = state.lastScanResult.buckets[bucketIdx];

    if (IsHeuristicCategory(bucket.category)) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kWarning);
        ImGui::TextWrapped("Heurístico: pastas de apps que parecem desinstalados, separadas pela chance "
                           "de o app ainda existir (processo rodando, atalho, registro, programas na "
                           "pasta, tempo sem uso). A limpeza automática só leva as de chance Baixa.");
        ImGui::PopStyleColor();
    }

    BucketView& view = state.GetBucketView(bucketIdx);
    if (view.visibleItems == 0) {
        ImGui::TextDisabled(state.IsFilterActive() ? "Nenhum item corresponde ao filtro."
                                                   : "Nenhum item.");
        return;
    }

    // Com filtro, "Selecionar todos" age só sobre o que está à vista.
    char selectLabel[64];
    if (state.IsFilterActive())
        std::snprintf(selectLabel, sizeof(selectLabel), "Selecionar visíveis (%zu)###all", view.visibleItems);
    else
        std::snprintf(selectLabel, sizeof(selectLabel), "Selecionar todos###all");
    bool selectAll = false;
    if (TriCheckbox(selectLabel, state.VisibleSelectionState(bucketIdx), selectAll))
        state.SetVisibleSelected(bucketIdx, selectAll);

    const ImGuiStyle& style = ImGui::GetStyle();
    float rowH = ImGui::GetFrameHeight() + style.CellPadding.y * 2.0f;
    float headerH = ImGui::GetTextLineHeight() + style.CellPadding.y * 2.0f;
    float tableH = std::min(headerH + rowH * static_cast<float>(view.rows.size()) + 2.0f, 420.0f);

    if (ImGui::BeginTable("items", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_Sortable,
                          ImVec2(0, tableH))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        // TableHeadersRow() usa PushID(coluna)+rótulo (ImGui 1.91, imgui_tables.cpp)
        // e os widgets de linha usam PushID(linha)+rótulo: rótulos distintos
        // ("##sel" x "##row"/"##grp") evitam que o cabeçalho roube o clique.
        ImGui::TableSetupColumn("##sel", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                ImGui::GetFrameHeight());
        ImGui::TableSetupColumn("Nome", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Itens", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                ImGui::GetFontSize() * 4.5f);
        ImGui::TableSetupColumn("Tamanho",
                                ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort |
                                    ImGuiTableColumnFlags_PreferSortDescending,
                                ImGui::GetFontSize() * 6.0f);
        constexpr int kNameColumn = 1, kSizeColumn = 3;
        if (view.sortChangedExternally) {
            ImGui::TableSetColumnSortDirection(view.sortKey == ItemSortKey::Name ? kNameColumn : kSizeColumn,
                                               view.sortDescending ? ImGuiSortDirection_Descending
                                                                   : ImGuiSortDirection_Ascending,
                                               false);
            view.sortChangedExternally = false;
        }
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
            if (specs->SpecsCount > 0) {
                const ImGuiTableColumnSortSpecs& spec = specs->Specs[0];
                state.SetResultsSort(bucketIdx,
                                     spec.ColumnIndex == kNameColumn ? ItemSortKey::Name : ItemSortKey::Size,
                                     spec.SortDirection == ImGuiSortDirection_Descending, true);
            }
            specs->SpecsDirty = false;
            // Reconstrói já, antes das linhas: senão a reconstrução preguiçosa
            // aconteceria no meio do laço abaixo, com referências a grupos
            // antigos em uso.
            state.GetBucketView(bucketIdx);
        }

        // Só as linhas à vista são desenhadas: uma categoria pode ter dezenas
        // de milhares de itens.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(view.rows.size()), rowH);
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                // Cópia: ToggleGroup reconstrói view.rows no meio do laço.
                ResultRow row = view.rows[r];
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH - style.CellPadding.y * 2.0f);
                ImGui::PushID(r);
                if (row.item == ResultRow::kGroupRow)
                    DrawGroupRow(state, bucketIdx, view, row.group);
                else
                    DrawItemRow(state, bucketIdx, view, row);
                ImGui::PopID();
                if (view.rowsDirty) break;
            }
            if (view.rowsDirty) break;
        }
        clipper.End();
        ImGui::EndTable();
    }
}

void DrawCategoryHeaders(UiState& state) {
    const auto& buckets = state.lastScanResult.buckets;
    for (std::size_t i = 0; i < buckets.size(); ++i) {
        const CategoryBucket& bucket = buckets[i];
        bool openRequested = state.pendingOpenBucket == i;
        if (openRequested) ImGui::SetNextItemOpen(true, ImGuiCond_Always);

        ImGui::PushID(static_cast<int>(i));
        // "###cat": o ID não pode depender do tamanho exibido, senão a
        // categoria fecha sozinha quando o total muda (ex.: pasta protegida).
        std::string header = "     " + std::string(CategoryName(bucket.category)) + " - " +
                             util::FormatSize(bucket.totalBytes) + " (" +
                             ItemCountText(bucket.items.size()) + ")###cat";
        bool open = ImGui::CollapsingHeader(header.c_str());
        // Quadradinho com a cor da fatia, no espaço reservado antes do nome.
        ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        float size = ImGui::GetTextLineHeight() * 0.7f;
        float x = min.x + ImGui::GetStyle().FramePadding.x + ImGui::GetTreeNodeToLabelSpacing();
        float y = (min.y + max.y - size) * 0.5f;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, y), ImVec2(x + size, y + size),
                                                  U32(theme::CategoryColor(bucket.category)), 3.0f);
        if (openRequested) {
            ImGui::SetScrollHereY(0.0f);
            state.pendingOpenBucket.reset();
        }
        if (open) DrawCategoryTable(state, i);
        ImGui::PopID();
    }
}

} // namespace

void DrawResultsPanel(UiState& state) {
    if (!state.hasScanResult) {
        ImGui::TextUnformatted("Nenhuma varredura ainda. Vá para a aba Painel e clique em Escanear.");
        return;
    }

    UiState::SelectionSummary selection = state.GetSelectionSummary();
    std::uint64_t selectedBytes = selection.bytes;
    bool cleaning = state.cleaning.load();
    bool hasCleanResult = false;
    {
        std::lock_guard<std::mutex> lock(state.cleanResultMutex);
        hasCleanResult = state.pendingCleanResult.has_value();
    }

    // Rodapé fixo (total + botão + progresso/resultado da limpeza): a lista
    // rola por cima dele em vez de empurrar o botão para fora da tela.
    const ImGuiStyle& style = ImGui::GetStyle();
    float footerH = style.ItemSpacing.y * 2.0f + ImGui::GetTextLineHeightWithSpacing() +
                    ImGui::GetFrameHeightWithSpacing();
    if (cleaning) footerH += 56.0f + style.FramePadding.y * 2.0f + style.ItemSpacing.y + ImGui::GetTextLineHeightWithSpacing();
    if (hasCleanResult) footerH += ImGui::GetTextLineHeightWithSpacing();

    // Com filtro, a seleção continua valendo para o que não está à vista:
    // avisa, senão "Limpar selecionados" leva itens que o usuário nem vê.
    std::size_t hiddenSelected = 0;
    if (state.IsFilterActive()) {
        std::size_t visibleSelected = 0;
        for (std::size_t b = 0; b < state.lastScanResult.buckets.size() && b < state.selection.size(); ++b) {
            const auto& sel = state.selection[b];
            for (const auto& group : state.GetBucketView(b).groups)
                for (std::size_t i : group.itemIndices)
                    if (i < sel.size() && sel[i]) ++visibleSelected;
        }
        hiddenSelected = selection.count - std::min(visibleSelected, selection.count);
    }
    if (hiddenSelected > 0) footerH += ImGui::GetTextLineHeightWithSpacing();

    if (ImGui::BeginChild("resultados", ImVec2(0, -footerH), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground)) {
        for (Category skipped : state.lastScanResult.skippedNeedAdmin)
            ImGui::TextColored(theme::kWarning, "%s: não verificado - requer executar como Administrador.",
                               CategoryName(skipped));
        for (Category partial : state.lastScanResult.partialNeedAdmin)
            ImGui::TextColored(theme::kWarning,
                               "%s: só os itens do usuário - os do sistema exigem executar como Administrador.",
                               CategoryName(partial));

        DrawCategoryDonut(state);
        ImGui::Separator();
        DrawFilterBar(state);
        DrawCategoryHeaders(state);
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Text("Total selecionado: %s (%s)", util::FormatSize(selectedBytes).c_str(),
                ItemCountText(selection.count).c_str());
    if (hiddenSelected > 0)
        ImGui::TextColored(theme::kWarning, "Inclui %s fora do filtro atual - eles também serão limpos.",
                           ItemCountText(hiddenSelected).c_str());

    ImGui::BeginDisabled(cleaning || selectedBytes == 0);
    if (ImGui::Button("Limpar selecionados", ImVec2(200, 0))) state.showCleanConfirmModal = true;
    ImGui::EndDisabled();

    if (state.showCleanConfirmModal) ImGui::OpenPopup("Confirmar limpeza");
    if (ImGui::BeginPopupModal("Confirmar limpeza", &state.showCleanConfirmModal,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Isso vai remover %zu item(ns), liberando %s.", selection.count,
                   util::FormatSize(selection.bytes).c_str());
        if (hiddenSelected > 0)
            ImGui::TextColored(theme::kWarning, "%s não aparece(m) com o filtro atual.",
                               ItemCountText(hiddenSelected).c_str());
        ImGui::TextUnformatted("Os itens vão para a Lixeira e podem ser restaurados, exceto as "
                              "categorias marcadas como permanentes.");
        if (state.dryRun)
            ImGui::TextColored(theme::kAccentGold,
                              "Modo simulação: nada será excluído, só registrado no histórico.");
        if (selection.anyPermanent)
            ImGui::TextColored(theme::kDanger,
                              "Atenção: Lixeira e/ou pontos de restauração selecionados serão "
                              "removidos em definitivo, sem opção de desfazer.");
        if (selection.riskyHeuristic > 0)
            ImGui::TextColored(theme::kDanger,
                               "Atenção: %zu pasta(s) com chance média/alta de o app ainda existir "
                               "estão marcadas (vão para a Lixeira).",
                               selection.riskyHeuristic);

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
        DrawProgressRing(state.cleanProgress.Read());
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
