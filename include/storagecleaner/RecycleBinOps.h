#pragma once

#include <cstdint>

namespace storagecleaner {

// Consulta o tamanho total ocupado pela Lixeira em todas as unidades. Retorna
// 0 em caso de falha (ex.: sem unidades acessíveis).
std::uint64_t QueryRecycleBinSizeBytes();

// Esvazia a Lixeira permanentemente (sem confirmação/progresso do Explorer).
// Não há "desfazer" para esta operação — ver IsPermanentCategory.
bool EmptyRecycleBin();

} // namespace storagecleaner
