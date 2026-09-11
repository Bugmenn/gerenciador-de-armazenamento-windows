#include "storagecleaner/RecycleBinOps.h"
#include "storagecleaner/Scanner.h"
#include "storagecleaner/Utils.h"

#include <windows.h>

namespace storagecleaner {

namespace {

void AddBucket(ScanResult& result, Category category, std::vector<ScanItem>&& items) {
    if (items.empty()) return;
    CategoryBucket bucket;
    bucket.category = category;
    for (const auto& item : items) bucket.totalBytes += item.sizeBytes;
    bucket.items = std::move(items);
    result.buckets.push_back(std::move(bucket));
}

} // namespace

void ScanEngine::RunAsync(const Config& config, ProgressChannel& progress) {
    if (running_.load()) return; // uma varredura já em andamento; UI deve desabilitar o botão

    if (worker_.joinable()) worker_.join(); // execução anterior já terminada
    running_.store(true);
    cancelRequested_.store(false);

    worker_ = std::thread([this, config, &progress]() {
        ScanResult result;
        result.scannedAt = std::chrono::system_clock::now();

        auto enabled = [&config](Category c) {
            auto it = config.perCategory.find(c);
            return it == config.perCategory.end() || it->second.enabled;
        };

        if (enabled(Category::TempFiles) && !cancelRequested_.load())
            AddBucket(result, Category::TempFiles, ScanTempFiles(config, progress, cancelRequested_));
        if (enabled(Category::BrowserCache) && !cancelRequested_.load())
            AddBucket(result, Category::BrowserCache,
                     ScanBrowserCache(config, progress, cancelRequested_));
        if (enabled(Category::Duplicate) && !cancelRequested_.load())
            AddBucket(result, Category::Duplicate, ScanDuplicates(config, progress, cancelRequested_));
        if (enabled(Category::OldLogs) && !cancelRequested_.load())
            AddBucket(result, Category::OldLogs, ScanOldLogs(config, progress, cancelRequested_));
        if (enabled(Category::RecycleBin) && !cancelRequested_.load())
            AddBucket(result, Category::RecycleBin,
                     ScanRecycleBin(config, progress, cancelRequested_));
        if (enabled(Category::RestorePoints) && !cancelRequested_.load())
            AddBucket(result, Category::RestorePoints,
                     ScanRestorePoints(config, progress, cancelRequested_));
        if (enabled(Category::OrphanedApps) && !cancelRequested_.load())
            AddBucket(result, Category::OrphanedApps,
                     ScanOrphanedAppData(config, progress, cancelRequested_));

        ProgressSnapshot finalSnapshot;
        finalSnapshot.phase = cancelRequested_.load() ? Phase::Cancelled : Phase::Done;
        finalSnapshot.fractionComplete = 1.0f;
        progress.Update(finalSnapshot);

        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            result_ = std::move(result);
        }
        running_.store(false);
    });
}

void ScanEngine::RequestCancel() { cancelRequested_.store(true); }

void ScanEngine::StopAndWait() {
    if (worker_.joinable()) {
        cancelRequested_.store(true);
        worker_.join();
    }
}

bool ScanEngine::TryTakeResult(ScanResult& out) {
    std::lock_guard<std::mutex> lock(resultMutex_);
    if (!result_.has_value()) return false;
    out = std::move(*result_);
    result_.reset();
    return true;
}

ScanEngine::~ScanEngine() { StopAndWait(); }

LightScanTotals RunLightBackgroundScan() {
    // Varredura leve: só tamanhos agregados já conhecidos por chamadas O(1)
    // (Lixeira) ou baratas (soma do diretório de temp do usuário atual), sem
    // hash de duplicados nem varredura de todos os perfis — mantém o
    // Dashboard atualizado sem competir por I/O com o uso normal da máquina.
    LightScanTotals totals;
    totals.recycleBinBytes = QueryRecycleBinSizeBytes();

    if (wchar_t tempPath[MAX_PATH]; ::GetTempPathW(MAX_PATH, tempPath) > 0) {
        // A varredura leve não tem um cancelamento real acionável pelo
        // usuário (roda sozinha em segundo plano) — usa um atomic local
        // sempre false só para satisfazer a assinatura compartilhada com as
        // demais varreduras.
        std::atomic<bool> noCancel{false};
        totals.tempBytes = util::DirectorySize(tempPath, noCancel);
    }

    return totals;
}

} // namespace storagecleaner
