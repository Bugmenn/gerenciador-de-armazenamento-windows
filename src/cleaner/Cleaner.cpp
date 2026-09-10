#include "storagecleaner/Cleaner.h"
#include "storagecleaner/RecycleBinOps.h"
#include "storagecleaner/VssRestorePoints.h"

#include <windows.h>
#include <shobjidl.h>

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace storagecleaner {

namespace {

// Sink mínimo de IFileOperationProgressSink: o único callback que nos
// interessa é PostDeleteItem, que informa se aquele item específico foi
// removido com sucesso. Sem isso, IFileOperation::PerformOperations() pode
// retornar sucesso geral mesmo com itens individuais falhando (ex.: arquivo
// travado por outro processo) — e o chamador contaria bytes liberados que na
// verdade não foram.
class DeleteProgressSink : public IFileOperationProgressSink {
public:
    // Um bool por chamada a DeleteItem(), na mesma ordem em que foram
    // adicionadas — IFileOperation processa a fila na ordem de inserção, e
    // os callbacks Post*Item disparam nessa mesma ordem.
    std::vector<bool> succeeded;

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IFileOperationProgressSink)) {
            *ppv = static_cast<IFileOperationProgressSink*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refCount_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG remaining = --refCount_;
        if (remaining == 0) delete this;
        return remaining;
    }

    IFACEMETHODIMP StartOperations() override { return S_OK; }
    IFACEMETHODIMP FinishOperations(HRESULT) override { return S_OK; }
    IFACEMETHODIMP PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    IFACEMETHODIMP PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override {
        return S_OK;
    }
    IFACEMETHODIMP PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    IFACEMETHODIMP PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT,
                                IShellItem*) override {
        return S_OK;
    }
    IFACEMETHODIMP PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    IFACEMETHODIMP PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT,
                                IShellItem*) override {
        return S_OK;
    }
    IFACEMETHODIMP PreDeleteItem(DWORD, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP PostDeleteItem(DWORD, IShellItem*, HRESULT hrDelete, IShellItem*) override {
        succeeded.push_back(SUCCEEDED(hrDelete));
        return S_OK;
    }
    IFACEMETHODIMP PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    IFACEMETHODIMP PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT,
                               IShellItem*) override {
        return S_OK;
    }
    IFACEMETHODIMP UpdateProgress(UINT, UINT) override { return S_OK; }
    IFACEMETHODIMP ResetTimer() override { return S_OK; }
    IFACEMETHODIMP PauseTimer() override { return S_OK; }
    IFACEMETHODIMP ResumeTimer() override { return S_OK; }

private:
    ULONG refCount_ = 1; // dono inicial é quem chama `new`; ver uso em SendToRecycleBin
};

// Envia um lote de arquivos/pastas para a Lixeira via IFileOperation (API COM
// moderna, preferida a SHFileOperationW por lidar melhor com caminhos longos
// e reportar erro por item). FOF_ALLOWUNDO é o que torna a exclusão
// reversível pelo usuário através da própria Lixeira do Windows.
//
// Preenche `succeededPerItem` (mesmo tamanho e ordem de `items`) indicando
// item a item se a remoção realmente aconteceu — necessário porque
// PerformOperations() pode retornar sucesso mesmo com itens individuais
// falhando (ex.: arquivo aberto em outro programa).
bool SendToRecycleBin(const std::vector<ScanItem>& items, std::vector<bool>& succeededPerItem,
                      std::string& errorSummary) {
    succeededPerItem.assign(items.size(), false);
    if (items.empty()) return true;

    ComPtr<IFileOperation> fileOp;
    HRESULT hr = ::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL,
                                    IID_PPV_ARGS(&fileOp));
    if (FAILED(hr)) {
        errorSummary = "Falha ao iniciar operacao de arquivo (COM)";
        return false;
    }

    fileOp->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT |
                              FOF_NOERRORUI);

    // Referência própria (refCount_ inicial = 1): Advise() faz seu próprio
    // AddRef, então o Release() explicito no fim devolve exatamente ao
    // balanço esperado depois do Unadvise().
    auto* sink = new DeleteProgressSink();
    DWORD adviseCookie = 0;
    bool advised = SUCCEEDED(fileOp->Advise(sink, &adviseCookie));

    // Índice original (em `items`) de cada DeleteItem() efetivamente
    // enfileirado — itens que falharam em SHCreateItemFromParsingName nunca
    // chegam a gerar um callback PostDeleteItem, então precisamos desse mapa
    // para correlacionar de volta corretamente.
    std::vector<std::size_t> queuedOriginalIndex;
    for (std::size_t i = 0; i < items.size(); ++i) {
        ComPtr<IShellItem> shellItem;
        hr = ::SHCreateItemFromParsingName(items[i].path.c_str(), nullptr,
                                           IID_PPV_ARGS(&shellItem));
        if (FAILED(hr)) continue; // item pode ter sido removido/renomeado entre o scan e a limpeza
        if (SUCCEEDED(fileOp->DeleteItem(shellItem.Get(), nullptr)))
            queuedOriginalIndex.push_back(i);
    }

    bool ok = true;
    if (!queuedOriginalIndex.empty()) {
        hr = fileOp->PerformOperations();
        if (FAILED(hr)) {
            errorSummary = "Falha ao mover itens para a Lixeira";
            ok = false;
        }
    }

    if (advised) fileOp->Unadvise(adviseCookie);

    for (std::size_t i = 0; i < queuedOriginalIndex.size() && i < sink->succeeded.size(); ++i)
        succeededPerItem[queuedOriginalIndex[i]] = sink->succeeded[i];

    sink->Release();
    return ok;
}

} // namespace

HistoryEntry CleanItems(const std::vector<ScanItem>& selectedItems, ProgressChannel& progress,
                        std::atomic<bool>& cancel, bool wasAutomatic) {
    HistoryEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.wasAutomatic = wasAutomatic;

    ProgressSnapshot snapshot;
    snapshot.phase = Phase::Cleaning;
    progress.Update(snapshot);

    // COM é por thread (apartment-threaded); a Cleaner roda na worker thread
    // dedicada, então inicializa/finaliza aqui em vez de assumir que o
    // chamador já fez isso.
    HRESULT comInit = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::vector<ScanItem> reversible;
    std::vector<ScanItem> recycleBinToEmpty;
    std::vector<ScanItem> restorePointsToDelete;

    for (const auto& item : selectedItems) {
        if (item.category == Category::RecycleBin) {
            recycleBinToEmpty.push_back(item);
        } else if (item.category == Category::RestorePoints) {
            restorePointsToDelete.push_back(item);
        } else {
            reversible.push_back(item);
        }
    }

    std::string errorSummary;
    std::size_t failedReversibleCount = 0;

    if (!cancel.load() && !reversible.empty()) {
        std::vector<bool> succeededPerItem;
        bool ranOk = SendToRecycleBin(reversible, succeededPerItem, errorSummary);
        for (std::size_t i = 0; i < reversible.size(); ++i) {
            const ScanItem& item = reversible[i];
            bool itemSucceeded = i < succeededPerItem.size() && succeededPerItem[i];
            if (!itemSucceeded) {
                ++failedReversibleCount;
                continue; // não conta bytes liberados nem entra no histórico como removido
            }
            entry.bytesFreedByCategory[item.category] += item.sizeBytes;
            entry.totalBytesFreed += item.sizeBytes;
            if (entry.items.size() < HistoryEntry::kMaxItemsPerEntry)
                entry.items.push_back({item.path, item.sizeBytes, item.category});
        }
        if (!ranOk) entry.success = false;
        snapshot.itemsProcessed += reversible.size();
        progress.Update(snapshot);
    }

    if (failedReversibleCount > 0) {
        entry.success = false;
        if (!errorSummary.empty()) errorSummary += "; ";
        errorSummary += std::to_string(failedReversibleCount) +
                        " item(ns) nao puderam ser movidos para a Lixeira (ex.: em uso por "
                        "outro programa)";
    }

    // Esvaziar a Lixeira é permanente e definitivo (ver IsPermanentCategory);
    // por isso está isolado do caminho "reversível" acima.
    if (!cancel.load() && !recycleBinToEmpty.empty()) {
        std::uint64_t freedBefore = 0;
        for (const auto& item : recycleBinToEmpty) freedBefore += item.sizeBytes;

        if (EmptyRecycleBin()) {
            entry.bytesFreedByCategory[Category::RecycleBin] += freedBefore;
            entry.totalBytesFreed += freedBefore;
        } else {
            entry.success = false;
            if (!errorSummary.empty()) errorSummary += "; ";
            errorSummary += "Falha ao esvaziar a Lixeira";
        }
    }

    // Pontos de restauração removidos via vssadmin também não têm "desfazer".
    if (!cancel.load() && !restorePointsToDelete.empty()) {
        for (const auto& item : restorePointsToDelete) {
            if (DeleteShadowCopy(item.path)) {
                entry.bytesFreedByCategory[Category::RestorePoints] += item.sizeBytes;
                entry.totalBytesFreed += item.sizeBytes;
                if (entry.items.size() < HistoryEntry::kMaxItemsPerEntry)
                    entry.items.push_back({item.path, item.sizeBytes, item.category});
            } else {
                entry.success = false;
                if (!errorSummary.empty()) errorSummary += "; ";
                errorSummary += "Falha ao remover ponto de restauracao (vssadmin)";
            }
        }
    }

    entry.errorSummary = errorSummary;

    if (SUCCEEDED(comInit)) ::CoUninitialize();

    snapshot.phase = cancel.load() ? Phase::Cancelled : Phase::Done;
    snapshot.fractionComplete = 1.0f;
    progress.Update(snapshot);

    return entry;
}

} // namespace storagecleaner
