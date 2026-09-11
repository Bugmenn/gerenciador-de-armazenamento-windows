#pragma once

#include "storagecleaner/ScanItem.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace storagecleaner {

// Com que frequência a limpeza automática deve rodar.
enum class AutoCleanFrequency {
    Daily,
    Every7Days,
    Every15Days,
    Monthly,
};

// AutoDelete limpa sozinho o que for encontrado; AskConfirmation apenas
// escaneia e traz a janela para frente, deixando a exclusão para o usuário.
enum class AutoCleanMode {
    AutoDelete,
    AskConfirmation,
};

struct CategoryConfig {
    bool enabled = true;
};

struct Config {
    std::uint64_t minFileSizeBytes = 4096;
    // Arquivo em %TEMP%/Windows\Temp só é candidato a "temporário" se não foi
    // escrito nas últimas N horas — evita marcar para exclusão um arquivo que
    // um processo em execução ainda está usando (instalador extraindo,
    // download em andamento, autosave).
    int tempFilesMinAgeHours = 1;
    int oldLogsThresholdDays = 30;
    int restorePointsToKeep = 2;
    // Pasta de AppData sem app instalado correspondente só é candidata a
    // "órfã" se estiver inativa há pelo menos esse tempo — evita marcar
    // como órfã um app raramente aberto mas ainda instalado.
    int orphanedAppsInactivityDays = 180;

    // Vazio = usa os padrões (Documentos, Área de trabalho, Downloads, Imagens).
    std::vector<std::wstring> duplicateScanRoots;

    std::map<Category, CategoryConfig> perCategory;

    // Execução em segundo plano.
    bool startWithWindows = false;
    bool minimizeToTrayOnClose = true;
    int backgroundScanIntervalMinutes = 60;

    // Limpeza automática agendada.
    bool autoCleanEnabled = false;
    AutoCleanFrequency autoCleanFrequency = AutoCleanFrequency::Every7Days;
    AutoCleanMode autoCleanMode = AutoCleanMode::AskConfirmation;
    // "HH:MM" no relógio de 24h, horário local.
    std::string autoCleanTimeOfDay = "03:00";

    static Config Defaults();
};

// Aplica os limites minimos de Config (chamada apos carregar config.json ou
// apos qualquer edicao pela UI) — fonte unica para essas invariantes, para
// nao precisar reimplementar o mesmo clamp em cada ponto de mutacao.
void SanitizeConfig(Config& config);

const char* AutoCleanFrequencyName(AutoCleanFrequency f);
const char* AutoCleanModeName(AutoCleanMode m);

} // namespace storagecleaner
