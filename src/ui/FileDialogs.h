#pragma once

#include <optional>
#include <string>

namespace storagecleaner::ui {

// Diálogos nativos do Windows (IFileOpenDialog). Modais:
// bloqueiam o loop de renderização enquanto abertos, como qualquer diálogo
// nativo. Retornam nullopt se o usuário cancelar ou a API falhar.
std::optional<std::wstring> PickFolder();

} // namespace storagecleaner::ui
