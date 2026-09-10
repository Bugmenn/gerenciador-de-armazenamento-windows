#include "storagecleaner/Scanner.h"
#include "storagecleaner/Utils.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace storagecleaner {

namespace {

// Cada pasta de cache vira 1 ScanItem (não 1 por arquivo): são dezenas de
// milhares de arquivos pequenos, e o usuário só precisa decidir "limpar o
// cache do Chrome" ou não — listar arquivo a arquivo só poluiria a tabela de
// resultados sem agregar valor.
void AddCacheDirIfExists(const fs::path& dir, std::vector<ScanItem>& out,
                         ProgressSnapshot& snapshot, ProgressChannel& progress) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec) || ec) return;

    std::uint64_t size = util::DirectorySize(dir.wstring());
    if (size == 0) return;

    out.push_back(ScanItem{dir.wstring(), size, Category::BrowserCache, L"", true});
    snapshot.itemsProcessed++;
    snapshot.bytesFoundSoFar += size;
    snapshot.currentItem = dir.wstring();
    progress.Update(snapshot);
}

void ScanProfileCaches(const fs::path& profileDir, std::vector<ScanItem>& out,
                       ProgressSnapshot& snapshot, ProgressChannel& progress,
                       std::atomic<bool>& cancel) {
    fs::path appDataLocal = profileDir / L"AppData" / L"Local";

    // Chrome / Edge: uma pasta "User Data" com subpastas de perfil (Default,
    // "Profile 1", etc.), cada uma com sua própria pasta Cache.
    for (const wchar_t* chromiumApp : {L"Google\\Chrome", L"Microsoft\\Edge"}) {
        fs::path userData = appDataLocal / chromiumApp / L"User Data";
        std::error_code ec;
        if (!fs::is_directory(userData, ec) || ec) continue;

        for (const auto& entry : fs::directory_iterator(
                 userData, fs::directory_options::skip_permission_denied, ec)) {
            if (cancel.load()) return;
            std::error_code entryEc;
            if (!entry.is_directory(entryEc) || entryEc) continue;

            AddCacheDirIfExists(entry.path() / L"Cache", out, snapshot, progress);
            AddCacheDirIfExists(entry.path() / L"Code Cache", out, snapshot, progress);
        }
    }

    // Firefox: perfis em Profiles\<hash>.default*, cache em cache2.
    fs::path firefoxProfiles = appDataLocal / L"Mozilla" / L"Firefox" / L"Profiles";
    std::error_code ec;
    if (fs::is_directory(firefoxProfiles, ec) && !ec) {
        for (const auto& entry : fs::directory_iterator(
                 firefoxProfiles, fs::directory_options::skip_permission_denied, ec)) {
            if (cancel.load()) return;
            std::error_code entryEc;
            if (!entry.is_directory(entryEc) || entryEc) continue;
            AddCacheDirIfExists(entry.path() / L"cache2", out, snapshot, progress);
        }
    }
}

} // namespace

std::vector<ScanItem> ScanBrowserCache(const Config&, ProgressChannel& progress,
                                        std::atomic<bool>& cancel) {
    std::vector<ScanItem> items;
    ProgressSnapshot snapshot;
    snapshot.phase = Phase::ScanningBrowserCache;
    progress.Update(snapshot);

    for (const auto& profileDir : util::EnumerateUserProfileDirs()) {
        if (cancel.load()) break;
        ScanProfileCaches(profileDir, items, snapshot, progress, cancel);
    }

    snapshot.fractionComplete = 1.0f;
    progress.Update(snapshot);
    return items;
}

} // namespace storagecleaner
