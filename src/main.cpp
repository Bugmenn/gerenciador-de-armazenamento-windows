#include "ui/App.h"

#include <windows.h>

#include <string>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int) {
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
