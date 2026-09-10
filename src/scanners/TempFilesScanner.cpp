#include "storagecleaner/Scanner.h"
#include "storagecleaner/Utils.h"

#include <windows.h>
#include <knownfolders.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace storagecleaner {

namespace {

void ScanDirForTempFiles(const fs::path& dir, const Config& config,
                         ProgressChannel& progress, std::atomic<bool>& cancel,
                         std::vector<ScanItem>& out, ProgressSnapshot& snapshot) {
    std::error_code ec;
    fs::recursive_directory_iterator it(
        dir, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    for (; it != end && !ec; it.increment(ec)) {
        if (cancel.load()) return;

        std::error_code fileEc;
        if (!it->is_regular_file(fileEc) || fileEc) continue;

        auto size = it->file_size(fileEc);
        if (fileEc || size < config.minFileSizeBytes) continue;

        out.push_back(ScanItem{it->path().wstring(), size, Category::TempFiles, L"", false});

        snapshot.itemsProcessed++;
        snapshot.bytesFoundSoFar += size;
        snapshot.currentItem = it->path().wstring();
        if (snapshot.itemsProcessed % 32 == 0) progress.Update(snapshot);
    }
}

} // namespace

std::vector<ScanItem> ScanTempFiles(const Config& config, ProgressChannel& progress,
                                     std::atomic<bool>& cancel) {
    std::vector<ScanItem> items;
    ProgressSnapshot snapshot;
    snapshot.phase = Phase::ScanningTemp;
    progress.Update(snapshot);

    // %TEMP% do usuário atual.
    if (wchar_t tempPath[MAX_PATH]; ::GetTempPathW(MAX_PATH, tempPath) > 0)
        ScanDirForTempFiles(tempPath, config, progress, cancel, items, snapshot);

    // C:\Windows\Temp — compartilhado por todos os usuários e por serviços.
    std::wstring windowsDir = util::KnownFolderPath(FOLDERID_Windows);
    if (!windowsDir.empty() && !cancel.load())
        ScanDirForTempFiles(fs::path(windowsDir) / L"Temp", config, progress, cancel, items,
                            snapshot);

    // AppData\Local\Temp de cada usuário. Perfis de outros usuários exigem
    // privilégio de administrador para leitura; sem elevação, o iterador
    // simplesmente pula essas pastas (skip_permission_denied) em vez de falhar.
    for (const auto& profileDir : util::EnumerateUserProfileDirs()) {
        if (cancel.load()) break;
        ScanDirForTempFiles(fs::path(profileDir) / L"AppData" / L"Local" / L"Temp", config,
                            progress, cancel, items, snapshot);
    }

    snapshot.fractionComplete = 1.0f;
    progress.Update(snapshot);
    return items;
}

} // namespace storagecleaner
