#pragma once

#include <string>
#include <vector>

namespace storagecleaner {

// Normaliza um caminho para comparação: '/' vira '\', letras em minúsculas
// (o NTFS não diferencia maiúsculas por padrão) e sem '\' no final.
std::wstring NormalizePathForCompare(const std::wstring& path);

// true se `itemPath` não pode ser removido por causa da lista de exclusões:
// é um caminho excluído, está dentro de um, ou — quando é uma pasta inteira
// a remover — contém um caminho excluído (apagar a pasta-pai levaria o
// excluído junto). Livre de Win32 para ser testável (tests/test_path_exclusion.cpp).
bool IsExcludedPath(const std::wstring& itemPath, bool itemIsDirectory,
                    const std::vector<std::wstring>& excludedPaths);

} // namespace storagecleaner
