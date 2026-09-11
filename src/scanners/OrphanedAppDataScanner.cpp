#include "storagecleaner/Scanner.h"
#include "storagecleaner/Utils.h"

#include <windows.h>
#include <knownfolders.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;

namespace storagecleaner {

namespace {

// Normaliza para comparação tolerante: minúsculas, sem espaços/hífens/pontos.
// "Adobe Photoshop CC" e "AdobePhotoshopCC" devem casar com a mesma pasta de
// AppData, por isso o match é por substring depois dessa normalização em vez
// de igualdade exata.
std::wstring Normalize(std::wstring s) {
    std::wstring out;
    for (wchar_t c : s) {
        if (std::iswalnum(c)) out.push_back(std::towlower(c));
    }
    return out;
}

// Nomes de pastas de infraestrutura do próprio Windows/runtime que não
// correspondem a "um aplicativo" e nunca devem ser candidatas a órfãs, mesmo
// que não apareçam na lista de programas instalados.
bool IsKnownInfrastructureFolder(const std::wstring& normalizedName) {
    static const std::unordered_set<std::wstring> kSkip = {
        L"microsoft",  L"packages",       L"connecteddevicesplatform",
        L"temp",       L"packagecache",   L"crashdumps",
        L"comms",      L"programs",       L"low",
    };
    return kSkip.count(normalizedName) > 0;
}

std::vector<std::wstring> CollectInstalledAppNames() {
    std::vector<std::wstring> names;

    struct RegRoot {
        HKEY hive;
        const wchar_t* subKey;
    };
    const RegRoot roots[] = {
        {HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
        {HKEY_LOCAL_MACHINE,
         L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
        {HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
    };

    for (const auto& root : roots) {
        HKEY key = nullptr;
        if (::RegOpenKeyExW(root.hive, root.subKey, 0, KEY_READ, &key) != ERROR_SUCCESS) continue;

        for (DWORD i = 0;; ++i) {
            wchar_t subKeyName[256];
            DWORD subKeyLen = 256;
            if (::RegEnumKeyExW(key, i, subKeyName, &subKeyLen, nullptr, nullptr, nullptr,
                                 nullptr) != ERROR_SUCCESS)
                break;

            HKEY appKey = nullptr;
            if (::RegOpenKeyExW(key, subKeyName, 0, KEY_READ, &appKey) != ERROR_SUCCESS) continue;

            wchar_t displayName[512];
            DWORD size = sizeof(displayName);
            DWORD type = 0;
            if (::RegQueryValueExW(appKey, L"DisplayName", nullptr, &type,
                                    reinterpret_cast<BYTE*>(displayName), &size) == ERROR_SUCCESS &&
                type == REG_SZ) {
                names.emplace_back(displayName);
            }
            ::RegCloseKey(appKey);
        }
        ::RegCloseKey(key);
    }

    return names;
}

std::vector<std::wstring> CollectProgramFilesFolderNames() {
    std::vector<std::wstring> names;
    for (const GUID& folder : {FOLDERID_ProgramFiles, FOLDERID_ProgramFilesX86}) {
        std::wstring path = util::KnownFolderPath(folder);
        if (path.empty()) continue;

        std::error_code ec;
        for (const auto& entry :
             fs::directory_iterator(path, fs::directory_options::skip_permission_denied, ec)) {
            std::error_code entryEc;
            if (entry.is_directory(entryEc) && !entryEc)
                names.push_back(entry.path().filename().wstring());
        }
    }
    return names;
}

// Abaixo desse tamanho um nome normalizado (ex.: "go", "ai") é curto demais
// para servir como lado "contido" do match por substring — aceitar geraria
// falsos positivos com pastas completamente não relacionadas.
constexpr std::size_t kMinSubstringMatchLen = 4;

// true se `needle` aparece dentro de `haystack`. Igualdade exata sempre
// conta (mesmo para nomes curtos, ex.: "vlc" == "vlc") — o tamanho mínimo só
// se aplica a uma contenção estrita, para não bloquear o caso mais comum
// (apps cujo nome de pasta é idêntico ao nome normalizado) junto com os
// falsos positivos de substring curta que ele existe para evitar.
bool ContainsAsSubstring(const std::wstring& haystack, const std::wstring& needle) {
    if (haystack == needle) return true;
    return needle.size() >= kMinSubstringMatchLen &&
           haystack.find(needle) != std::wstring::npos;
}

bool MatchesAnyInstalledApp(const std::wstring& normalizedFolderName,
                            const std::unordered_set<std::wstring>& exactNames,
                            const std::vector<std::wstring>& normalizedKnownNames) {
    // Caminho comum, O(1): a maioria das pastas de AppData tem nome idêntico
    // (após normalização) ao app instalado. Só cai no laço de substring
    // (mais caro, O(n) comparações de string) quando não há match exato.
    if (exactNames.count(normalizedFolderName) > 0) return true;

    for (const auto& known : normalizedKnownNames) {
        if (known.empty()) continue;
        // Substring nos dois sentidos: "steam" deve casar com "valve steam" e
        // vice-versa, já que nomes de pastas raramente são idênticos ao
        // DisplayName completo do instalador.
        if (ContainsAsSubstring(known, normalizedFolderName) ||
            ContainsAsSubstring(normalizedFolderName, known)) {
            return true;
        }
    }
    return false;
}

void ScanAppDataRoot(const fs::path& root, const std::unordered_set<std::wstring>& exactNames,
                    const std::vector<std::wstring>& normalizedKnownNames, int inactivityDays,
                    std::vector<ScanItem>& out, ProgressSnapshot& snapshot,
                    ProgressChannel& progress, std::atomic<bool>& cancel) {
    std::error_code ec;
    if (!fs::is_directory(root, ec) || ec) return;

    for (const auto& entry :
         fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
        if (cancel.load()) return;
        std::error_code entryEc;
        if (!entry.is_directory(entryEc) || entryEc) continue;

        std::wstring folderName = entry.path().filename().wstring();
        std::wstring normalized = Normalize(folderName);
        if (normalized.empty() || IsKnownInfrastructureFolder(normalized)) continue;

        if (MatchesAnyInstalledApp(normalized, exactNames, normalizedKnownNames)) continue;

        // Uma única varredura recursiva decide tamanho E atividade recente
        // (em vez de checar o mtime só da pasta de topo, que no NTFS não
        // reflete escritas em subpastas — ver ComputeDirectoryStats). Só é
        // candidata a "orfã" se, além de não casar com nenhum app conhecido,
        // nenhum arquivo da árvore foi escrito nos últimos `inactivityDays`
        // dias — evita marcar pastas de apps portáteis/raramente abertos que
        // ainda são válidos mas não aparecem no registro de programas
        // instalados.
        util::DirectoryStats stats = util::ComputeDirectoryStats(entry.path().wstring(), inactivityDays);
        if (stats.totalBytes == 0 || stats.hasRecentActivity) continue;

        out.push_back(
            ScanItem{entry.path().wstring(), stats.totalBytes, Category::OrphanedApps, L"", true});
        snapshot.itemsProcessed++;
        snapshot.bytesFoundSoFar += stats.totalBytes;
        snapshot.currentItem = entry.path().wstring();
        progress.Update(snapshot);
    }
}

} // namespace

std::vector<ScanItem> ScanOrphanedAppData(const Config& config, ProgressChannel& progress,
                                          std::atomic<bool>& cancel) {
    std::vector<ScanItem> items;
    ProgressSnapshot snapshot;
    snapshot.phase = Phase::ScanningOrphanedApps;
    progress.Update(snapshot);

    std::vector<std::wstring> normalizedKnownNames;
    for (const auto& name : CollectInstalledAppNames()) normalizedKnownNames.push_back(Normalize(name));
    for (const auto& name : CollectProgramFilesFolderNames())
        normalizedKnownNames.push_back(Normalize(name));
    std::unordered_set<std::wstring> exactNames(normalizedKnownNames.begin(),
                                                normalizedKnownNames.end());

    for (const auto& profileDir : util::EnumerateUserProfileDirs()) {
        if (cancel.load()) break;
        fs::path appData = fs::path(profileDir) / L"AppData";
        ScanAppDataRoot(appData / L"Roaming", exactNames, normalizedKnownNames,
                       config.orphanedAppsInactivityDays, items, snapshot, progress, cancel);
        ScanAppDataRoot(appData / L"Local", exactNames, normalizedKnownNames,
                       config.orphanedAppsInactivityDays, items, snapshot, progress, cancel);
        ScanAppDataRoot(appData / L"LocalLow", exactNames, normalizedKnownNames,
                       config.orphanedAppsInactivityDays, items, snapshot, progress, cancel);
    }

    snapshot.fractionComplete = 1.0f;
    progress.Update(snapshot);
    return items;
}

} // namespace storagecleaner
