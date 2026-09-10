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
