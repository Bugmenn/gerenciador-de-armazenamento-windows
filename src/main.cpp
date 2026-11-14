#include "storagecleaner/Scanner.h"
#include "ui/App.h"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <string>

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

    // Impede duas instancias simultaneas: ambas leriam/escreveriam
    // config.json e history.json ao mesmo tempo sem nenhum lock entre
    // processos, podendo perder o registro de uma limpeza (last-write-wins
    // no MoveFileExW), alem de rodar dois ScanEngine/Cleaner concorrentes
    // sobre os mesmos arquivos.
    HANDLE singleInstanceMutex = ::CreateMutexW(nullptr, TRUE, L"Local\\StorageCleanerSingleInstance");
    bool alreadyRunning = singleInstanceMutex != nullptr && ::GetLastError() == ERROR_ALREADY_EXISTS;
    if (alreadyRunning) {
        if (singleInstanceMutex) ::CloseHandle(singleInstanceMutex);
        return 0;
    }

    // "--startup" e' passado pela propria entrada de auto-inicio do Windows
    // (ver TrayIcon::SetStartWithWindows) para diferenciar "o usuario abriu o
    // app" de "o Windows ligou e o app subiu sozinho" — nesse segundo caso a
    // janela abre direto minimizada na bandeja, sem interromper o login.
    std::wstring args = cmdLine ? cmdLine : L"";
    bool startMinimized = args.find(L"--startup") != std::wstring::npos;
    bool testBalloons = args.find(L"--test-balloons") != std::wstring::npos;
    // O teste de balões é só visual: nunca exclui nada, mesmo que o usuário
    // clique em "Limpar" durante ele.
    bool dryRun = testBalloons || args.find(L"--dry-run") != std::wstring::npos;

    int result = storagecleaner::ui::RunApp(startMinimized, dryRun, testBalloons);

    if (singleInstanceMutex) ::CloseHandle(singleInstanceMutex);
    return result;
}
