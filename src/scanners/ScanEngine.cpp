#include "storagecleaner/PathExclusion.h"
#include "storagecleaner/RecycleBinOps.h"
#include "storagecleaner/Scanner.h"
#include "storagecleaner/Utils.h"
#include "storagecleaner/VssRestorePoints.h"

#include <windows.h>

#include <iterator>

namespace storagecleaner {

namespace {

using ScannerFn = std::vector<ScanItem> (*)(const Config&, ProgressChannel&, std::atomic<bool>&);

struct ScannerEntry {
    Category category;
    ScannerFn scan;
};

// Ordem de execução das varreduras (a mesma exibida nos Resultados).
constexpr ScannerEntry kScanners[] = {
    {Category::TempFiles, ScanTempFiles},
    {Category::BrowserCache, ScanBrowserCache},
    {Category::Duplicate, ScanDuplicates},
    {Category::OldLogs, ScanOldLogs},
    {Category::RecycleBin, ScanRecycleBin},
    {Category::RestorePoints, ScanRestorePoints},
    {Category::OrphanedApps, ScanOrphanedAppData},
    {Category::WindowsUpdateCache, ScanWindowsUpdateCache},
    {Category::CrashDumps, ScanCrashDumps},
    {Category::ThumbnailCache, ScanThumbnailCache},
};
constexpr bool ScannersMatchCategories() {
    if (std::size(kScanners) != std::size(kAllCategories)) return false;
    for (std::size_t i = 0; i < std::size(kScanners); ++i)
        if (kScanners[i].category != kAllCategories[i]) return false;
    return true;
}
static_assert(ScannersMatchCategories(),
              "kScanners precisa de exatamente um scanner por categoria, na ordem de kAllCategories");

void AddBucket(ScanResult& result, Category category, std::vector<ScanItem>&& items,
               const std::vector<std::wstring>& excludedPaths) {
    // Ponto único antes de qualquer item chegar à UI ou à limpeza: um scanner
    // novo não precisa lembrar de respeitar a lista de exclusões.
    RemoveProtectedItems(items, excludedPaths);
    if (items.empty()) return;
    CategoryBucket bucket;
    bucket.category = category;
    for (const auto& item : items) bucket.totalBytes += item.sizeBytes;
    bucket.items = std::move(items);
    result.buckets.push_back(std::move(bucket));
}

} // namespace

void RemoveProtectedItems(std::vector<ScanItem>& items, const std::vector<std::wstring>& excludedPaths) {
    if (excludedPaths.empty() || items.empty()) return;
    std::vector<std::wstring> canonicalExcluded;
    canonicalExcluded.reserve(excludedPaths.size());
    for (const auto& p : excludedPaths) canonicalExcluded.push_back(util::CanonicalPath(p));

    std::erase_if(items, [&](const ScanItem& item) {
        // Testa a forma como o scanner achou e a canônica: a primeira cobre
        // itens que não existem mais em disco, a segunda nomes 8.3.
        return IsExcludedPath(item.path, item.isDirectory, canonicalExcluded) ||
               IsExcludedPath(util::CanonicalPath(item.path), item.isDirectory, canonicalExcluded);
    });
}

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

        const bool elevated = IsRunningElevated();
        for (const ScannerEntry& scanner : kScanners) {
            if (cancelRequested_.load()) break;
            if (!enabled(scanner.category)) continue;
            if (IsAdminRequiredCategory(scanner.category) && !elevated) {
                result.skippedNeedAdmin.push_back(scanner.category);
                continue;
            }
            if (IsPartiallyAdminCategory(scanner.category) && !elevated)
                result.partialNeedAdmin.push_back(scanner.category);
            AddBucket(result, scanner.category, scanner.scan(config, progress, cancelRequested_),
                      config.excludedPaths);
        }

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
    totals.drives = util::EnumerateDrives();

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
