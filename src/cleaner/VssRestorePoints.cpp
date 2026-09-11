#include "storagecleaner/VssRestorePoints.h"
#include "storagecleaner/Utils.h"

#include <windows.h>

#include <cwchar>
#include <sstream>

namespace storagecleaner {

bool IsRunningElevated() {
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return false;

    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    bool elevated = false;
    if (::GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
        elevated = elevation.TokenIsElevated != 0;

    ::CloseHandle(token);
    return elevated;
}

std::vector<ShadowCopyInfo> ListShadowCopies() {
    std::vector<ShadowCopyInfo> result;
    if (!IsRunningElevated()) return result;

    std::wstring vssadminPath = util::SystemToolPath(L"vssadmin");
    if (vssadminPath.empty()) return result; // nao arrisca resolucao via PATH

    util::CommandResult cmd =
        util::RunCommandCaptureOutput(vssadminPath + L" list shadows /for=C:");
    if (cmd.exitCode != 0) return result;

    // A saída do vssadmin é texto tabular simples e estável (não muda entre
    // execuções na mesma versão do Windows), então um parser linha a linha é
    // suficiente — não há necessidade de uma dependência de regex/XML.
    // O formato de data/hora exato varia por versão/localidade do Windows;
    // guardamos o texto bruto e deixamos a formatação para a UI.
    std::wstring wideOutput = util::Utf8ToWide(cmd.output);
    std::wistringstream stream(wideOutput);
    std::wstring line;
    std::wstring currentCreationTime;

    while (std::getline(stream, line)) {
        auto creationPos = line.find(L"creation time:");
        if (creationPos != std::wstring::npos) {
            currentCreationTime = line.substr(creationPos + wcslen(L"creation time:"));
            while (!currentCreationTime.empty() && currentCreationTime.front() == L' ')
                currentCreationTime.erase(currentCreationTime.begin());
            continue;
        }

        auto idLabelPos = line.find(L"Shadow Copy ID:");
        if (idLabelPos != std::wstring::npos) {
            auto braceStart = line.find(L'{', idLabelPos);
            auto braceEnd = line.find(L'}', idLabelPos);
            if (braceStart != std::wstring::npos && braceEnd != std::wstring::npos) {
                ShadowCopyInfo info;
                info.id = line.substr(braceStart, braceEnd - braceStart + 1);
                info.creationTime = currentCreationTime;
                result.push_back(std::move(info));
            }
        }
    }

    return result;
}

bool DeleteShadowCopy(const std::wstring& id) {
    if (!IsRunningElevated()) return false;

    std::wstring vssadminPath = util::SystemToolPath(L"vssadmin");
    if (vssadminPath.empty()) return false; // nao arrisca resolucao via PATH

    std::wstring commandLine = vssadminPath + L" delete shadows /shadow=" + id + L" /quiet";
    util::CommandResult cmd = util::RunCommandCaptureOutput(commandLine);
    return cmd.exitCode == 0;
}

} // namespace storagecleaner
