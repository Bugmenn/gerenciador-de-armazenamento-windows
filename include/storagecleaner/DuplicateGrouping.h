#pragma once

#include "storagecleaner/ScanItem.h"

#include <cstdint>
#include <string>
#include <vector>

namespace storagecleaner {

// Um arquivo já com hash calculado, pronto para ser agrupado. Deliberadamente
// livre de qualquer chamada de sistema de arquivos para que a lógica de
// agrupamento (a parte que realmente importa revisar/testar) possa ser
// exercitada com dados fabricados, inclusive fora do Windows.
struct HashedFile {
    std::wstring path;
    std::uint64_t sizeBytes = 0;
    std::string sha256Hex;
    // Qualquer valor comparável monotônico com o tempo (ex.: ticks de
    // last_write_time); usado só para decidir qual copia manter.
    std::int64_t lastWriteTicks = 0;
};

// Agrupa por hash (assume que todos os `files` já têm o mesmo tamanho — quem
// chama já fez o agrupamento por tamanho antes de hashear, exatamente para
// evitar hashear arquivos que não podem ser duplicados). Para cada grupo com
// 2+ arquivos, mantém o mais antigo (menor lastWriteTicks) e devolve um
// ScanItem::Duplicate para cada um dos demais, com keepPath apontando para o
// mantido.
std::vector<ScanItem> GroupDuplicatesByHash(std::vector<HashedFile> files);

} // namespace storagecleaner
