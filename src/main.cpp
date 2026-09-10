#include "ui/App.h"

#include <windows.h>

#include <string>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int) {
    // "--startup" e' passado pela propria entrada de auto-inicio do Windows
    // (ver TrayIcon::SetStartWithWindows) para diferenciar "o usuario abriu o
    // app" de "o Windows ligou e o app subiu sozinho" — nesse segundo caso a
    // janela abre direto minimizada na bandeja, sem interromper o login.
    std::wstring args = cmdLine ? cmdLine : L"";
    bool startMinimized = args.find(L"--startup") != std::wstring::npos;

    return storagecleaner::ui::RunApp(startMinimized);
}
