#include "storagecleaner/DuplicateGrouping.h"

#include <algorithm>
#include <unordered_map>

namespace storagecleaner {

std::vector<ScanItem> GroupDuplicatesByHash(std::vector<HashedFile> files) {
    std::vector<ScanItem> result;

    std::unordered_map<std::string, std::vector<HashedFile>> byHash;
    for (auto& file : files) {
        if (file.sha256Hex.empty()) continue; // hash não calculado (ex.: falha de leitura): ignora
        byHash[file.sha256Hex].push_back(std::move(file));
    }

    for (auto& [hash, group] : byHash) {
        if (group.size() < 2) continue;

        // Cópias costumam ter a mesma data: sem o desempate pelo caminho, qual
        // fica dependeria da ordem em que a listagem de pastas as achou.
        std::sort(group.begin(), group.end(), [](const HashedFile& a, const HashedFile& b) {
            if (a.lastWriteTicks != b.lastWriteTicks) return a.lastWriteTicks < b.lastWriteTicks;
            return a.path < b.path;
        });

        const std::wstring keepPath = group.front().path;
        for (std::size_t i = 1; i < group.size(); ++i) {
            result.push_back(ScanItem{group[i].path, group[i].sizeBytes, Category::Duplicate,
                                      keepPath, false});
        }
    }

    return result;
}

} // namespace storagecleaner
