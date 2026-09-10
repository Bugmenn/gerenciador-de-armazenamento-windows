#include "storagecleaner/Scanner.h"
#include "storagecleaner/Utils.h"

#include <windows.h>
#include <knownfolders.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace storagecleaner {

namespace {

void ScanDirForOldLogs(const fs::path& dir, const Config& config, ProgressChannel& progress,
                       std::atomic<bool>& cancel, std::vector<ScanItem>& out,
                       ProgressSnapshot& snapshot) {
    std::error_code ec;
    fs::recursive_directory_iterator it(
        dir, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    for (; it != end && !ec; it.increment(ec)) {
        if (cancel.load()) return;

        std::error_code fileEc;
        if (!it->is_regular_file(fileEc) || fileEc) continue;
        if (it->path().extension() != L".log") continue;

        if (!util::IsOlderThanDays(it->path().wstring(), config.oldLogsThresholdDays)) continue;

        auto size = it->file_size(fileEc);
        if (fileEc) continue;

        out.push_back(ScanItem{it->path().wstring(), size, Category::OldLogs, L"", false});
        snapshot.itemsProcessed++;
        snapshot.bytesFoundSoFar += size;
        snapshot.currentItem = it->path().wstring();
        if (snapshot.itemsProcessed % 32 == 0) progress.Update(snapshot);
    }
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
