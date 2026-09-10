#pragma once

#include "storagecleaner/ScanItem.h"

#include <chrono>
#include <cstdint>
#include <vector>

namespace storagecleaner {

struct CategoryBucket {
    Category category;
    std::vector<ScanItem> items;
    std::uint64_t totalBytes = 0;
};

// Resultado agregado de uma varredura completa. A seleção de itens (o que o
// usuário marcou para excluir) fica de fora deste struct, na camada de UI —
// mantém o scanner e o cleaner desacoplados da apresentação.
struct ScanResult {
    std::vector<CategoryBucket> buckets;
    std::chrono::system_clock::time_point scannedAt;

    std::uint64_t GrandTotalBytes() const {
        std::uint64_t total = 0;
        for (const auto& bucket : buckets) total += bucket.totalBytes;
        return total;
    }
};

} // namespace storagecleaner
