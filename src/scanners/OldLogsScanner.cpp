#include "storagecleaner/Scanner.h"
#include "storagecleaner/Utils.h"

#include <windows.h>
#include <shlobj.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace storagecleaner {

namespace {

void ScanDirForOldLogs(const fs::path& dir, const Config& config, ProgressChannel& progress,
                       std::atomic<bool>& cancel, std::vector<ScanItem>& out,
                       ProgressSnapshot& snapshot) {
    const std::int64_t now = util::NowFileTimeTicks();
    util::ForEachFileRecursive(dir.wstring(), cancel, [&](const util::FileEntry& entry) {
        const std::wstring& path = entry.path;
        const std::uint64_t size = entry.sizeBytes;
        if (fs::path(path).extension() != L".log") return;
        // Data da listagem como filtro barato, reconfirmada no arquivo: um log
        // mantido aberto por um serviço tem a data da listagem atrasada e
        // pareceria antigo.
        if (util::AgeHoursAt(entry.lastWriteTicks, now) < static_cast<std::int64_t>(config.oldLogsThresholdDays) * 24)
            return;
        if (!util::IsOlderThanDays(path, config.oldLogsThresholdDays)) return;

        out.push_back(ScanItem{path, size, Category::OldLogs, L"", false});
        snapshot.itemsProcessed++;
        snapshot.bytesFoundSoFar += size;
        snapshot.currentItem = path;
        if (snapshot.itemsProcessed % 32 == 0) progress.Update(snapshot);
    });
}

} // namespace

std::vector<ScanItem> ScanOldLogs(const Config& config, ProgressChannel& progress,
                                   std::atomic<bool>& cancel) {
    std::vector<ScanItem> items;
    ProgressSnapshot snapshot;
    snapshot.phase = Phase::ScanningLogs;
    progress.Update(snapshot);

    std::wstring windowsDir = util::KnownFolderPath(FOLDERID_Windows);
    if (!windowsDir.empty())
        ScanDirForOldLogs(fs::path(windowsDir) / L"Logs", config, progress, cancel, items,
                          snapshot);

    if (wchar_t programData[MAX_PATH]; ::GetEnvironmentVariableW(L"ProgramData", programData,
                                                                  MAX_PATH) > 0 &&
                                       !cancel.load())
        ScanDirForOldLogs(programData, config, progress, cancel, items, snapshot);

    for (const auto& profileDir : util::EnumerateUserProfileDirs()) {
        if (cancel.load()) break;
        ScanDirForOldLogs(fs::path(profileDir) / L"AppData" / L"Local", config, progress, cancel,
                          items, snapshot);
    }

    snapshot.fractionComplete = 1.0f;
    progress.Update(snapshot);
    return items;
}

} // namespace storagecleaner
