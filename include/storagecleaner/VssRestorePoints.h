#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace storagecleaner {

struct ShadowCopyInfo {
    std::wstring id;              // GUID entre chaves, como aparece na saída do vssadmin
    std::wstring creationTime;    // texto bruto do vssadmin; parsing exato varia por versão/idioma do Windows
    std::optional<std::uint64_t> sizeBytes;  // nem sempre é possível estimar via vssadmin
};

// true quando o processo atual tem privilégios de administrador — necessário
// para consultar/remover pontos de restauração via vssadmin.
bool IsRunningElevated();

// Lista os shadow copies da unidade do sistema (C:), mais recentes por
// último. Retorna vazio se não elevado ou se o comando falhar.
std::vector<ShadowCopyInfo> ListShadowCopies();

// Remove um shadow copy específico pelo ID. Operação permanente, sem
// "desfazer" — ver IsPermanentCategory.
bool DeleteShadowCopy(const std::wstring& id);

} // namespace storagecleaner
