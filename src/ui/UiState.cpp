#include "TrayIcon.h"
#include "UiState.h"
#include "storagecleaner/HistoryStore.h"
#include "storagecleaner/Utils.h"

#include <ctime>

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

bool ShouldRunAutoClean(const Config& config) {
    if (!config.autoCleanEnabled) return false;

    auto last = LastAutoCleanTimestamp();
    if (!last.has_value()) return true; // nunca rodou: dispara na primeira checagem

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
    cleanProgress.ResetCancel();

    if (cleanThread.joinable()) cleanThread.join();

    cleanThread = std::thread([this, selected, automatic]() {
        HistoryEntry entry = CleanItems(selected, cleanProgress, cleanCancel, automatic);
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

void UiState::Tick() {
    // Absorve o resultado da varredura, se pronto, e prepara a seleção
    // (todas as categorias, exceto as heurísticas, já vêm marcadas).
    ScanResult freshResult;
    if (scanEngine.TryTakeResult(freshResult)) {
        lastScanResult = std::move(freshResult);
        selection.assign(lastScanResult.buckets.size(), {});
        for (std::size_t i = 0; i < lastScanResult.buckets.size(); ++i) {
            bool preselect = !IsHeuristicCategory(lastScanResult.buckets[i].category);
            selection[i].assign(lastScanResult.buckets[i].items.size(), preselect);
        }
        hasScanResult = true;
        activeTab = 1; // pula para a aba Resultados

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
            // Modo "pedir confirmacao": só traz a janela para frente e espera
            // o usuário revisar; currentScanIsAutoClean continua true até o
            // clique em "Limpar selecionados" (ver ResultsPanel), para que o
            // HistoryEntry resultante ainda seja marcado como automático.
            windowVisible = true;
        }
    }

    // Limpeza automática (AutoDelete) concluída: avisa por balão da bandeja,
    // já que o usuário pode nem estar com a janela aberta nesse fluxo.
    if (autoCleanNotifyArmed && !cleaning.load()) {
        std::lock_guard<std::mutex> lock(cleanResultMutex);
        if (pendingCleanResult.has_value()) {
            std::wstring text =
                L"Espaco liberado: " + util::Utf8ToWide(util::FormatSize(pendingCleanResult->totalBytesFreed));
            ShowTrayBalloon(L"Limpeza automatica concluida", text.c_str());
            autoCleanNotifyArmed = false;
        }
    }

    // Varredura leve periódica em segundo plano.
    auto now = std::chrono::steady_clock::now();
    auto sinceLast =
        std::chrono::duration_cast<std::chrono::minutes>(now - lastLightScan).count();
    if (lastLightScan.time_since_epoch().count() == 0 ||
        sinceLast >= config.backgroundScanIntervalMinutes) {
        lightTotals = RunLightBackgroundScan(config);
        lastLightScan = now;
    }

    // Timer do auto-clean: checa no máximo uma vez por minuto se está na hora.
    auto sinceAutoCleanCheck = std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::system_clock::now() - lastAutoCleanCheck)
                                  .count();
    if (sinceAutoCleanCheck >= 60) {
        lastAutoCleanCheck = std::chrono::system_clock::now();
        if (!scanEngine.IsRunning() && !cleaning.load() && ShouldRunAutoClean(config))
            StartScan(/*triggeredByAutoClean=*/true);
    }
}

} // namespace storagecleaner::ui
