#pragma once

#include "storagecleaner/Config.h"

namespace storagecleaner {

// Persiste em %APPDATA%\StorageCleaner\config.json. Cria com os valores
// padrão (Config::Defaults()) se o arquivo ainda não existir.
Config LoadConfig();
void SaveConfig(const Config& config);

} // namespace storagecleaner
