#pragma once

#include "storagecleaner/Config.h"
#include "storagecleaner/HistoryEntry.h"
#include "storagecleaner/ProgressState.h"
#include "storagecleaner/ScanItem.h"

#include <atomic>
#include <vector>

namespace storagecleaner {

// Remove os itens selecionados e devolve o registro de histórico da execução.
// Categorias não-permanentes (ver IsPermanentCategory) vão para a Lixeira via
// IFileOperation (reversível); Lixeira e pontos de restauração são removidos
// em definitivo, pois não têm conceito de desfazer.
HistoryEntry CleanItems(const std::vector<ScanItem>& selectedItems,
                        ProgressChannel& progress, std::atomic<bool>& cancel,
                        bool wasAutomatic);

} // namespace storagecleaner
