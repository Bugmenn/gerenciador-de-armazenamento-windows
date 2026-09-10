#include "TrayIcon.h"

#include <shellapi.h>

#include <string>

namespace storagecleaner::ui {

namespace {

constexpr UINT kTrayIconId = 1;
NOTIFYICONDATAW g_iconData{};

enum MenuCommandId {
    kMenuOpen = 1,
    kMenuScanNow = 2,
    kMenuExit = 3,
};

} // namespace

void CreateTrayIcon(HWND hwnd) {
    g_iconData = {};
    g_iconData.cbSize = sizeof(g_iconData);
    g_iconData.hWnd = hwnd;
    g_iconData.uID = kTrayIconId;
    g_iconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_iconData.uCallbackMessage = kTrayCallbackMessage;
    g_iconData.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(g_iconData.szTip, L"Otimizador de Armazenamento", _TRUNCATE);
    ::Shell_NotifyIconW(NIM_ADD, &g_iconData);
}

void RemoveTrayIcon() { ::Shell_NotifyIconW(NIM_DELETE, &g_iconData); }

void ShowTrayBalloon(const wchar_t* title, const wchar_t* text) {
    g_iconData.uFlags = NIF_INFO;
    wcsncpy_s(g_iconData.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(g_iconData.szInfo, text, _TRUNCATE);
    g_iconData.dwInfoFlags = NIIF_INFO;
    ::Shell_NotifyIconW(NIM_MODIFY, &g_iconData);
    g_iconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; // restaura flags normais
}

void ShowTrayContextMenu(HWND hwnd) {
    POINT cursor;
    ::GetCursorPos(&cursor);

    HMENU menu = ::CreatePopupMenu();
    ::AppendMenuW(menu, MF_STRING, kMenuOpen, L"Abrir");
    ::AppendMenuW(menu, MF_STRING, kMenuScanNow, L"Escanear agora");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, kMenuExit, L"Sair");

    // Necessario para o menu fechar corretamente ao clicar fora dele (ver
    // documentacao do TrackPopupMenu / padrao "SetForegroundWindow trick").
    ::SetForegroundWindow(hwnd);
    ::TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, hwnd, nullptr);
    ::PostMessageW(hwnd, WM_NULL, 0, 0);
    ::DestroyMenu(menu);
}

TrayCommand HandleTrayCommand(WPARAM wParam) {
    switch (LOWORD(wParam)) {
        case kMenuOpen:     return TrayCommand::Open;
        case kMenuScanNow:  return TrayCommand::ScanNow;
        case kMenuExit:     return TrayCommand::Exit;
    }
    return TrayCommand::None;
}

void SetStartWithWindows(bool enabled) {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE,
                        &key) != ERROR_SUCCESS)
        return;

    const wchar_t* valueName = L"StorageCleaner";
    if (enabled) {
        wchar_t exePath[MAX_PATH];
        ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        // "--startup" avisa o main.cpp para abrir direto na bandeja em vez de
        // mostrar a janela durante o login do usuario.
        std::wstring command = L"\"" + std::wstring(exePath) + L"\" --startup";
        ::RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                         static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        ::RegDeleteValueW(key, valueName);
    }
    ::RegCloseKey(key);
}

} // namespace storagecleaner::ui
