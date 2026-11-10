#include "storagecleaner/ItemGrouping.h"

#include "storagecleaner/PathExclusion.h"

#include <algorithm>
#include <unordered_map>

namespace storagecleaner {

namespace {

std::size_t LastSeparator(std::wstring_view path) { return path.find_last_of(L"\\/"); }

// `needle` já normalizado (vazio casa com tudo).
bool MatchesNormalized(const ScanItem& item, const std::wstring& needle, std::uint64_t minBytes) {
    if (item.sizeBytes < minBytes) return false;
    if (needle.empty()) return true;
    return NormalizePathForCompare(item.path).find(needle) != std::wstring::npos;
}

} // namespace

std::wstring_view ParentFolder(std::wstring_view path) {
    std::size_t sep = LastSeparator(path);
    if (sep == std::wstring_view::npos) return {};
    // Raiz de unidade ("C:\arquivo") mantém o separador: "C:" sozinho seria
    // a pasta atual daquela unidade, não a raiz.
    if (sep == 2 && path.size() > 1 && path[1] == L':') return path.substr(0, 3);
    return path.substr(0, sep);
}

std::wstring_view FileNameOf(std::wstring_view path) {
    std::size_t sep = LastSeparator(path);
    return sep == std::wstring_view::npos ? path : path.substr(sep + 1);
}

// NormalizePathForCompare já dobra maiúsculas (inclusive Latin-1) e unifica
// separadores — mesma regra da lista de pastas protegidas.
bool MatchesFilter(const ScanItem& item, const ItemFilter& filter) {
    return MatchesNormalized(item, NormalizePathForCompare(filter.needle), filter.minBytes);
}

std::vector<ItemGroup> GroupItemsByFolder(const std::vector<ScanItem>& items,
                                          const ItemFilter& filter, ItemSortKey sortKey,
                                          bool descending) {
    std::vector<ItemGroup> groups;
    std::unordered_map<std::wstring, std::size_t> groupByFolder;
    // Normalizado uma vez: o filtro é reaplicado a cada tecla digitada.
    const std::wstring needle = NormalizePathForCompare(filter.needle);
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (!MatchesNormalized(items[i], needle, filter.minBytes)) continue;
        std::wstring folder(ParentFolder(items[i].path));
        auto [it, inserted] = groupByFolder.try_emplace(folder, groups.size());
        if (inserted) groups.push_back(ItemGroup{std::move(folder), {}, 0});
        ItemGroup& group = groups[it->second];
        group.itemIndices.push_back(i);
        group.totalBytes += items[i].sizeBytes;
    }

    // Chaves de nome pré-calculadas: normalizar dentro do comparador custaria
    // uma alocação por comparação em listas de milhares de itens.
    std::vector<std::wstring> nameKey(items.size());
    for (const auto& group : groups)
        for (std::size_t i : group.itemIndices)
            nameKey[i] = NormalizePathForCompare(std::wstring(FileNameOf(items[i].path)));

    auto itemLess = [&](std::size_t a, std::size_t b) {
        if (sortKey == ItemSortKey::Size && items[a].sizeBytes != items[b].sizeBytes)
            return descending ? items[a].sizeBytes > items[b].sizeBytes
                              : items[a].sizeBytes < items[b].sizeBytes;
        if (nameKey[a] != nameKey[b])
            return (sortKey == ItemSortKey::Name && descending) ? nameKey[a] > nameKey[b]
                                                                : nameKey[a] < nameKey[b];
        return a < b;
    };
    for (auto& group : groups)
        std::sort(group.itemIndices.begin(), group.itemIndices.end(), itemLess);

    std::vector<std::wstring> folderKey;
    folderKey.reserve(groups.size());
    for (const auto& group : groups) folderKey.push_back(NormalizePathForCompare(group.folder));
    std::vector<std::size_t> order(groups.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (sortKey == ItemSortKey::Size && groups[a].totalBytes != groups[b].totalBytes)
            return descending ? groups[a].totalBytes > groups[b].totalBytes
                              : groups[a].totalBytes < groups[b].totalBytes;
        if (folderKey[a] != folderKey[b])
            return (sortKey == ItemSortKey::Name && descending) ? folderKey[a] > folderKey[b]
                                                                : folderKey[a] < folderKey[b];
        return a < b;
    });

    std::vector<ItemGroup> sorted;
    sorted.reserve(groups.size());
    for (std::size_t i : order) sorted.push_back(std::move(groups[i]));
    return sorted;
}

std::map<wchar_t, std::uint64_t> CleanableBytesByDrive(const std::vector<CategoryBucket>& buckets) {
    std::map<wchar_t, std::uint64_t> byDrive;
    for (const auto& bucket : buckets) {
        for (const auto& item : bucket.items) {
            const std::wstring& p = item.path;
            if (p.size() < 3 || p[1] != L':' || (p[2] != L'\\' && p[2] != L'/')) continue;
            wchar_t letter = p[0];
            if (letter >= L'a' && letter <= L'z') letter = static_cast<wchar_t>(letter - L'a' + L'A');
            if (letter < L'A' || letter > L'Z') continue;
            byDrive[letter] += item.sizeBytes;
        }
    }
    return byDrive;
}

} // namespace storagecleaner
