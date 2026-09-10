#include "storagecleaner/RecycleBinOps.h"

#include <windows.h>
#include <shellapi.h>

namespace storagecleaner {

std::uint64_t QueryRecycleBinSizeBytes() {
    SHQUERYRBINFO info{};
    info.cbSize = sizeof(info);
    // nullptr = consulta todas as unidades, não só uma específica.
    if (FAILED(::SHQueryRecycleBinW(nullptr, &info))) return 0;
    return static_cast<std::uint64_t>(info.i64Size);
}

bool EmptyRecycleBin() {
    HRESULT hr = ::SHEmptyRecycleBinW(nullptr, nullptr,
                                       SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    // S_FALSE é retornado quando a Lixeira já está vazia — não é uma falha.
    return SUCCEEDED(hr) || hr == S_FALSE;
}

} // namespace storagecleaner
