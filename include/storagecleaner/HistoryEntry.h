#pragma once

#include "storagecleaner/ScanItem.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace storagecleaner {

struct HistoryItemRecord {
    std::wstring path;
    std::uint64_t sizeBytes = 0;
    Category category;
};

// Um registro por execução de limpeza concluída (não por varredura sem
// limpeza — essas são efêmeras e não entram no histórico).
struct HistoryEntry {
    std::chrono::system_clock::time_point timestamp;
    std::map<Category, std::uint64_t> bytesFreedByCategory;
    std::uint64_t totalBytesFreed = 0;
    // Capado por execução para o arquivo de histórico não crescer sem limite
    // em limpezas com dezenas de milhares de arquivos; os totais acima
    // continuam exatos mesmo quando a lista é truncada.
    static constexpr std::size_t kMaxItemsPerEntry = 5000;
    // Limite de execuções mantidas no history.json; as mais antigas são
    // descartadas para o arquivo não crescer sem limite em uso de longo prazo.
    static constexpr std::size_t kMaxEntries = 500;
    std::vector<HistoryItemRecord> items;
    // Itens que estavam no scan mas sumiram/mudaram antes da limpeza (ex.:
    // removidos ou renomeados pelo usuário nesse meio-tempo) — não entram em
    // totalBytesFreed nem em `items`, mas o usuário deve saber que existiram
    // em vez de a diferença passar despercebida.
    std::size_t itemsSkipped = 0;
    bool success = true;
    std::string errorSummary;
    // true quando disparada pelo agendamento automático em vez de um clique manual.
    bool wasAutomatic = false;
};

} // namespace storagecleaner
