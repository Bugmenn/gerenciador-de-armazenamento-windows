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

        std::sort(group.begin(), group.end(), [](const HashedFile& a, const HashedFile& b) {
            return a.lastWriteTicks < b.lastWriteTicks;
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
