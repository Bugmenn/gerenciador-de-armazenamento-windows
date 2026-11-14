#include "storagecleaner/OrphanClassifier.h"
#include "storagecleaner/PathExclusion.h"
#include "storagecleaner/Scanner.h"
#include "storagecleaner/Utils.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>

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

// Lê um valor de texto do registro (REG_SZ ou REG_EXPAND_SZ) com as
// variáveis de ambiente expandidas — alguns instaladores gravam
// "%LOCALAPPDATA%\..." até em REG_SZ. Vazio se não existir.
std::wstring ReadRegString(HKEY key, const wchar_t* valueName) {
    DWORD type = 0;
    DWORD size = 0;
    if (::RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0)
        return {};
    // +1 caractere para garantir o terminador nulo: RegQueryValueExW não o
    // garante quando o valor preenche o buffer exatamente (pitfall da API).
    std::wstring value(size / sizeof(wchar_t) + 1, L'\0');
    DWORD bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    if (::RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes) !=
        ERROR_SUCCESS)
        return {};
    value.resize(std::min<std::size_t>(bytes / sizeof(wchar_t), value.size()));
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    if (value.find(L'%') == std::wstring::npos) return value;
    DWORD needed = ::ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (needed == 0) return value;
    std::wstring expanded(needed, L'\0');
    if (::ExpandEnvironmentStringsW(value.c_str(), expanded.data(), needed) == 0) return value;
    expanded.resize(needed - 1);
    return expanded;
}

struct InstalledApps {
    std::vector<std::wstring> names;      // DisplayName
    std::vector<std::wstring> publishers; // Publisher, normalizado
    std::vector<std::wstring> paths;      // InstallLocation, DisplayIcon, UninstallString
};

InstalledApps CollectInstalledApps() {
    InstalledApps apps;

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
            LSTATUS enumStatus = ::RegEnumKeyExW(key, i, subKeyName, &subKeyLen, nullptr, nullptr,
                                                 nullptr, nullptr);
            if (enumStatus == ERROR_NO_MORE_ITEMS) break;
            if (enumStatus != ERROR_SUCCESS) {
                // ERROR_MORE_DATA (nome de subchave > 255 chars, raro mas
                // existe) não significa "acabou a lista" — só essa entrada não
                // coube no buffer. Dar break aqui pararia de enumerar TODO o
                // resto do hive, tratando apps genuinamente instalados (cujas
                // subchaves viriam depois) como não instalados. Pula só esta
                // entrada e continua.
                continue;
            }

            HKEY appKey = nullptr;
            if (::RegOpenKeyExW(key, subKeyName, 0, KEY_READ, &appKey) != ERROR_SUCCESS) continue;

            std::wstring displayName = ReadRegString(appKey, L"DisplayName");
            if (!displayName.empty()) apps.names.push_back(displayName);
            std::wstring publisher = Normalize(ReadRegString(appKey, L"Publisher"));
            if (!publisher.empty()) apps.publishers.push_back(publisher);
            for (const wchar_t* value : {L"InstallLocation", L"DisplayIcon", L"UninstallString"}) {
                std::wstring path = ReadRegString(appKey, value);
                if (!path.empty()) apps.paths.push_back(path);
            }
            ::RegCloseKey(appKey);
        }
        ::RegCloseKey(key);
    }

    return apps;
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

// Caminho do executável de cada processo em execução. Processos protegidos
// (sistema, antivírus) recusam a consulta e simplesmente ficam de fora.
std::vector<std::wstring> CollectRunningProcessPaths() {
    std::vector<std::wstring> paths;
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return paths;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = ::Process32FirstW(snapshot, &entry); ok; ok = ::Process32NextW(snapshot, &entry)) {
        HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process) continue;
        wchar_t image[MAX_PATH * 2];
        DWORD len = MAX_PATH * 2;
        if (::QueryFullProcessImageNameW(process, 0, image, &len)) paths.emplace_back(image, len);
        ::CloseHandle(process);
    }
    ::CloseHandle(snapshot);
    return paths;
}

// Linhas de comando que o Windows roda no logon (Run/RunOnce).
std::vector<std::wstring> CollectStartupCommands() {
    std::vector<std::wstring> commands;
    const std::pair<HKEY, const wchar_t*> keys[] = {
        {HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"},
        {HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce"},
        {HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"},
        {HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce"},
        {HKEY_LOCAL_MACHINE, L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run"},
    };
    for (const auto& [hive, subKey] : keys) {
        HKEY key = nullptr;
        if (::RegOpenKeyExW(hive, subKey, 0, KEY_READ, &key) != ERROR_SUCCESS) continue;
        for (DWORD i = 0;; ++i) {
            wchar_t valueName[512];
            DWORD nameLen = 512;
            LSTATUS status = ::RegEnumValueW(key, i, valueName, &nameLen, nullptr, nullptr, nullptr, nullptr);
            if (status == ERROR_NO_MORE_ITEMS) break;
            if (status != ERROR_SUCCESS) continue;
            std::wstring command = ReadRegString(key, valueName);
            if (!command.empty()) commands.push_back(command);
        }
        ::RegCloseKey(key);
    }
    return commands;
}

// Inicializa COM na thread atual pelo tempo de vida do objeto. A varredura
// roda numa worker thread do ScanEngine, que não inicializa COM por conta
// própria; CoInitializeEx é contado por referência.
class ComScope {
public:
    ComScope() : hr_(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~ComScope() {
        if (SUCCEEDED(hr_)) ::CoUninitialize();
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    HRESULT hr_;
};

// Destino de cada atalho do Menu Iniciar (que já contém a pasta Inicializar)
// e da Área de Trabalho, do usuário e de todos. Só lê o caminho gravado no
// .lnk — sem IShellLink::Resolve, que pode procurar o arquivo pelo disco ou
// abrir UI.
std::vector<std::wstring> CollectShortcutTargets(std::atomic<bool>& cancel) {
    std::vector<std::wstring> targets;
    ComScope com;

    IShellLinkW* link = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
        return targets;
    IPersistFile* file = nullptr;
    if (FAILED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
        link->Release();
        return targets;
    }

    auto readShortcut = [&](const std::wstring& path) {
        std::wstring ext = fs::path(path).extension().wstring();
        for (auto& ch : ext) ch = static_cast<wchar_t>(std::towlower(ch));
        if (ext != L".lnk" || FAILED(file->Load(path.c_str(), STGM_READ))) return;
        wchar_t raw[MAX_PATH];
        if (link->GetPath(raw, MAX_PATH, nullptr, SLGP_RAWPATH) != S_OK || raw[0] == L'\0') return;
        wchar_t expanded[MAX_PATH * 2];
        DWORD len = ::ExpandEnvironmentStringsW(raw, expanded, MAX_PATH * 2);
        targets.emplace_back((len > 0 && len <= MAX_PATH * 2) ? expanded : raw);
    };

    // Menu Iniciar: atalhos ficam em subpastas por fabricante — recursivo.
    for (const GUID& folder : {FOLDERID_Programs, FOLDERID_CommonPrograms}) {
        std::wstring root = util::KnownFolderPath(folder);
        if (!root.empty())
            util::ForEachFileRecursive(root, cancel, [&](const std::wstring& path, std::uint64_t) { readShortcut(path); });
    }
    // Área de Trabalho: só o primeiro nível. Ela costuma guardar muita coisa
    // (projetos, downloads — nesta máquina, dezenas de milhares de arquivos)
    // e atalho em subpasta dela é raro.
    for (const GUID& folder : {FOLDERID_Desktop, FOLDERID_PublicDesktop}) {
        std::wstring root = util::KnownFolderPath(folder);
        if (root.empty()) continue;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
            if (cancel.load()) break;
            std::error_code entryEc;
            if (entry.is_regular_file(entryEc) && !entryEc) readShortcut(entry.path().wstring());
        }
    }

    file->Release();
    link->Release();
    return targets;
}

bool RegistryKeyExists(HKEY hive, const std::wstring& subKey) {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(hive, subKey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    ::RegCloseKey(key);
    return true;
}

// `normalizedTexts` e `normalizedFolder` já passados por NormalizePathForCompare.
bool AnyMentions(const std::vector<std::wstring>& normalizedTexts, const std::wstring& normalizedFolder) {
    return std::any_of(normalizedTexts.begin(), normalizedTexts.end(), [&](const std::wstring& text) {
        return MentionsNormalizedFolder(text, normalizedFolder);
    });
}

// Normaliza uma vez, no início da varredura: cada lista é comparada contra
// todas as pastas candidatas.
std::vector<std::wstring> NormalizeAll(const std::vector<std::wstring>& texts) {
    std::vector<std::wstring> out;
    out.reserve(texts.size());
    for (const auto& text : texts) out.push_back(NormalizePathForCompare(text));
    return out;
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

// Tudo o que é coletado uma vez por varredura e consultado para cada pasta.
// As listas de caminhos/comandos já vêm normalizadas (NormalizeAll).
struct ScanContext {
    std::unordered_set<std::wstring> exactNames;
    std::vector<std::wstring> normalizedKnownNames;
    std::vector<std::wstring> publishers;
    std::vector<std::wstring> installedPaths;
    std::vector<std::wstring> processPaths;
    std::vector<std::wstring> startupCommands;
    std::vector<std::wstring> shortcutTargets;
    int inactivityDays = 0;
};

OrphanEvidence GatherEvidence(const ScanContext& ctx, const fs::path& folder,
                              const std::wstring& normalizedName, bool isCurrentUser,
                              const util::DirectoryStats& stats) {
    std::wstring folderPath = NormalizePathForCompare(folder.wstring());
    std::wstring folderName = folder.filename().wstring();

    OrphanEvidence e;
    e.runningProcess = AnyMentions(ctx.processPaths, folderPath);
    e.startupEntry = AnyMentions(ctx.startupCommands, folderPath);
    e.shortcutTarget = AnyMentions(ctx.shortcutTargets, folderPath);
    e.uninstallPath = AnyMentions(ctx.installedPaths, folderPath);
    e.publisherMatch = std::any_of(ctx.publishers.begin(), ctx.publishers.end(), [&](const std::wstring& p) {
        return ContainsAsSubstring(p, normalizedName) || ContainsAsSubstring(normalizedName, p);
    });
    e.hasExecutables = stats.executableCount > 0;
    // HKCU só descreve o usuário que roda o app; para pastas de outros
    // perfis fica só a checagem de HKLM.
    e.softwareRegistryKey =
        (isCurrentUser && RegistryKeyExists(HKEY_CURRENT_USER, L"Software\\" + folderName)) ||
        RegistryKeyExists(HKEY_LOCAL_MACHINE, L"Software\\" + folderName) ||
        RegistryKeyExists(HKEY_LOCAL_MACHINE, L"Software\\WOW6432Node\\" + folderName);
    e.daysSinceLastWrite = stats.daysSinceNewestWrite;
    e.otherProfile = !isCurrentUser;
    return e;
}

void ScanAppDataRoot(const fs::path& root, const ScanContext& ctx, bool isCurrentUser,
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

        if (MatchesAnyInstalledApp(normalized, ctx.exactNames, ctx.normalizedKnownNames)) continue;

        // Uma única varredura recursiva decide tamanho, atividade recente e
        // os sinais de arquivo (.exe, escrita mais recente) — o mtime só da
        // pasta de topo não reflete escritas em subpastas no NTFS (ver
        // ComputeDirectoryStats). Pasta com escrita nos últimos
        // `inactivityDays` dias nem é candidata.
        util::DirectoryStats stats =
            util::ComputeDirectoryStats(entry.path().wstring(), ctx.inactivityDays, cancel);
        if (stats.totalBytes == 0 || stats.hasRecentActivity) continue;

        OrphanVerdict verdict =
            ClassifyOrphan(GatherEvidence(ctx, entry.path(), normalized, isCurrentUser, stats));
        ScanItem item{entry.path().wstring(), stats.totalBytes, Category::OrphanedApps, L"", true};
        item.existenceChance = verdict.chance;
        item.evidence = JoinReasons(verdict.reasons);
        out.push_back(std::move(item));

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

    ScanContext ctx;
    InstalledApps installed = CollectInstalledApps();
    for (const auto& name : installed.names) ctx.normalizedKnownNames.push_back(Normalize(name));
    for (const auto& name : CollectProgramFilesFolderNames())
        ctx.normalizedKnownNames.push_back(Normalize(name));
    ctx.exactNames.insert(ctx.normalizedKnownNames.begin(), ctx.normalizedKnownNames.end());
    ctx.publishers = std::move(installed.publishers);
    ctx.installedPaths = NormalizeAll(installed.paths);
    ctx.processPaths = NormalizeAll(CollectRunningProcessPaths());
    ctx.startupCommands = NormalizeAll(CollectStartupCommands());
    ctx.shortcutTargets = NormalizeAll(CollectShortcutTargets(cancel));
    ctx.inactivityDays = config.orphanedAppsInactivityDays;

    const std::wstring currentProfile = NormalizePathForCompare(util::KnownFolderPath(FOLDERID_Profile));
    for (const auto& profileDir : util::EnumerateUserProfileDirs()) {
        if (cancel.load()) break;
        bool isCurrentUser = NormalizePathForCompare(profileDir) == currentProfile;
        fs::path appData = fs::path(profileDir) / L"AppData";
        for (const wchar_t* sub : {L"Roaming", L"Local", L"LocalLow"})
            ScanAppDataRoot(appData / sub, ctx, isCurrentUser, items, snapshot, progress, cancel);
    }

    snapshot.fractionComplete = 1.0f;
    progress.Update(snapshot);
    return items;
}

} // namespace storagecleaner
