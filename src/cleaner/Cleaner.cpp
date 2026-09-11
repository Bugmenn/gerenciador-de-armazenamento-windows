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
// `succeeded` recebe apenas os itens de fato enfileirados com sucesso — o
// chamador so deve contabilizar bytes liberados/gravar no historico para
// esses, nao para `items` inteiro, ja que um item pode ter sido
// removido/renomeado entre o scan e a limpeza.
bool SendToRecycleBin(const std::vector<ScanItem>& items, std::string& errorSummary,
                      std::vector<ScanItem>& succeeded, std::size_t& itemsSkipped) {
    if (items.empty()) return true;

    ComPtr<IFileOperation> fileOp;
    HRESULT hr = ::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL,
                                    IID_PPV_ARGS(&fileOp));
    if (FAILED(hr)) {
        errorSummary = "Falha ao iniciar operacao de arquivo (COM)";
        itemsSkipped += items.size();
        return false;
    }

    fileOp->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT |
                              FOF_NOERRORUI);

    for (const auto& item : items) {
        ComPtr<IShellItem> shellItem;
        hr = ::SHCreateItemFromParsingName(item.path.c_str(), nullptr, IID_PPV_ARGS(&shellItem));
        if (FAILED(hr)) {
            ++itemsSkipped; // item pode ter sido removido/renomeado entre o scan e a limpeza
            continue;
        }
        if (SUCCEEDED(fileOp->DeleteItem(shellItem.Get(), nullptr)))
            succeeded.push_back(item);
        else
            ++itemsSkipped;
    }

    if (succeeded.empty()) return true; // nada de válido para excluir não é um erro

    hr = fileOp->PerformOperations();
    if (FAILED(hr)) {
        // IFileOperation sem um IFileOperationProgressSink nao diz quais
        // itens do lote de fato foram movidos antes da falha agregada — so
        // sabemos que PerformOperations, como um todo, nao teve sucesso.
        // Escolha deliberadamente conservadora: contar tudo como "nao
        // confirmado" (itemsSkipped) em vez de arriscar contabilizar bytes
        // liberados de itens que podem nao ter sido de fato excluidos.
        errorSummary = "Falha ao mover itens para a Lixeira";
        itemsSkipped += succeeded.size();
        succeeded.clear();
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
        std::vector<ScanItem> reversibleSucceeded;
        if (SendToRecycleBin(reversible, errorSummary, reversibleSucceeded, entry.itemsSkipped)) {
            for (const auto& item : reversibleSucceeded) {
                entry.bytesFreedByCategory[item.category] += item.sizeBytes;
                entry.totalBytesFreed += item.sizeBytes;
                if (entry.items.size() < HistoryEntry::kMaxItemsPerEntry)
                    entry.items.push_back({item.path, item.sizeBytes, item.category});
            }
        } else {
            entry.success = false;
        }
        snapshot.itemsProcessed += reversibleSucceeded.size();
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
            entry.itemsSkipped += recycleBinToEmpty.size();
            if (!errorSummary.empty()) errorSummary += "; ";
            errorSummary += "Falha ao esvaziar a Lixeira";
        }
    }

    // Pontos de restauração removidos via vssadmin também não têm "desfazer".
    if (!cancel.load() && !restorePointsToDelete.empty()) {
        for (std::size_t i = 0; i < restorePointsToDelete.size(); ++i) {
            if (cancel.load()) {
                // Os itens ainda nao processados nao devem virar "sucesso
                // silencioso" no historico — sem isso, uma limpeza cancelada
                // no meio do caminho apareceria como "OK" completo.
                entry.itemsSkipped += restorePointsToDelete.size() - i;
                break;
            }
            const auto& item = restorePointsToDelete[i];
            if (DeleteShadowCopy(item.path)) {
                entry.bytesFreedByCategory[Category::RestorePoints] += item.sizeBytes;
                entry.totalBytesFreed += item.sizeBytes;
                if (entry.items.size() < HistoryEntry::kMaxItemsPerEntry)
                    entry.items.push_back({item.path, item.sizeBytes, item.category});
            } else {
                entry.success = false;
                ++entry.itemsSkipped;
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
