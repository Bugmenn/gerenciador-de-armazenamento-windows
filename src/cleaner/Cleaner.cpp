#include "storagecleaner/Cleaner.h"
#include "storagecleaner/RecycleBinOps.h"
#include "storagecleaner/VssRestorePoints.h"

#include <windows.h>
#include <shobjidl.h>

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace storagecleaner {

namespace {

// Envia um lote de arquivos/pastas para a Lixeira via IFileOperation (API COM
// moderna, preferida a SHFileOperationW por lidar melhor com caminhos longos
// e reportar erro por item). FOF_ALLOWUNDO é o que torna a exclusão
// reversível pelo usuário através da própria Lixeira do Windows.
bool SendToRecycleBin(const std::vector<ScanItem>& items, std::string& errorSummary) {
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

    bool anyItemAdded = false;
    for (const auto& item : items) {
        ComPtr<IShellItem> shellItem;
        hr = ::SHCreateItemFromParsingName(item.path.c_str(), nullptr, IID_PPV_ARGS(&shellItem));
        if (FAILED(hr)) continue; // item pode ter sido removido/renomeado entre o scan e a limpeza
        if (SUCCEEDED(fileOp->DeleteItem(shellItem.Get(), nullptr))) anyItemAdded = true;
    }

    if (!anyItemAdded) return true; // nada de válido para excluir não é um erro

    hr = fileOp->PerformOperations();
    if (FAILED(hr)) {
        errorSummary = "Falha ao mover itens para a Lixeira";
        return false;
    }
    return true;
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

    if (!cancel.load() && !reversible.empty()) {
        if (SendToRecycleBin(reversible, errorSummary)) {
            for (const auto& item : reversible) {
                entry.bytesFreedByCategory[item.category] += item.sizeBytes;
                entry.totalBytesFreed += item.sizeBytes;
                if (entry.items.size() < HistoryEntry::kMaxItemsPerEntry)
                    entry.items.push_back({item.path, item.sizeBytes, item.category});
            }
        } else {
            entry.success = false;
        }
        snapshot.itemsProcessed += reversible.size();
        progress.Update(snapshot);
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
