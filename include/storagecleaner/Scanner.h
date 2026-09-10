#pragma once

#include "storagecleaner/Config.h"
#include "storagecleaner/ProgressState.h"
#include "storagecleaner/ScanItem.h"
#include "storagecleaner/ScanResult.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace storagecleaner {

// Todo scanner de categoria segue essa mesma assinatura: lê a config (raízes
// customizadas, limites), publica progresso no canal, e observa `cancel` a
// cada iteração para poder interromper rapidamente uma varredura longa.
std::vector<ScanItem> ScanTempFiles(const Config&, ProgressChannel&, std::atomic<bool>& cancel);
std::vector<ScanItem> ScanBrowserCache(const Config&, ProgressChannel&, std::atomic<bool>& cancel);
std::vector<ScanItem> ScanDuplicates(const Config&, ProgressChannel&, std::atomic<bool>& cancel);
std::vector<ScanItem> ScanOldLogs(const Config&, ProgressChannel&, std::atomic<bool>& cancel);
std::vector<ScanItem> ScanRecycleBin(const Config&, ProgressChannel&, std::atomic<bool>& cancel);
std::vector<ScanItem> ScanRestorePoints(const Config&, ProgressChannel&, std::atomic<bool>& cancel);
std::vector<ScanItem> ScanOrphanedAppData(const Config&, ProgressChannel&, std::atomic<bool>& cancel);

// Varredura leve usada em segundo plano (apenas tamanhos agregados de
// Lixeira e temporários, sem hash de duplicados) para manter o Dashboard
// atualizado sem custo alto de I/O/CPU.
struct LightScanTotals {
    std::uint64_t tempBytes = 0;
    std::uint64_t recycleBinBytes = 0;
};
LightScanTotals RunLightBackgroundScan(const Config&);

class ScanEngine {
public:
    // Roda todos os scanners habilitados sequencialmente numa worker thread;
    // não bloqueia a chamadora. Uma nova chamada só deve ser feita depois que
    // IsRunning() voltar a false.
    void RunAsync(const Config& config, ProgressChannel& progress);
    void RequestCancel();
    bool IsRunning() const { return running_.load(); }

    // Não bloqueante: true e move o resultado para `out` se uma execução
    // tiver terminado desde a última chamada.
    bool TryTakeResult(ScanResult& out);

    ~ScanEngine();

private:
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelRequested_{false};
    std::thread worker_;
    std::mutex resultMutex_;
    std::optional<ScanResult> result_;
};

} // namespace storagecleaner
