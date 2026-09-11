#include "storagecleaner/Scanner.h"
#include "storagecleaner/VssRestorePoints.h"

#include <windows.h>
#include <oleauto.h>

#include <algorithm>
#include <utility>

namespace storagecleaner {

namespace {

// vssadmin nao garante contratualmente a ordem de saida (o codigo assume
// cronologica, mais antigo primeiro), entao tentamos reordenar de forma
// explicita usando a data de criacao. VarDateFromStr interpreta o texto
// conforme a localidade do usuario (mesma localidade em que o vssadmin
// escreveu), entao lida com os varios formatos possiveis sem parser manual.
bool TryParseCreationTime(const std::wstring& text, double& outDate) {
    return ::VarDateFromStr(text.c_str(), LOCALE_USER_DEFAULT, 0, &outDate) == S_OK;
}

// Reordena por data quando possivel; se qualquer entrada nao puder ser
// interpretada (formato inesperado), mantem a ordem original devolvida pelo
// vssadmin em vez de arriscar uma ordenacao parcialmente errada.
void SortByCreationTimeIfPossible(std::vector<ShadowCopyInfo>& shadows) {
    // Primeiro passo so' valida (sem mover nada) — se qualquer entrada falhar
    // o parse, retorna sem ter tocado `shadows`. Mover antes de confirmar que
    // todas as entradas sao parseaveis deixaria `shadows` parcialmente
    // "movido-de" no caminho de retorno antecipado, corrompendo justamente a
    // ordem original que esse fallback deveria preservar.
    std::vector<double> dates(shadows.size());
    for (std::size_t i = 0; i < shadows.size(); ++i) {
        if (!TryParseCreationTime(shadows[i].creationTime, dates[i])) return;
    }

    std::vector<std::pair<double, ShadowCopyInfo>> dated;
    dated.reserve(shadows.size());
    for (std::size_t i = 0; i < shadows.size(); ++i)
        dated.emplace_back(dates[i], std::move(shadows[i]));

    std::sort(dated.begin(), dated.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    for (std::size_t i = 0; i < dated.size(); ++i) shadows[i] = std::move(dated[i].second);
}

} // namespace

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
    SortByCreationTimeIfPossible(shadows);
    // Mantém as N mais recentes (mais antigo primeiro após a ordenação
    // acima); o restante é candidato a remoção. restorePointsToKeep já vem
    // não-negativo (ver Config::SanitizeConfig, aplicado no load e na UI).
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
