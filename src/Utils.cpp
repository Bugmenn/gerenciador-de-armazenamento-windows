#include "storagecleaner/Utils.h"

#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <winioctl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace storagecleaner::util {

std::string FormatSize(std::uint64_t bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double value = static_cast<double>(bytes);
    int unitIndex = 0;
    while (value >= 1024.0 && unitIndex < 5) {
        value /= 1024.0;
        ++unitIndex;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f %s", value, units[unitIndex]);
    return std::string(buf);
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int sizeNeeded = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                           static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string result(sizeNeeded, 0);
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                          result.data(), sizeNeeded, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int sizeNeeded = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring result(sizeNeeded, 0);
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                         result.data(), sizeNeeded);
    return result;
}

namespace {

// Fecha o HANDLE ao sair do escopo (todos os retornos antecipados do hash).
struct FileHandle {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~FileHandle() {
        if (h != INVALID_HANDLE_VALUE) ::CloseHandle(h);
    }
};

constexpr std::size_t kHashBufferSize = 1 << 20; // 1 MB por leitura

} // namespace

FileHasher::FileHasher() : buffer_(kHashBufferSize) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr,
                                                      BCRYPT_HASH_REUSABLE_FLAG)))
        return;
    algorithm_ = alg;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCRYPT_SUCCESS(::BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, BCRYPT_HASH_REUSABLE_FLAG)))
        hash_ = hash;
}

FileHasher::~FileHasher() {
    if (hash_) ::BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(hash_));
    if (algorithm_) ::BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(algorithm_), 0);
}

std::string FileHasher::Finish() {
    unsigned char digest[32];
    // Com BCRYPT_HASH_REUSABLE_FLAG, FinishHash também zera o objeto para o
    // próximo arquivo.
    if (!BCRYPT_SUCCESS(::BCryptFinishHash(static_cast<BCRYPT_HASH_HANDLE>(hash_), digest, sizeof(digest), 0)))
        return {};
    static const char kHex[] = "0123456789abcdef";
    std::string hex(64, '0');
    for (int i = 0; i < 32; ++i) {
        hex[i * 2] = kHex[digest[i] >> 4];
        hex[i * 2 + 1] = kHex[digest[i] & 0xF];
    }
    return hex;
}

std::string FileHasher::Full(const std::wstring& path) {
    if (!hash_) return {};
    FileHandle file{::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (file.h == INVALID_HANDLE_VALUE) return {};
    for (;;) {
        DWORD bytesRead = 0;
        // Falha de leitura no meio não pode virar o hash do pedaço lido.
        if (!::ReadFile(file.h, buffer_.data(), static_cast<DWORD>(buffer_.size()), &bytesRead, nullptr) ||
            (bytesRead > 0 &&
             !BCRYPT_SUCCESS(::BCryptHashData(static_cast<BCRYPT_HASH_HANDLE>(hash_), buffer_.data(), bytesRead, 0)))) {
            Finish(); // zera o objeto reutilizável
            return {};
        }
        if (bytesRead == 0) break;
    }
    return Finish();
}

std::string FileHasher::Edges(const std::wstring& path, std::uint64_t sizeBytes, std::uint32_t edgeBytes) {
    if (!hash_) return {};
    if (sizeBytes <= 2ull * edgeBytes) return Full(path);
    FileHandle file{::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (file.h == INVALID_HANDLE_VALUE) return {};
    auto readAt = [&](std::uint64_t offset) {
        LARGE_INTEGER pos;
        pos.QuadPart = static_cast<LONGLONG>(offset);
        DWORD bytesRead = 0;
        return ::SetFilePointerEx(file.h, pos, nullptr, FILE_BEGIN) &&
               ::ReadFile(file.h, buffer_.data(), edgeBytes, &bytesRead, nullptr) && bytesRead == edgeBytes &&
               BCRYPT_SUCCESS(::BCryptHashData(static_cast<BCRYPT_HASH_HANDLE>(hash_), buffer_.data(), bytesRead, 0));
    };
    if (!readAt(0) || !readAt(sizeBytes - edgeBytes)) {
        Finish();
        return {};
    }
    return Finish();
}

std::optional<bool> IsSolidStateDrive(wchar_t driveLetter) {
    wchar_t device[] = L"\\\\.\\X:";
    device[4] = driveLetter;
    // Acesso 0: só consulta propriedades do dispositivo, não exige Administrador.
    FileHandle volume{::CreateFileW(device, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr)};
    if (volume.h == INVALID_HANDLE_VALUE) return std::nullopt;
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    query.QueryType = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR penalty{};
    DWORD returned = 0;
    if (!::DeviceIoControl(volume.h, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &penalty,
                           sizeof(penalty), &returned, nullptr) ||
        returned < sizeof(penalty))
        return std::nullopt;
    return !penalty.IncursSeekPenalty;
}

int ReadParallelism(wchar_t driveLetter) {
    if (!IsSolidStateDrive(driveLetter).value_or(false)) return 1;
    unsigned hw = std::thread::hardware_concurrency();
    return static_cast<int>(std::clamp<unsigned>(hw, 1, 4));
}

std::int64_t NowFileTimeTicks() {
    FILETIME now;
    ::GetSystemTimeAsFileTime(&now);
    return static_cast<std::int64_t>((static_cast<std::uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime);
}

bool IsOlderThanHours(const std::wstring& path, int hours) {
    std::error_code ec;
    auto ftime = fs::last_write_time(path, ec);
    if (ec) return false;

    auto now = fs::file_time_type::clock::now();
    auto age = std::chrono::duration_cast<std::chrono::hours>(now - ftime).count();
    return age >= static_cast<long long>(hours);
}

bool IsOlderThanDays(const std::wstring& path, int days) {
    return IsOlderThanHours(path, days * 24);
}

std::uint64_t DirectorySize(const std::wstring& dir, std::atomic<bool>& cancel) {
    std::uint64_t total = 0;
    ForEachFileRecursive(dir, cancel, [&total](const FileEntry& entry) { total += entry.sizeBytes; });
    return total;
}

namespace {

bool EndsWithExe(const std::wstring& path) {
    if (path.size() < 4) return false;
    const wchar_t* tail = path.c_str() + path.size() - 4;
    return tail[0] == L'.' && (tail[1] | 0x20) == L'e' && (tail[2] | 0x20) == L'x' && (tail[3] | 0x20) == L'e';
}

} // namespace

void ForEachFileRecursive(const std::wstring& dir, std::atomic<bool>& cancel, const FileVisitor& visitor) {
    std::vector<std::wstring> pending{dir};
    FileEntry entry;
    WIN32_FIND_DATAW data;
    while (!pending.empty()) {
        if (cancel.load()) return;
        std::wstring current = std::move(pending.back());
        pending.pop_back();
        while (current.size() > 1 && (current.back() == L'\\' || current.back() == L'/')) current.pop_back();

        // Caminho longo: o prefixo \\?\ libera o limite de MAX_PATH na busca;
        // os caminhos entregues ao visitante continuam na forma normal.
        std::wstring pattern = current + L"\\*";
        if (pattern.size() >= MAX_PATH && current.rfind(L"\\\\", 0) != 0) pattern = L"\\\\?\\" + pattern;

        HANDLE find = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                         FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE) continue; // sem permissão, sumiu ou não é pasta
        do {
            if (cancel.load()) break;
            const wchar_t* name = data.cFileName;
            if (name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'))) continue;
            std::wstring full = current + L'\\' + name;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                // Só symlink e junction ficam de fora (laço, sair da árvore) —
                // como o recursive_directory_iterator de antes. Outras pastas
                // reparse point, como as do OneDrive, são pastas de verdade.
                const bool link = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                                  (data.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT ||
                                   data.dwReserved0 == IO_REPARSE_TAG_SYMLINK);
                if (!link) pending.push_back(std::move(full));
                continue;
            }
            entry.path = std::move(full);
            entry.sizeBytes = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            entry.lastWriteTicks = static_cast<std::int64_t>(
                (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                data.ftLastWriteTime.dwLowDateTime);
            entry.attributes = data.dwFileAttributes;
            visitor(entry);
        } while (::FindNextFileW(find, &data));
        ::FindClose(find);
    }
}

DirectoryStats ComputeDirectoryStats(const std::wstring& dir, int thresholdDays, std::atomic<bool>& cancel,
                                     bool readDatesFromFiles) {
    DirectoryStats stats;
    const std::int64_t now = NowFileTimeTicks();
    const std::int64_t threshold = now - static_cast<std::int64_t>(thresholdDays) * 24 * kTicksPerHour;
    std::int64_t newest = 0;

    ForEachFileRecursive(dir, cancel, [&](const FileEntry& entry) {
        stats.totalBytes += entry.sizeBytes;
        if (EndsWithExe(entry.path)) ++stats.executableCount;
        std::int64_t written = entry.lastWriteTicks;
        if (readDatesFromFiles) {
            // file_clock do MSVC = FILETIME (100 ns desde 1601), mesma escala
            // de lastWriteTicks.
            std::error_code ec;
            auto real = fs::last_write_time(entry.path, ec);
            if (!ec) written = real.time_since_epoch().count();
        }
        if (written >= threshold) stats.hasRecentActivity = true;
        if (written > newest) newest = written;
    });
    if (newest > 0) stats.daysSinceNewestWrite = static_cast<int>(AgeHoursAt(newest, now) / 24);
    return stats;
}

CommandResult RunCommandCaptureOutput(const std::wstring& commandLine) {
    CommandResult result;
    constexpr DWORD kHangTimeoutMs = 60000; // vssadmin nunca deveria levar perto disso

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!::CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        result.output = "Falha ao criar pipe";
        return result;
    }
    ::SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;

    PROCESS_INFORMATION pi{};
    // CreateProcessW pode modificar o buffer de linha de comando; usamos uma
    // cópia mutável dedicada.
    std::vector<wchar_t> cmdBuf(commandLine.begin(), commandLine.end());
    cmdBuf.push_back(L'\0');

    BOOL created = ::CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                                     CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    ::CloseHandle(writePipe);

    if (!created) {
        ::CloseHandle(readPipe);
        result.output = "Falha ao iniciar processo";
        return result;
    }

    // Watchdog: se o processo travar (ex.: servico VSS ocupado), forca o
    // encerramento apos o timeout fixo abaixo em vez de bloquear esta thread
    // para sempre. Isso evita um hang permanente, mas NAO torna o
    // cancelamento do usuario instantaneo: se o cancelamento acontecer
    // durante a janela do timeout, ainda esperamos ate kHangTimeoutMs (ou o
    // termino natural do processo) antes de retomar.
    HANDLE processHandle = pi.hProcess;
    std::thread watchdog([processHandle, kHangTimeoutMs]() {
        if (::WaitForSingleObject(processHandle, kHangTimeoutMs) == WAIT_TIMEOUT)
            ::TerminateProcess(processHandle, 1);
    });

    std::string output;
    char buf[4096];
    DWORD bytesRead = 0;
    while (::ReadFile(readPipe, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
        output.append(buf, bytesRead);
    }
    ::CloseHandle(readPipe);

    ::WaitForSingleObject(pi.hProcess, INFINITE);
    watchdog.join();
    DWORD exitCode = 0;
    ::GetExitCodeProcess(pi.hProcess, &exitCode);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);

    result.exitCode = static_cast<int>(exitCode);
    result.output = std::move(output);
    return result;
}

std::wstring KnownFolderPath(const GUID& folderId) {
    PWSTR rawPath = nullptr;
    std::wstring result;
    if (SUCCEEDED(::SHGetKnownFolderPath(folderId, 0, nullptr, &rawPath))) {
        result = rawPath;
        ::CoTaskMemFree(rawPath);
    }
    return result;
}

std::wstring SystemToolPath(const wchar_t* toolName) {
    wchar_t sysDir[MAX_PATH];
    UINT len = ::GetSystemDirectoryW(sysDir, MAX_PATH);
    // Falha aqui e' improvavel, mas jamais cair para o nome sem caminho: isso
    // reabriria a busca por PATH que esta funcao existe para evitar. O
    // chamador deve tratar string vazia como falha e nao executar o comando.
    if (len == 0 || len >= MAX_PATH) return L"";

    // Aspas para o caso (improvavel, mas nao impossivel) de o diretorio do
    // sistema conter espacos — CreateProcessW trata o primeiro token da
    // linha de comando como o executavel, entao precisa estar bem delimitado.
    std::wstring path = L"\"" + std::wstring(sysDir) + L"\\" + toolName + L".exe\"";
    return path;
}

std::wstring CanonicalPath(const std::wstring& path) {
    if (path.empty()) return path;
    DWORD needed = ::GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0) return path;
    std::wstring full(needed, L'\0');
    DWORD written = ::GetFullPathNameW(path.c_str(), needed, full.data(), nullptr);
    if (written == 0 || written >= needed) return path;
    full.resize(written);

    needed = ::GetLongPathNameW(full.c_str(), nullptr, 0);
    if (needed == 0) return full; // não existe mais / sem acesso
    std::wstring longPath(needed, L'\0');
    written = ::GetLongPathNameW(full.c_str(), longPath.data(), needed);
    if (written == 0 || written >= needed) return full;
    longPath.resize(written);
    return longPath;
}

std::vector<DriveSpace> EnumerateDrives() {
    std::vector<DriveSpace> drives;
    // Leitor de cartão sem mídia pode abrir a caixa "Não há disco na
    // unidade" e travar esta worker até alguém clicar.
    DWORD oldErrorMode = 0;
    ::SetThreadErrorMode(SEM_FAILCRITICALERRORS, &oldErrorMode);
    DWORD mask = ::GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        wchar_t root[] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', L'\0'};
        UINT type = ::GetDriveTypeW(root);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) continue;

        ULARGE_INTEGER freeToCaller{}, total{}, totalFree{};
        // Falha = leitor de cartão sem mídia etc.; só não lista a unidade.
        if (!::GetDiskFreeSpaceExW(root, &freeToCaller, &total, &totalFree)) continue;
        if (total.QuadPart == 0) continue;

        drives.push_back(DriveSpace{root, total.QuadPart, totalFree.QuadPart,
                                    type == DRIVE_REMOVABLE});
    }
    ::SetThreadErrorMode(oldErrorMode, nullptr);
    return drives;
}

std::vector<std::wstring> EnumerateUserProfileDirs() {
    std::vector<std::wstring> dirs;
    std::wstring usersRoot = KnownFolderPath(FOLDERID_UserProfiles);
    if (usersRoot.empty()) return dirs;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(
             usersRoot, fs::directory_options::skip_permission_denied, ec)) {
        std::error_code entryEc;
        if (entry.is_directory(entryEc) && !entryEc) dirs.push_back(entry.path().wstring());
    }
    return dirs;
}

} // namespace storagecleaner::util
