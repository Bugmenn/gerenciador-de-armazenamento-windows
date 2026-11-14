#include "TrayIcon.h"

#include "resource.h"
#include "storagecleaner/PathExclusion.h"
#include "storagecleaner/Utils.h"

#define SECURITY_WIN32
#include <security.h>
#include <shellapi.h>
#include <shlobj.h>
#include <taskschd.h>
#include <wrl/client.h>

#include <string>

namespace storagecleaner::ui {

namespace {

constexpr UINT kTrayIconId = 1;
NOTIFYICONDATAW g_iconData{};

enum MenuCommandId {
    kMenuOpen = 1,
    kMenuScanNow = 2,
    kMenuExit = kTrayExitCommand,
};

} // namespace

void CreateTrayIcon(HWND hwnd) {
    g_iconData = {};
    g_iconData.cbSize = sizeof(g_iconData);
    g_iconData.hWnd = hwnd;
    g_iconData.uID = kTrayIconId;
    g_iconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_iconData.uCallbackMessage = kTrayCallbackMessage;
    // Mesmo icone de resources/app.rc usado na janela (App.cpp); carregado no
    // tamanho pequeno de sistema (SM_CXSMICON) porque e' o que a bandeja
    // exibe, em vez do generico IDI_APPLICATION.
    g_iconData.hIcon = static_cast<HICON>(::LoadImageW(
        ::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    wcsncpy_s(g_iconData.szTip, L"Otimizador de Armazenamento", _TRUNCATE);
    ::Shell_NotifyIconW(NIM_ADD, &g_iconData);
}

void RemoveTrayIcon() { ::Shell_NotifyIconW(NIM_DELETE, &g_iconData); }

UINT TaskbarCreatedMessage() {
    static const UINT message = ::RegisterWindowMessageW(L"TaskbarCreated");
    return message;
}

void AllowTaskbarCreated(HWND hwnd) {
    ::ChangeWindowMessageFilterEx(hwnd, TaskbarCreatedMessage(), MSGFLT_ALLOW, nullptr);
}

void ShowTrayBalloon(const wchar_t* title, const wchar_t* text) {
    g_iconData.uFlags = NIF_INFO;
    wcsncpy_s(g_iconData.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(g_iconData.szInfo, text, _TRUNCATE);
    // Adiar balões em "Não perturbe"/Assistente de Foco, jogo em tela cheia e
    // apresentação é feito pelo próprio Windows para qualquer notificação.
    // RESPECT_QUIET_TIME só descarta balões na primeira hora após o primeiro
    // logon de um usuário novo — recomendado pela Microsoft para avisos que
    // não são resposta a uma ação do usuário (quase todos os deste app; no
    // "Varredura concluída" manual, perder o aviso nessa 1a hora é aceitável).
    g_iconData.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
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

namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kStartupTaskName[] = L"Otimizador de Armazenamento - Iniciar com o Windows";

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

bool HasLegacyRunEntry() {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    bool exists = ::RegQueryValueExW(key, L"StorageCleaner", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    ::RegCloseKey(key);
    return exists;
}

// Entrada usada até a versão que rodava sem elevação.
void RemoveLegacyRunEntry() {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;
    ::RegDeleteValueW(key, L"StorageCleaner");
    ::RegCloseKey(key);
}

// BSTR com dono (as APIs do Agendador de Tarefas só aceitam BSTR).
class Bstr {
public:
    explicit Bstr(const std::wstring& text) : value_(::SysAllocStringLen(text.data(), static_cast<UINT>(text.size()))) {}
    ~Bstr() { ::SysFreeString(value_); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
    operator BSTR() const { return value_; }

private:
    BSTR value_;
};

// Mesmo padrão de FileDialogs.cpp: CoInitializeEx é contado por referência.
class ComScope {
public:
    ComScope() : hr_(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~ComScope() {
        if (SUCCEEDED(hr_)) ::CoUninitialize();
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    HRESULT hr_;
};

ComPtr<ITaskFolder> RootTaskFolder(ComPtr<ITaskService>& service) {
    ComPtr<ITaskFolder> root;
    if (FAILED(::CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service))))
        return root;
    VARIANT empty;
    ::VariantInit(&empty);
    if (FAILED(service->Connect(empty, empty, empty, empty))) return root;
    service->GetFolder(Bstr(L"\\").operator BSTR(), &root);
    return root;
}

// "DOMINIO\\usuario" de quem roda o app — a tarefa dispara só no logon dele.
std::wstring CurrentUserSamName() {
    wchar_t name[256];
    ULONG size = 256;
    if (!::GetUserNameExW(NameSamCompatible, name, &size)) return {};
    return std::wstring(name, size);
}

bool RegisterStartupTask() {
    wchar_t exePath[MAX_PATH];
    DWORD len = ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring user = CurrentUserSamName();
    if (len == 0 || len >= MAX_PATH || user.empty()) return false;

    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> root = RootTaskFolder(service);
    ComPtr<ITaskDefinition> task;
    if (!root || FAILED(service->NewTask(0, &task))) return false;

    ComPtr<IRegistrationInfo> info;
    if (SUCCEEDED(task->get_RegistrationInfo(&info)))
        info->put_Description(Bstr(L"Abre o Otimizador de Armazenamento na bandeja ao entrar no Windows."));

    ComPtr<IPrincipal> principal;
    if (FAILED(task->get_Principal(&principal)) || FAILED(principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN)) ||
        FAILED(principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST)))
        return false;

    // O padrão do Windows encerra a tarefa depois de 72 h e não a inicia na
    // bateria — o app fica aberto na bandeja o dia todo, inclusive em notebook.
    ComPtr<ITaskSettings> settings;
    // Prioridade 4 = normal (o padrão do Agendador, 7, roda abaixo do normal,
    // inclusive em I/O). Sem disparo manual: a tarefa só serve ao logon.
    if (FAILED(task->get_Settings(&settings)) || FAILED(settings->put_ExecutionTimeLimit(Bstr(L"PT0S"))) ||
        FAILED(settings->put_Priority(4)) || FAILED(settings->put_AllowDemandStart(VARIANT_FALSE)) ||
        FAILED(settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE)) ||
        FAILED(settings->put_StopIfGoingOnBatteries(VARIANT_FALSE)) ||
        FAILED(settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW)))
        return false;

    ComPtr<ITriggerCollection> triggers;
    ComPtr<ITrigger> trigger;
    ComPtr<ILogonTrigger> logon;
    if (FAILED(task->get_Triggers(&triggers)) || FAILED(triggers->Create(TASK_TRIGGER_LOGON, &trigger)) ||
        FAILED(trigger.As(&logon)) || FAILED(logon->put_UserId(Bstr(user))) ||
        // Dá tempo ao Explorer de montar a bandeja (TaskbarCreated cobre o resto).
        FAILED(logon->put_Delay(Bstr(L"PT15S"))))
        return false;

    ComPtr<IActionCollection> actions;
    ComPtr<IAction> action;
    ComPtr<IExecAction> exec;
    if (FAILED(task->get_Actions(&actions)) || FAILED(actions->Create(TASK_ACTION_EXEC, &action)) ||
        FAILED(action.As(&exec)) || FAILED(exec->put_Path(Bstr(exePath))) ||
        FAILED(exec->put_Arguments(Bstr(L"--startup"))))
        return false;

    VARIANT userId;
    ::VariantInit(&userId);
    userId.vt = VT_BSTR;
    userId.bstrVal = ::SysAllocString(user.c_str());
    VARIANT empty;
    ::VariantInit(&empty);
    ComPtr<IRegisteredTask> registered;
    HRESULT hr = root->RegisterTaskDefinition(Bstr(kStartupTaskName), task.Get(), TASK_CREATE_OR_UPDATE, userId,
                                              empty, TASK_LOGON_INTERACTIVE_TOKEN, empty, &registered);
    ::VariantClear(&userId);
    return SUCCEEDED(hr);
}

bool DeleteStartupTask() {
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> root = RootTaskFolder(service);
    if (!root) return false;
    HRESULT hr = root->DeleteTask(Bstr(kStartupTaskName), 0);
    return SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}

} // namespace

bool SetStartWithWindows(bool enabled) {
    ComScope com;
    bool ok = enabled ? RegisterStartupTask() : DeleteStartupTask();
    // A entrada Run antiga só sai depois de a tarefa ficar no estado pedido:
    // falhando, o auto-início anterior continua funcionando.
    if (ok) RemoveLegacyRunEntry();
    return ok;
}

bool MigrateLegacyStartupEntry() {
    if (!HasLegacyRunEntry() || !ExecutableInProtectedFolder()) return false;
    return SetStartWithWindows(true);
}

bool ExecutableInProtectedFolder() {
    wchar_t exePath[MAX_PATH];
    DWORD len = ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;
    std::vector<std::wstring> protectedRoots;
    for (const GUID& folder : {FOLDERID_ProgramFiles, FOLDERID_ProgramFilesX86}) {
        std::wstring path = util::KnownFolderPath(folder);
        if (!path.empty()) protectedRoots.push_back(path);
    }
    // Mesma comparação da lista de pastas protegidas: "dentro de" ignora
    // maiúsculas e separadores.
    return IsExcludedPath(exePath, false, protectedRoots);
}

} // namespace storagecleaner::ui
