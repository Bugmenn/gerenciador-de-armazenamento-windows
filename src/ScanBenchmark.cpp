#include "storagecleaner/Config.h"
#include "storagecleaner/Scanner.h"
#include "storagecleaner/Utils.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace storagecleaner {

int RunScanBenchmark(const std::wstring& reportPath) {
    ProgressChannel progress;
    ScanEngine engine;
    ScanResult result;

    auto started = std::chrono::steady_clock::now();
    engine.RunAsync(Config::Defaults(), progress);
    while (!engine.TryTakeResult(result)) ::Sleep(50);
    double totalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

    std::ofstream out(std::filesystem::path(reportPath), std::ios::binary | std::ios::trunc);
    if (!out) return 1;
    char line[256];
    for (const auto& [category, ms] : result.categoryMillis) {
        std::size_t items = 0;
        std::uint64_t bytes = 0;
        for (const auto& bucket : result.buckets) {
            if (bucket.category != category) continue;
            items = bucket.items.size();
            bytes = bucket.totalBytes;
        }
        std::snprintf(line, sizeof(line), "%-36s %10.0f ms  %7zu itens  %s\n", CategoryName(category), ms, items,
                      util::FormatSize(bytes).c_str());
        out << line;
    }
    for (Category skipped : result.skippedNeedAdmin)
        out << CategoryName(skipped) << ": pulada (requer Administrador)\n";
    std::snprintf(line, sizeof(line), "TOTAL %.0f ms\n", totalMs);
    out << line;
    return out ? 0 : 1;
}

} // namespace storagecleaner
