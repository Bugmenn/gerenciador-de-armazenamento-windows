#include "storagecleaner/Scanner.h"
#include "storagecleaner/Utils.h"

#include <windows.h>
#include <shlobj.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace storagecleaner {

std::vector<ScanItem> ScanWindowsUpdateCache(const Config& config, ProgressChannel& progress,
                                             std::atomic<bool>& cancel) {
    std::vector<ScanItem> items;
    ProgressSnapshot snapshot;
    snapshot.phase = Phase::ScanningWindowsUpdate;
    progress.Update(snapshot);

    std::wstring windowsDir = util::KnownFolderPath(FOLDERID_Windows);
    if (windowsDir.empty()) return items;
    fs::path downloadDir = fs::path(windowsDir) / L"SoftwareDistribution" / L"Download";

    // Um item por entrada de topo (não a pasta Download inteira): o serviço
    // Windows Update espera que a pasta exista, então só o conteúdo sai.
    std::error_code ec;
    for (fs::directory_iterator it(downloadDir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (cancel.load()) break;
        const fs::path& entry = it->path();
        std::error_code entryEc;
        bool isDir = it->is_directory(entryEc);

        std::uint64_t size = 0;
        if (isDir) {
            // Atividade no último dia = download/instalação possivelmente em
            // andamento; mexer nisso pode corromper uma atualização pendente.
            util::DirectoryStats stats = util::ComputeDirectoryStats(entry.wstring(), 1, cancel);
            if (stats.hasRecentActivity) continue;
            size = stats.totalBytes;
        } else {
            if (!util::IsOlderThanDays(entry.wstring(), 1)) continue;
            size = it->file_size(entryEc);
            if (entryEc) continue;
        }
        if (size < config.minFileSizeBytes) continue;

        items.push_back(ScanItem{entry.wstring(), size, Category::WindowsUpdateCache, L"", isDir});
        snapshot.itemsProcessed++;
        snapshot.bytesFoundSoFar += size;
        snapshot.currentItem = entry.wstring();
        progress.Update(snapshot);
    }

    snapshot.fractionComplete = 1.0f;
    progress.Update(snapshot);
    return items;
}

} // namespace storagecleaner
