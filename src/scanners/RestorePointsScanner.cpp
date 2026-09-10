#include "storagecleaner/Scanner.h"
#include "storagecleaner/VssRestorePoints.h"

namespace storagecleaner {

std::vector<ScanItem> ScanRestorePoints(const Config& config, ProgressChannel& progress,
                                        std::atomic<bool>&) {
    ProgressSnapshot snapshot;
    snapshot.phase = Phase::ScanningRestorePoints;
    progress.Update(snapshot);

    std::vector<ScanItem> items;

    if (!IsRunningElevated()) {
        // Sem privilégio de administrador o vssadmin nem consulta a lista;
        // a UI mostra um aviso pedindo para reabrir o app como Administrador
        // em vez de reportar isso como um erro genérico.
        snapshot.lastError =
            "Pontos de restauracao requerem executar como Administrador";
        progress.Update(snapshot);
        return items;
    }

    std::vector<ShadowCopyInfo> shadows = ListShadowCopies();
    // Mantém as N mais recentes (a lista já vem em ordem cronológica do
    // vssadmin); o restante é candidato a remoção.
    int toKeep = config.restorePointsToKeep;
    for (std::size_t i = 0; i < shadows.size(); ++i) {
        bool isAmongNewest = static_cast<int>(shadows.size() - i) <= toKeep;
        if (isAmongNewest) continue;

        ScanItem item;
        item.path = shadows[i].id;
        // Tamanho de shadow copies individuais nem sempre é obtido de forma
        // confiável via vssadmin — 0 aqui significa "tamanho desconhecido" e
        // a UI deve mostrar isso explicitamente em vez de um número errado.
        item.sizeBytes = shadows[i].sizeBytes.value_or(0);
        item.category = Category::RestorePoints;
        item.isDirectory = false;
        items.push_back(std::move(item));
    }

    snapshot.fractionComplete = 1.0f;
    progress.Update(snapshot);
    return items;
}

} // namespace storagecleaner
