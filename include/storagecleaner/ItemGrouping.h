#pragma once

#include "storagecleaner/ScanResult.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace storagecleaner {

// Pasta e nome de um caminho, cortando no último '\' ou '/'. Caminho sem
// separador (ex.: "(Lixeira)", ID de shadow copy) não tem pasta: o nome é o
// caminho inteiro.
std::wstring_view ParentFolder(std::wstring_view path);
std::wstring_view FileNameOf(std::wstring_view path);

// Filtro da aba Resultados. `needle` vazio casa com tudo; a comparação ignora
// maiúsculas (inclusive acentuadas) e trata '/' como '\'.
struct ItemFilter {
    std::wstring needle;
    std::uint64_t minBytes = 0;
};
bool MatchesFilter(const ScanItem& item, const ItemFilter& filter);

enum class ItemSortKey { Size, Name };

// Itens de uma mesma pasta. `itemIndices` aponta para o vetor de itens
// original — a seleção da UI (vetor paralelo a ele) continua valendo.
struct ItemGroup {
    std::wstring folder;
    std::vector<std::size_t> itemIndices;
    std::uint64_t totalBytes = 0;
};

// Agrupa por pasta só os itens que passam no filtro. Grupos são ordenados pelo
// total (Size) ou pela pasta (Name); os itens de cada grupo, pelo tamanho ou
// pelo nome do arquivo. Empates caem na ordem alfabética, para a lista não
// "pular" entre reconstruções.
std::vector<ItemGroup> GroupItemsByFolder(const std::vector<ScanItem>& items,
                                          const ItemFilter& filter, ItemSortKey sortKey,
                                          bool descending);

// Bytes encontrados por letra de unidade (maiúscula), para a rosca de cada
// unidade no Painel. Itens sem caminho "X:\..." (Lixeira, shadow copies) não
// entram: não dá para saber de qual unidade vêm.
std::map<wchar_t, std::uint64_t> CleanableBytesByDrive(const std::vector<CategoryBucket>& buckets);

} // namespace storagecleaner
