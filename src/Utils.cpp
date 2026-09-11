#include "storagecleaner/Utils.h"

#include <windows.h>
#include <shlobj.h>
#include <wincrypt.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <sstream>
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

std::string HashFileSHA256(const std::wstring& path) {
    HANDLE hFile = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return {};

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    std::string result;

    if (!::CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES,
                                 CRYPT_VERIFYCONTEXT)) {
        ::CloseHandle(hFile);
        return {};
    }
    if (!::CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        ::CryptReleaseContext(hProv, 0);
        ::CloseHandle(hFile);
        return {};
    }

    // Lido em blocos (em vez de carregar o arquivo inteiro) para não estourar
    // memória ao gerar hash de arquivos grandes durante a busca por duplicados.
    constexpr DWORD kBufSize = 1 << 16;
    std::vector<BYTE> buffer(kBufSize);
    DWORD bytesRead = 0;
    bool ok = true;
    while (::ReadFile(hFile, buffer.data(), kBufSize, &bytesRead, nullptr) && bytesRead > 0) {
        if (!::CryptHashData(hHash, buffer.data(), bytesRead, 0)) {
            ok = false;
            break;
        }
    }

    if (ok) {
        BYTE hashValue[32];
        DWORD hashLen = sizeof(hashValue);
        if (::CryptGetHashParam(hHash, HP_HASHVAL, hashValue, &hashLen, 0)) {
            std::ostringstream oss;
            for (DWORD i = 0; i < hashLen; ++i) {
                char buf[3];
                std::snprintf(buf, sizeof(buf), "%02x", hashValue[i]);
                oss << buf;
            }
            result = oss.str();
        }
    }

    ::CryptDestroyHash(hHash);
    ::CryptReleaseContext(hProv, 0);
    ::CloseHandle(hFile);
    return result;
}

bool IsOlderThanDays(const std::wstring& path, int days) {
    std::error_code ec;
    auto ftime = fs::last_write_time(path, ec);
    if (ec) return false;

    auto now = fs::file_time_type::clock::now();
    auto age = std::chrono::duration_cast<std::chrono::hours>(now - ftime).count();
    return age >= static_cast<long long>(days) * 24;
}

std::uint64_t DirectorySize(const std::wstring& dir) {
    std::uint64_t total = 0;
    std::error_code ec;
    fs::recursive_directory_iterator it(
        dir, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    for (; it != end && !ec; it.increment(ec)) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc) && !fileEc) {
            auto size = it->file_size(fileEc);
            if (!fileEc) total += size;
        }
    }
    return total;
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
