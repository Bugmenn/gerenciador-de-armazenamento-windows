#pragma once

#include <windows.h>

namespace storagecleaner::ui {

// Mensagem customizada usada pelo Shell_NotifyIcon para notificar cliques no
// icone da bandeja; escolhida em WM_APP para nao colidir com mensagens do
// sistema.
constexpr UINT kTrayCallbackMessage = WM_APP + 1;

enum class TrayCommand {
    None,
    Open,
    ScanNow,
    Exit,
};

void CreateTrayIcon(HWND hwnd);
void RemoveTrayIcon();
void ShowTrayBalloon(const wchar_t* title, const wchar_t* text);
// Interpreta WM_COMMAND vindos do menu de contexto da bandeja.
TrayCommand HandleTrayCommand(WPARAM wParam);
// Mostra o menu de contexto na posicao do cursor.
void ShowTrayContextMenu(HWND hwnd);

// Aplica ou remove a entrada de auto-inicio em
// HKCU\Software\Microsoft\Windows\CurrentVersion\Run — nao exige elevacao
// porque grava na hive do usuario atual, nao na de maquina.
void SetStartWithWindows(bool enabled);

} // namespace storagecleaner::ui
