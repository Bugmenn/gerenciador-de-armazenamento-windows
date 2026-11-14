#pragma once

#include <windows.h>

namespace storagecleaner::ui {

// Mensagem customizada usada pelo Shell_NotifyIcon para notificar cliques no
// icone da bandeja; escolhida em WM_APP para nao colidir com mensagens do
// sistema.
constexpr UINT kTrayCallbackMessage = WM_APP + 1;

// WM_COMMAND do item "Sair" do menu da bandeja — também usado por main.cpp
// para pedir que uma instância antiga sem elevação feche pelo caminho normal.
constexpr UINT kTrayExitCommand = 3;

// "TaskbarCreated": o Explorer avisa quando (re)cria a área de notificação —
// no logon (a tarefa de auto-início pode subir o app antes dele) ou depois de
// reiniciar. Quem recebe precisa recriar o ícone. Processo elevado precisa
// liberar a mensagem (AllowTaskbarCreated), senão o Windows a bloqueia vindo
// do Explorer, que roda sem elevação.
UINT TaskbarCreatedMessage();
void AllowTaskbarCreated(HWND hwnd);

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

// Liga/desliga o auto-inicio: uma tarefa agendada no logon do usuario atual
// com "privilegios mais altos", que sobe o app ja elevado e sem aviso do UAC
// (uma entrada Run comum nao serve: o Windows nao eleva nada que suba por
// ela). Registrar a tarefa exige o app elevado; devolve false se falhou.
// Ligar/desligar tambem apaga a entrada Run antiga (versoes anteriores).
bool SetStartWithWindows(bool enabled);

// true se o .exe esta em Program Files. A tarefa de auto-inicio roda o .exe
// do caminho registrado com Administrador e sem aviso: numa pasta que o
// usuario pode alterar, outro programa poderia trocar o .exe e ganhar
// Administrador no proximo logon — a UI avisa.
bool ExecutableInProtectedFolder();

// Quem tinha o auto-início das versões sem elevação (entrada Run): troca pela
// tarefa elevada — só com o app elevado e o .exe em Program Files (a tarefa
// roda sem aviso o .exe do caminho atual). Devolve true se migrou.
bool MigrateLegacyStartupEntry();

} // namespace storagecleaner::ui
