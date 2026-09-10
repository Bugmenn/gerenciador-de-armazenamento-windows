#pragma once

#include "storagecleaner/HistoryEntry.h"

#include <chrono>
#include <optional>
#include <vector>

namespace storagecleaner {

// Persiste em %APPDATA%\StorageCleaner\history.json, escrita atômica
// (arquivo .tmp + rename) para não corromper o histórico se o processo
// cair no meio de uma gravação.
std::vector<HistoryEntry> LoadHistory();
void AppendHistoryEntry(const HistoryEntry& entry);

// Usado pelo agendador de limpeza automática para saber se já passou tempo
// suficiente desde a última execução, sobrevivendo a reinícios do app.
std::optional<std::chrono::system_clock::time_point> LastAutoCleanTimestamp();

} // namespace storagecleaner
