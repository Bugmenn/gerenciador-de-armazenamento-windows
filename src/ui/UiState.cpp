#include "Notifications.h"
#include "UiState.h"
#include "storagecleaner/HistoryStore.h"
#include "storagecleaner/PathExclusion.h"
#include "storagecleaner/Utils.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <iterator>

namespace storagecleaner::ui {

namespace {

// Converte a config de agendamento (frequência + horário) num intervalo em
// horas, aplicado sobre a última execução automática registrada no
// histórico. Mantém a lógica simples e determinística em vez de tentar
// alinhar exatamente com o relógio de parede em todos os fusos/DST.
int FrequencyToHours(AutoCleanFrequency freq) {
    switch (freq) {
        case AutoCleanFrequency::Daily:       return 24;
        case AutoCleanFrequency::Every7Days:  return 24 * 7;
        case AutoCleanFrequency::Every15Days: return 24 * 15;
        case AutoCleanFrequency::Monthly:     return 24 * 30;
    }
    return 24 * 7;
}

// Só passa a ser elegível depois que o horário configurado (HH:MM, hora
// local) já tiver passado no dia atual — sem essa checagem, "todo dia às
// 03:00" disparava a qualquer hora em que o app estivesse aberto (o campo
// `autoCleanTimeOfDay` era lido/gravado na config mas nunca consultado aqui).
bool IsPastConfiguredTimeOfDay(const std::string& hhmm) {
    int hour = 0, minute = 0;
    if (std::sscanf(hhmm.c_str(), "%d:%d", &hour, &minute) != 2) return true; // config inválida: não bloqueia

    std::time_t now = std::time(nullptr);
    std::tm nowTm{};
    localtime_s(&nowTm, &now);
    return (nowTm.tm_hour > hour) || (nowTm.tm_hour == hour && nowTm.tm_min >= minute);
}

// `lastTriggerThisSession` cobre os disparos que não gravam limpeza real no
// histórico — simulação (--dry-run) e "pedir confirmação" sem o usuário
// confirmar. Sem ele, o timer de 60 s refazia a varredura completa a cada
// minuto até o app fechar.
bool ShouldRunAutoClean(const Config& config,
                        std::optional<std::chrono::system_clock::time_point> lastTriggerThisSession) {
    if (!config.autoCleanEnabled) return false;
    if (!IsPastConfiguredTimeOfDay(config.autoCleanTimeOfDay)) return false;

    auto last = LastAutoCleanTimestamp();
    if (lastTriggerThisSession && (!last || *lastTriggerThisSession > *last)) last = lastTriggerThisSession;
    if (!last.has_value()) return true; // nunca rodou: dispara na primeira checagem apos o horario

    auto elapsedHours =
        std::chrono::duration_cast<std::chrono::hours>(std::chrono::system_clock::now() - *last)
            .count();
    return elapsedHours >= FrequencyToHours(config.autoCleanFrequency);
}

} // namespace

void UiState::StartScan(bool triggeredByAutoClean) {
    if (scanEngine.IsRunning()) return;
    hasScanResult = false;
    currentScanIsAutoClean = triggeredByAutoClean;
    // Zera antes de a worker começar: sem isso, o primeiro quadro mostraria o
    // 100% da varredura anterior. Seguro: nenhuma worker usa o canal agora.
    scanProgress.SetStage(0, 0);
    scanProgress.Update(ProgressSnapshot{});
    scanEngine.RunAsync(config, scanProgress);
}

void UiState::StartClean(const std::vector<ScanItem>& selected, bool automatic) {
    if (cleaning.load() || selected.empty()) return;

    currentScanIsAutoClean = false; // consumido: a próxima varredura volta a ser manual por padrão
    {
        std::lock_guard<std::mutex> lock(cleanResultMutex);
        pendingCleanResult.reset();
    }
    cleaning.store(true);
    cleanCancel.store(false);

    if (cleanThread.joinable()) cleanThread.join();
    cleanProgress.SetStage(0, 0);
    cleanProgress.Update(ProgressSnapshot{});

    cleanThread = std::thread([this, items = selected, automatic, dryRun = dryRun,
                               excluded = config.excludedPaths]() mutable {
        // O ScanEngine já filtra as pastas protegidas, mas o resultado pode
        // ser de antes de o usuário proteger uma pasta. Roda aqui, e não na
        // UI thread, porque canonizar cada caminho faz I/O de metadados.
        RemoveProtectedItems(items, excluded);
        HistoryEntry entry = CleanItems(items, cleanProgress, cleanCancel, automatic, dryRun);
        AppendHistoryEntry(entry);
        historyLoaded = false; // força recarregar na próxima abertura da aba Histórico

        {
            std::lock_guard<std::mutex> lock(cleanResultMutex);
            pendingCleanResult = std::move(entry);
        }
        cleaning.store(false);
    });
}

std::vector<ScanItem> UiState::CollectSelectedItems() const {
    std::vector<ScanItem> result;
    for (std::size_t bucketIdx = 0; bucketIdx < lastScanResult.buckets.size(); ++bucketIdx) {
        const auto& bucket = lastScanResult.buckets[bucketIdx];
        if (bucketIdx >= selection.size()) continue;
        for (std::size_t itemIdx = 0; itemIdx < bucket.items.size(); ++itemIdx) {
            if (itemIdx < selection[bucketIdx].size() && selection[bucketIdx][itemIdx])
                result.push_back(bucket.items[itemIdx]);
        }
    }
    return result;
}

UiState::SelectionSummary UiState::GetSelectionSummary() const {
    SelectionSummary summary;
    for (std::size_t bucketIdx = 0; bucketIdx < lastScanResult.buckets.size(); ++bucketIdx) {
        if (bucketIdx >= selection.size()) continue;
        const auto& bucket = lastScanResult.buckets[bucketIdx];
        const auto& sel = selection[bucketIdx];
        bool permanent = IsPermanentCategory(bucket.category);
        for (std::size_t itemIdx = 0; itemIdx < bucket.items.size(); ++itemIdx) {
            if (itemIdx >= sel.size() || !sel[itemIdx]) continue;
            summary.bytes += bucket.items[itemIdx].sizeBytes;
            summary.count++;
            if (permanent) summary.anyPermanent = true;
        }
    }
    return summary;
}

void UiState::CheckLowDiskAlert() {
    if (!config.lowDiskAlertEnabled) {
        lowDiskAlerted.clear();
        return;
    }
    std::wstring text;
    for (const auto& drive : GetLightTotals().drives) {
        // Pendrive/cartão cheio é comum e esperado; alertar só unidades fixas.
        if (drive.isRemovable) continue;
        double freePercent = 100.0 * static_cast<double>(drive.freeBytes) /
                             static_cast<double>(drive.totalBytes);
        bool low = freePercent < config.lowDiskAlertPercent;
        if (!low) {
            lowDiskAlerted.erase(drive.root); // rearma para a próxima queda
            continue;
        }
        if (!lowDiskAlerted.insert(drive.root).second) continue; // já avisado
        if (!text.empty()) text += L" ";
        text += LowDiskDriveText(drive.root, drive.freeBytes, static_cast<int>(freePercent));
    }
    // Um balão para todas as unidades, em vez de um por unidade na fila.
    if (!text.empty()) NotifyLowDisk(text);
}

void UiState::ApplyExclusionsToLastResult() {
    // Versão textual (sem canonizar) porque roda na UI thread; a limpeza
    // refaz o filtro completo na worker antes de excluir qualquer coisa.
    for (std::size_t b = 0; b < lastScanResult.buckets.size() && b < selection.size(); ++b) {
        auto& bucket = lastScanResult.buckets[b];
        auto& sel = selection[b];
        std::vector<ScanItem> keptItems;
        std::vector<bool> keptSel;
        bucket.totalBytes = 0;
        for (std::size_t i = 0; i < bucket.items.size(); ++i) {
            if (IsExcludedPath(bucket.items[i].path, bucket.items[i].isDirectory, config.excludedPaths))
                continue;
            bucket.totalBytes += bucket.items[i].sizeBytes;
            keptItems.push_back(std::move(bucket.items[i]));
            keptSel.push_back(i < sel.size() && sel[i]);
        }
        bucket.items = std::move(keptItems);
        sel = std::move(keptSel);
    }
    OnResultChanged();
}

void UiState::AdoptScanResult(ScanResult result) {
    lastScanResult = std::move(result);
    selection.assign(lastScanResult.buckets.size(), {});
    for (std::size_t i = 0; i < lastScanResult.buckets.size(); ++i) {
        bool preselect = !IsHeuristicCategory(lastScanResult.buckets[i].category);
        selection[i].assign(lastScanResult.buckets[i].items.size(), preselect);
    }
    // Varredura nova: pastas voltam a fechar, mas a ordenação escolhida
    // fica — a tabela do ImGui guarda a seta do cabeçalho e não seria
    // avisada de um reset aqui.
    for (auto& view : bucketViews) view.toggledFolders.clear();
    OnResultChanged();
    hasScanResult = true;
    // Só a varredura manual troca de aba: a agendada não pode tirar o
    // usuário do que ele estiver fazendo (ex.: digitando nas Configurações).
    if (!currentScanIsAutoClean) requestedTab = 1;
}

void UiState::OnResultChanged() {
    ++resultVersion;
    // Preserva ordenação e pastas abertas de cada categoria: o resultado
    // mudou (ex.: exclusões aplicadas), mas o usuário continua na mesma tela.
    bucketViews.resize(lastScanResult.buckets.size());
    cleanableByDrive = CleanableBytesByDrive(lastScanResult.buckets);
}

ItemFilter UiState::CurrentItemFilter() const {
    ItemFilter filter;
    // Chamado várias vezes por linha desenhada: só reconverte quando o texto muda.
    if (filterUtf8Cache != resultsFilter) {
        filterUtf8Cache = resultsFilter;
        filterWideCache = util::Utf8ToWide(filterUtf8Cache);
    }
    filter.needle = filterWideCache;
    int idx = std::clamp(resultsMinSizeIdx, 0, static_cast<int>(std::size(kMinSizeOptions)) - 1);
    filter.minBytes = kMinSizeOptions[idx].bytes;
    return filter;
}

bool UiState::IsFilterActive() const { return resultsFilter[0] != '\0' || resultsMinSizeIdx > 0; }

bool UiState::IsGroupOpen(const BucketView& view, std::size_t groupIdx) const {
    if (groupIdx >= view.groups.size()) return false;
    bool openByDefault = resultsFilter[0] != '\0';
    return openByDefault != (view.toggledFolders.count(view.groups[groupIdx].folder) > 0);
}

BucketView& UiState::GetBucketView(std::size_t bucketIdx) {
    if (bucketViews.size() < lastScanResult.buckets.size())
        bucketViews.resize(lastScanResult.buckets.size());
    BucketView& view = bucketViews[bucketIdx];
    ItemFilter filter = CurrentItemFilter();
    if (view.builtVersion != resultVersion || view.builtNeedle != filter.needle ||
        view.builtMinBytes != filter.minBytes || view.builtSortKey != view.sortKey ||
        view.builtSortDescending != view.sortDescending) {
        // O padrão aberto/fechado muda com o texto do filtro; inversões
        // feitas sob o padrão anterior não valem mais.
        if (view.builtNeedle.empty() != filter.needle.empty()) view.toggledFolders.clear();
        view.groups = GroupItemsByFolder(lastScanResult.buckets[bucketIdx].items, filter,
                                         view.sortKey, view.sortDescending);
        view.visibleItems = 0;
        for (const auto& group : view.groups) view.visibleItems += group.itemIndices.size();
        view.builtVersion = resultVersion;
        view.builtNeedle = std::move(filter.needle);
        view.builtMinBytes = filter.minBytes;
        view.builtSortKey = view.sortKey;
        view.builtSortDescending = view.sortDescending;
        view.rowsDirty = true;
    }
    if (view.rowsDirty) {
        view.rows.clear();
        for (std::size_t g = 0; g < view.groups.size(); ++g) {
            const ItemGroup& group = view.groups[g];
            if (group.itemIndices.size() == 1) {
                view.rows.push_back(ResultRow{g, group.itemIndices[0]});
                continue;
            }
            view.rows.push_back(ResultRow{g, ResultRow::kGroupRow});
            if (IsGroupOpen(view, g))
                for (std::size_t i : group.itemIndices) view.rows.push_back(ResultRow{g, i});
        }
        view.rowsDirty = false;
    }
    return view;
}

void UiState::ToggleGroup(std::size_t bucketIdx, std::size_t groupIdx) {
    BucketView& view = GetBucketView(bucketIdx);
    if (groupIdx >= view.groups.size()) return;
    const std::wstring& folder = view.groups[groupIdx].folder;
    if (!view.toggledFolders.erase(folder)) view.toggledFolders.insert(folder);
    view.rowsDirty = true;
}

void UiState::SetGroupSelected(std::size_t bucketIdx, std::size_t groupIdx, bool selected) {
    BucketView& view = GetBucketView(bucketIdx);
    if (groupIdx >= view.groups.size() || bucketIdx >= selection.size()) return;
    auto& sel = selection[bucketIdx];
    for (std::size_t i : view.groups[groupIdx].itemIndices)
        if (i < sel.size()) sel[i] = selected;
}

void UiState::SetVisibleSelected(std::size_t bucketIdx, bool selected) {
    BucketView& view = GetBucketView(bucketIdx);
    for (std::size_t g = 0; g < view.groups.size(); ++g) SetGroupSelected(bucketIdx, g, selected);
}

namespace {

TriState CountToTriState(std::size_t marked, std::size_t total) {
    if (total == 0 || marked == 0) return TriState::None;
    return marked == total ? TriState::All : TriState::Some;
}

} // namespace

TriState UiState::GroupSelectionState(std::size_t bucketIdx, std::size_t groupIdx) {
    BucketView& view = GetBucketView(bucketIdx);
    if (groupIdx >= view.groups.size() || bucketIdx >= selection.size()) return TriState::None;
    const auto& sel = selection[bucketIdx];
    std::size_t marked = 0;
    for (std::size_t i : view.groups[groupIdx].itemIndices)
        if (i < sel.size() && sel[i]) ++marked;
    return CountToTriState(marked, view.groups[groupIdx].itemIndices.size());
}

TriState UiState::VisibleSelectionState(std::size_t bucketIdx) {
    BucketView& view = GetBucketView(bucketIdx);
    if (bucketIdx >= selection.size()) return TriState::None;
    const auto& sel = selection[bucketIdx];
    std::size_t marked = 0;
    for (const auto& group : view.groups)
        for (std::size_t i : group.itemIndices)
            if (i < sel.size() && sel[i]) ++marked;
    return CountToTriState(marked, view.visibleItems);
}

void UiState::SetResultsSort(std::size_t bucketIdx, ItemSortKey key, bool descending, bool fromHeader) {
    if (bucketViews.size() < lastScanResult.buckets.size())
        bucketViews.resize(lastScanResult.buckets.size());
    if (bucketIdx >= bucketViews.size()) return;
    BucketView& view = bucketViews[bucketIdx];
    view.sortKey = key;
    view.sortDescending = descending;
    if (!fromHeader) view.sortChangedExternally = true;
}

void UiState::EnqueueBalloonTest() {
    // Valores fictícios; os textos vêm das mesmas funções dos fluxos reais
    // (Notifications.cpp), e a fila os mostra um a cada 8 s.
    auto prefix = [](int n) { return L"[Teste " + std::to_wstring(n) + L"/6] "; };
    const std::uint64_t sample = 1288490189; // ~1,2 GB
    NotifyScanDone(sample, /*scheduled=*/false, prefix(1));
    NotifyScanDone(0, /*scheduled=*/false, prefix(2));
    NotifyScanDone(sample, /*scheduled=*/true, prefix(3));
    NotifyAutoCleanDone(sample, /*dryRun=*/true, prefix(4));
    NotifyAutoCleanDone(sample, /*dryRun=*/false, prefix(5));
    NotifyLowDisk(LowDiskDriveText(L"C:\\", 10200547328ull /* ~9,5 GB */, 4), prefix(6));
}

void UiState::Tick() {
    if (testBalloons && !balloonTestQueued) {
        EnqueueBalloonTest();
        balloonTestQueued = true;
    }

    // Absorve o resultado da varredura, se pronto, e prepara a seleção
    // (todas as categorias, exceto as heurísticas, já vêm marcadas).
    ScanResult freshResult;
    if (scanEngine.TryTakeResult(freshResult)) {
        AdoptScanResult(std::move(freshResult));

        if (currentScanIsAutoClean && config.autoCleanMode == AutoCleanMode::AutoDelete) {
            // Disparo automático em modo "excluir sem perguntar": limpa direto,
            // exceto "Dados orfaos", que nunca entra no auto-clean (ver
            // Config.h / IsHeuristicCategory) mesmo que o usuário tenha
            // habilitado a categoria manualmente.
            std::vector<ScanItem> autoItems;
            for (const auto& item : CollectSelectedItems())
                if (!IsHeuristicCategory(item.category)) autoItems.push_back(item);
            if (!autoItems.empty()) {
                autoCleanNotifyArmed = true;
                StartClean(autoItems, /*automatic=*/true);
            }
            currentScanIsAutoClean = false;
        } else if (currentScanIsAutoClean) {
            // Modo "pedir confirmacao": só avisa por balão — abrir a janela
            // sozinho roubaria o foco de quem estiver jogando, digitando ou
            // apresentando. Clicar no balão abre a janela (App.cpp,
            // NIN_BALLOONUSERCLICK). currentScanIsAutoClean continua true até
            // o clique em "Limpar selecionados" (ver ResultsPanel), para que
            // o HistoryEntry resultante ainda seja marcado como automático.
            // Sem balão se não há nada a revisar ou se a janela está à vista
            // (a aba Resultados já foi selecionada acima); escondida na
            // bandeja ou minimizada, avisa.
            std::uint64_t found = lastScanResult.GrandTotalBytes();
            if (found > 0 && (!windowVisible || windowMinimized))
                NotifyScanDone(found, /*scheduled=*/true);
        } else if ((!windowVisible || windowMinimized) &&
                   scanProgress.Read().phase != Phase::Cancelled) {
            // Varredura manual (tela ou "Escanear agora" da bandeja) que
            // terminou com a janela fora de vista: o usuário pediu e espera um
            // retorno, inclusive "nada encontrado". Cancelada não avisa (foi
            // ele mesmo quem cancelou).
            NotifyScanDone(lastScanResult.GrandTotalBytes(), /*scheduled=*/false);
        }
    }

    // Limpeza real concluída: a parte "limpável" de cada unidade (rosca do
    // Painel) deixou de existir — zera em vez de afirmar um valor velho.
    bool cleaningNow = cleaning.load();
    if (cleanWasRunning && !cleaningNow) {
        std::lock_guard<std::mutex> lock(cleanResultMutex);
        if (pendingCleanResult && !pendingCleanResult->wasDryRun && pendingCleanResult->totalBytesFreed > 0)
            cleanableByDrive.clear();
    }
    cleanWasRunning = cleaningNow;

    // Limpeza automática (AutoDelete) concluída: avisa por balão da bandeja,
    // já que o usuário pode nem estar com a janela aberta nesse fluxo.
    if (autoCleanNotifyArmed && !cleaning.load()) {
        std::lock_guard<std::mutex> lock(cleanResultMutex);
        if (pendingCleanResult.has_value()) {
            NotifyAutoCleanDone(pendingCleanResult->totalBytesFreed, pendingCleanResult->wasDryRun);
            autoCleanNotifyArmed = false;
        }
    }

    // Balão da bandeja sai daqui (UI thread), não da worker da varredura leve.
    if (lightScanFresh.exchange(false)) CheckLowDiskAlert();
    PumpNotifications();

    // --ui-selftest: os dados são fictícios e nada pode tocar o disco real nem
    // disparar varredura/limpeza por baixo do roteiro.
    if (selfTest) return;

    // Varredura leve periódica em segundo plano — roda em worker thread
    // própria (nunca direto aqui em Tick(), que é a UI thread): apesar do
    // nome "leve", ainda faz uma chamada Shell e uma varredura recursiva de
    // %TEMP%, e rodar isso na thread de renderização travaria a UI a cada
    // intervalo. O intervalo é sempre clampado a um mínimo (5 min) mesmo que
    // o usuário configure um valor menor/zero/negativo nas Configurações.
    auto now = std::chrono::steady_clock::now();
    auto sinceLast =
        std::chrono::duration_cast<std::chrono::minutes>(now - lastLightScan).count();
    int intervalMinutes = std::max(config.backgroundScanIntervalMinutes, 5);
    if (!lightScanInProgress.load() &&
        (lastLightScan.time_since_epoch().count() == 0 || sinceLast >= intervalMinutes)) {
        lastLightScan = now;
        lightScanInProgress.store(true);
        if (lightScanThread.joinable()) lightScanThread.join();

        lightScanThread = std::thread([this]() {
            LightScanTotals totals = RunLightBackgroundScan();
            {
                std::lock_guard<std::mutex> lock(lightScanMutex);
                lightTotals = totals;
            }
            lightScanFresh.store(true);
            lightScanInProgress.store(false);
        });
    }

    // Timer do auto-clean: checa no máximo uma vez por minuto se está na hora.
    auto sinceAutoCleanCheck = std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::system_clock::now() - lastAutoCleanCheck)
                                  .count();
    if (sinceAutoCleanCheck >= 60) {
        lastAutoCleanCheck = std::chrono::system_clock::now();
        // No --test-balloons o agendamento fica parado: o teste é só visual
        // e não pode disparar varredura/limpeza por baixo.
        if (!testBalloons && !scanEngine.IsRunning() && !cleaning.load() &&
            ShouldRunAutoClean(config, lastAutoCleanTrigger)) {
            lastAutoCleanTrigger = std::chrono::system_clock::now();
            StartScan(/*triggeredByAutoClean=*/true);
        }
    }
}

} // namespace storagecleaner::ui
