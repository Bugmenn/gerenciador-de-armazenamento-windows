#include "storagecleaner/RecycleBinOps.h"
#include "storagecleaner/Scanner.h"

namespace storagecleaner {

std::vector<ScanItem> ScanRecycleBin(const Config&, ProgressChannel& progress,
                                      std::atomic<bool>&) {
    ProgressSnapshot snapshot;
    snapshot.phase = Phase::ScanningRecycleBin;
    progress.Update(snapshot);

    std::vector<ScanItem> items;
    std::uint64_t size = QueryRecycleBinSizeBytes();
    if (size > 0) {
        // Item único e agregado: a Lixeira é esvaziada de uma vez
        // (SHEmptyRecycleBinW), não item a item, então não faz sentido listar
        // arquivo por arquivo aqui.
        items.push_back(ScanItem{L"(Lixeira)", size, Category::RecycleBin, L"", true});
    }

    snapshot.bytesFoundSoFar = size;
    snapshot.fractionComplete = 1.0f;
    progress.Update(snapshot);
    return items;
}

} // namespace storagecleaner
