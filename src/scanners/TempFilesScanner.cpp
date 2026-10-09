#include "storagecleaner/PathExclusion.h"
#include "storagecleaner/Scanner.h"
#include "storagecleaner/Utils.h"

#include <windows.h>
#include <shlobj.h>

#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace storagecleaner {

namespace {

void ScanDirForTempFiles(const fs::path& dir, const Config& config,
                         ProgressChannel& progress, std::atomic<bool>& cancel,
                         std::vector<ScanItem>& out, ProgressSnapshot& snapshot) {
    util::ForEachFileRecursive(dir.wstring(), cancel, [&](const std::wstring& path,
                                                          std::uint64_t size) {
        if (size < config.minFileSizeBytes) return;
        // Sem esse filtro de idade, um temporário criado há segundos por um
        // processo em execução (instalador extraindo, download em andamento,
        // autosave) poderia ser marcado para exclusão só por já ultrapassar o
        // tamanho mínimo.
        if (!util::IsOlderThanHours(path, config.tempFilesMinAgeHours)) return;

        out.push_back(ScanItem{path, size, Category::TempFiles, L"", false});

        snapshot.itemsProcessed++;
        snapshot.bytesFoundSoFar += size;
        snapshot.currentItem = path;
        if (snapshot.itemsProcessed % 32 == 0) progress.Update(snapshot);
    });
}

} // namespace

std::vector<ScanItem> ScanTempFiles(const Config& config, ProgressChannel& progress,
                                     std::atomic<bool>& cancel) {
    std::vector<ScanItem> items;
    ProgressSnapshot snapshot;
    snapshot.phase = Phase::ScanningTemp;
    progress.Update(snapshot);

    // O %TEMP% do usuário atual normalmente é o mesmo AppData\Local\Temp do
    // laço de perfis abaixo (às vezes em forma 8.3): sem comparar a forma
    // canônica, cada arquivo entrava duas vezes e o total saía dobrado.
    std::set<std::wstring> scannedRoots;
    auto scanRootOnce = [&](const fs::path& dir) {
        if (cancel.load()) return;
        std::wstring canonical = util::CanonicalPath(dir.wstring());
        if (!scannedRoots.insert(NormalizePathForCompare(canonical)).second) return;
        ScanDirForTempFiles(canonical, config, progress, cancel, items, snapshot);
    };

    // %TEMP% do usuário atual.
    if (wchar_t tempPath[MAX_PATH]; ::GetTempPathW(MAX_PATH, tempPath) > 0) scanRootOnce(tempPath);

    // C:\Windows\Temp — compartilhado por todos os usuários e por serviços.
    std::wstring windowsDir = util::KnownFolderPath(FOLDERID_Windows);
    if (!windowsDir.empty()) scanRootOnce(fs::path(windowsDir) / L"Temp");

    // AppData\Local\Temp de cada usuário. Perfis de outros usuários exigem
    // privilégio de administrador para leitura; sem elevação, o iterador
    // simplesmente pula essas pastas (skip_permission_denied) em vez de falhar.
    for (const auto& profileDir : util::EnumerateUserProfileDirs())
        scanRootOnce(fs::path(profileDir) / L"AppData" / L"Local" / L"Temp");

    snapshot.fractionComplete = 1.0f;
    progress.Update(snapshot);
    return items;
}

} // namespace storagecleaner
