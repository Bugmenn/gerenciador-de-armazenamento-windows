#include "storagecleaner/Scanner.h"
#include "storagecleaner/Utils.h"

#include <windows.h>
#include <shlobj.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace storagecleaner {

namespace {

bool StartsWithIgnoreCase(const std::wstring& s, const wchar_t* prefix) {
    std::size_t n = wcslen(prefix);
    return s.size() >= n && _wcsnicmp(s.c_str(), prefix, n) == 0;
}

// O Explorer mantém os .db de miniatura abertos enquanto roda; tentar
// mandá-los para a Lixeira só geraria falha na limpeza. Pedir acesso DELETE
// reproduz exatamente o que a exclusão vai precisar, e compartilhar tudo
// evita que esta checagem impeça o próprio Explorer de abrir o arquivo.
// Abrir o handle não altera nem remove nada.
bool IsLockedByAnotherProcess(const std::wstring& path) {
    HANDLE h = ::CreateFileW(path.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return ::GetLastError() == ERROR_SHARING_VIOLATION;
    ::CloseHandle(h);
    return false;
}

} // namespace

std::vector<ScanItem> ScanThumbnailCache(const Config& config, ProgressChannel& progress,
                                         std::atomic<bool>& cancel) {
    std::vector<ScanItem> items;
    ProgressSnapshot snapshot;
    snapshot.phase = Phase::ScanningThumbnails;
    progress.Update(snapshot);

    std::wstring localAppData = util::KnownFolderPath(FOLDERID_LocalAppData);
    if (localAppData.empty()) return items;
    fs::path explorerDir = fs::path(localAppData) / L"Microsoft" / L"Windows" / L"Explorer";

    std::error_code ec;
    for (fs::directory_iterator it(explorerDir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (cancel.load()) break;
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc)) continue;

        std::wstring name = it->path().filename().wstring();
        if (!StartsWithIgnoreCase(name, L"thumbcache_") && !StartsWithIgnoreCase(name, L"iconcache_"))
            continue;
        if (it->path().extension() != L".db") continue;

        std::uint64_t size = it->file_size(entryEc);
        if (entryEc || size < config.minFileSizeBytes) continue;
        std::wstring path = it->path().wstring();
        if (IsLockedByAnotherProcess(path)) continue;

        items.push_back(ScanItem{path, size, Category::ThumbnailCache, L"", false});
        snapshot.itemsProcessed++;
        snapshot.bytesFoundSoFar += size;
        snapshot.currentItem = path;
        progress.Update(snapshot);
    }

    snapshot.fractionComplete = 1.0f;
    progress.Update(snapshot);
    return items;
}

} // namespace storagecleaner
