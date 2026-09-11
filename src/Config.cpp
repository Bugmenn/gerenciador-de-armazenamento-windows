#include "storagecleaner/Config.h"

namespace storagecleaner {

Config Config::Defaults() {
    Config config;
    for (Category c : kAllCategories) {
        CategoryConfig catConfig;
        // "Dados órfãos" é heurístico (ver ScanItem.h / IsHeuristicCategory):
        // fica desligado até o usuário revisar e habilitar conscientemente.
        catConfig.enabled = !IsHeuristicCategory(c);
        config.perCategory[c] = catConfig;
    }
    return config;
}

void SanitizeConfig(Config& config) {
    // Fonte unica desses limites — chamada tanto ao carregar config.json
    // quanto ao editar pela UI, para nao duplicar a mesma regra em varios
    // lugares (e arriscar divergir se um dos limites mudar no futuro).
    if (config.oldLogsThresholdDays < 0) config.oldLogsThresholdDays = 0;
    if (config.restorePointsToKeep < 0) config.restorePointsToKeep = 0;
    if (config.orphanedAppsInactivityDays < 0) config.orphanedAppsInactivityDays = 0;
    // Minimo 5: a varredura leve roda em worker thread propria mas ainda faz
    // I/O real (Shell API + soma recursiva de %TEMP%), entao um intervalo
    // muito baixo faria essa thread disparar quase sem parar.
    if (config.backgroundScanIntervalMinutes < 5) config.backgroundScanIntervalMinutes = 5;
}

const char* AutoCleanFrequencyName(AutoCleanFrequency f) {
    switch (f) {
        case AutoCleanFrequency::Daily:       return "Todo dia";
        case AutoCleanFrequency::Every7Days:  return "A cada 7 dias";
        case AutoCleanFrequency::Every15Days: return "A cada 15 dias";
        case AutoCleanFrequency::Monthly:     return "Uma vez por mes";
    }
    return "Desconhecido";
}

const char* AutoCleanModeName(AutoCleanMode m) {
    switch (m) {
        case AutoCleanMode::AutoDelete:      return "Excluir automaticamente";
        case AutoCleanMode::AskConfirmation: return "Mostrar e pedir confirmacao";
    }
    return "Desconhecido";
}

} // namespace storagecleaner
