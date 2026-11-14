#include "storagecleaner/Scanner.h"
#include "storagecleaner/VssRestorePoints.h"
#include "ui/App.h"
#include "ui/TrayIcon.h"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <string>

namespace {

// Administrador com UAC ligado (token "dividido"): o unico caso em que o
// "runas" eleva a mesma conta sem pedir senha. Conta padrao e UAC desligado
// dao TokenElevationTypeDefault.
bool IsLimitedAdminToken() {
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION_TYPE type = TokenElevationTypeDefault;
    DWORD size = sizeof(type);
    bool limited = ::GetTokenInformation(token, TokenElevationType, &type, sizeof(type), &size) &&
                   type == TokenElevationTypeLimited;
    ::CloseHandle(token);
    return limited;
}

bool IsProcessElevated(DWORD pid) {
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return true; // sem acesso: trata como elevada (nao derruba)
    HANDLE token = nullptr;
    bool elevated = true;
    if (::OpenProcessToken(process, TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation{};
        DWORD size = sizeof(elevation);
        if (::GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
            elevated = elevation.TokenIsElevated != 0;
        ::CloseHandle(token);
    }
    ::CloseHandle(process);
    return elevated;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int) {
    // --ui-selftest <pasta>: roteiro automático da UI com dados fictícios. Não
    // lê nem grava config.json/history.json nem exclui nada, então dispensa o
    // mutex de instância única — roda mesmo com o app do usuário aberto.
    std::wstring selfTestDir;
    std::wstring benchmarkReport;
    {
        int argc = 0;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        for (int i = 1; argv && i + 1 < argc; ++i) {
            if (std::wstring(argv[i]) == L"--ui-selftest") selfTestDir = argv[i + 1];
            if (std::wstring(argv[i]) == L"--scan-benchmark") benchmarkReport = argv[i + 1];
        }
        if (argv) ::LocalFree(argv);
    }
    // --scan-benchmark <arquivo>: só varre (leitura) e grava os tempos; não
    // abre janela nem toca config/histórico, então também dispensa o mutex.
    if (!benchmarkReport.empty()) return storagecleaner::RunScanBenchmark(benchmarkReport);
    if (!selfTestDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(selfTestDir, ec);
        if (ec) return 100;
        return storagecleaner::ui::RunApp(false, true, false, selfTestDir);
    }

    // "--startup" e' passado pela propria tarefa de auto-inicio do Windows
    // (ver TrayIcon::SetStartWithWindows) para diferenciar "o usuario abriu o
    // app" de "o Windows ligou e o app subiu sozinho" — nesse segundo caso a
    // janela abre direto minimizada na bandeja, sem interromper o login.
    std::wstring args = cmdLine ? cmdLine : L"";
    bool startMinimized = args.find(L"--startup") != std::wstring::npos;

    bool testBalloons = args.find(L"--test-balloons") != std::wstring::npos;
    const bool elevated = storagecleaner::IsRunningElevated();
    // O app sempre roda como Administrador (pontos de restauracao, cache do
    // Windows Update, despejos do sistema): sem elevacao, se reabre com
    // "runas" (UAC) e este processo sai. O .exe continua asInvoker de
    // proposito — requireAdministrator no manifesto faria os modos de teste
    // acima pedirem UAC e o Windows nao sobe por Run um app que exige
    // Administrador. Nao pede UAC:
    //  - no --startup (a tarefa agendada ja sobe elevada; aviso no logon
    //    interromperia o usuario);
    //  - fora de uma conta Administrador com UAC (conta padrao pediria a senha
    //    de OUTRA conta e o app passaria a ver config, temporarios e Lixeira
    //    dela, nao os do usuario logado);
    //  - em --test-balloons (so visual) e --no-elevate (depurar no Visual Studio:
    //    o processo elevado teria outro PID, fora do depurador);
    //  - em --elevated-retry, que evita laco se o Windows devolver um processo
    //    ainda nao elevado.
    const bool shouldElevate = !elevated && !startMinimized && !testBalloons &&
                               args.find(L"--no-elevate") == std::wstring::npos &&
                               args.find(L"--elevated-retry") == std::wstring::npos &&
                               IsLimitedAdminToken();

    // Instancia ja aberta. Mutex criado por processo elevado nega acesso a um
    // nao elevado — ACCESS_DENIED tambem significa "existe, e e elevada".
    constexpr const wchar_t* kMutexName = L"Local\\StorageCleanerSingleInstance";
    if (HANDLE existing = ::OpenMutexW(SYNCHRONIZE, FALSE, kMutexName)) {
        HWND oldWindow = ::FindWindowW(L"StorageCleanerWindowClass", nullptr);
        DWORD oldPid = 0;
        if (oldWindow) ::GetWindowThreadProcessId(oldWindow, &oldPid);
        // Ja existe uma elevada, ou esta nao vai se elevar: a aberta vale.
        if (!shouldElevate && !(elevated && oldWindow && !IsProcessElevated(oldPid))) {
            ::CloseHandle(existing);
            return 0;
        }
        if (elevated) {
            // A aberta e a sem elevacao (ex.: entrada Run antiga, ou UAC
            // recusado antes): pede para ela sair pelo caminho normal ("Sair"
            // da bandeja) e assume no lugar dela.
            ::PostMessageW(oldWindow, WM_COMMAND, storagecleaner::ui::kTrayExitCommand, 0);
            DWORD wait = ::WaitForSingleObject(existing, 15000);
            ::CloseHandle(existing);
            if (wait == WAIT_TIMEOUT) return 0;
        } else {
            ::CloseHandle(existing); // o processo elevado cuida da antiga
        }
    } else if (::GetLastError() == ERROR_ACCESS_DENIED) {
        return 0;
    }

    if (shouldElevate) {
        wchar_t exePath[MAX_PATH];
        DWORD len = ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            std::wstring elevatedArgs = args.empty() ? L"--elevated-retry" : args + L" --elevated-retry";
            SHELLEXECUTEINFOW info{};
            info.cbSize = sizeof(info);
            // Este processo sai logo em seguida: espera o lancamento terminar.
            info.fMask = SEE_MASK_NOASYNC;
            info.lpVerb = L"runas";
            info.lpFile = exePath;
            info.lpParameters = elevatedArgs.c_str();
            info.nShow = SW_SHOWNORMAL;
            if (::ShellExecuteExW(&info)) return 0;
        }
        // UAC recusado ou falha: segue sem elevacao, com as categorias de
        // Administrador puladas (a UI avisa quais).
    }

    // Impede duas instancias simultaneas: ambas leriam/escreveriam
    // config.json e history.json ao mesmo tempo sem nenhum lock entre
    // processos, podendo perder o registro de uma limpeza (last-write-wins
    // no MoveFileExW), alem de rodar dois ScanEngine/Cleaner concorrentes
    // sobre os mesmos arquivos.
    HANDLE singleInstanceMutex = ::CreateMutexW(nullptr, TRUE, kMutexName);
    DWORD mutexError = ::GetLastError();
    bool alreadyRunning = singleInstanceMutex == nullptr ? mutexError == ERROR_ACCESS_DENIED
                                                         : mutexError == ERROR_ALREADY_EXISTS;
    if (alreadyRunning) {
        if (singleInstanceMutex) ::CloseHandle(singleInstanceMutex);
        return 0;
    }

    // O teste de balões é só visual: nunca exclui nada, mesmo que o usuário
    // clique em "Limpar" durante ele.
    bool dryRun = testBalloons || args.find(L"--dry-run") != std::wstring::npos;

    int result = storagecleaner::ui::RunApp(startMinimized, dryRun, testBalloons);

    if (singleInstanceMutex) ::CloseHandle(singleInstanceMutex);
    return result;
}
