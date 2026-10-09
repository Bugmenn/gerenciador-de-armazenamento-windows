#pragma once

#include "storagecleaner/Config.h"

#include <optional>

namespace storagecleaner {

// Persiste em %APPDATA%\StorageCleaner\config.json. Cria com os valores
// padrão (Config::Defaults()) se o arquivo ainda não existir.
Config LoadConfig();
void SaveConfig(const Config& config);

// Como LoadConfig, mas devolve nullopt quando o arquivo existe e não pôde ser
// lido/interpretado (em vez de padrões). Usado antes de regravar só um campo
// (janela, pastas protegidas): regravar sobre um arquivo ilegível trocaria a
// config do usuário pelos padrões sem ele saber.
std::optional<Config> TryLoadConfig();

} // namespace storagecleaner
