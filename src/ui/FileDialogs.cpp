#include "FileDialogs.h"

#include <windows.h>
#include <shobjidl.h>

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace storagecleaner::ui {

namespace {

// COM é inicializado por chamada (e não uma vez no RunApp) porque só estes
// diálogos o usam na UI thread; CoInitializeEx é contado por referência,
// então chamar de novo numa thread já inicializada é seguro.
class ComScope {
public:
    ComScope() : hr_(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~ComScope() {
        if (SUCCEEDED(hr_)) ::CoUninitialize();
    }
    bool ok() const { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }

private:
    HRESULT hr_;
};

std::optional<std::wstring> ResultPath(IFileDialog* dialog) {
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) return std::nullopt;
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return std::nullopt;
    std::wstring result(path);
    ::CoTaskMemFree(path);
    return result;
}

} // namespace

std::optional<std::wstring> PickFolder() {
    ComScope com;
    if (!com.ok()) return std::nullopt;

    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog))))
        return std::nullopt;

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (FAILED(dialog->Show(::GetActiveWindow()))) return std::nullopt; // inclui cancelar
    return ResultPath(dialog.Get());
}

} // namespace storagecleaner::ui
