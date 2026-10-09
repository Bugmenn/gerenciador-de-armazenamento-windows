#include "storagecleaner/Config.h"
#include "storagecleaner/PathExclusion.h"

#include <algorithm>

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
    if (config.tempFilesMinAgeHours < 0) config.tempFilesMinAgeHours = 0;
    if (config.oldLogsThresholdDays < 0) config.oldLogsThresholdDays = 0;
    if (config.restorePointsToKeep < 0) config.restorePointsToKeep = 0;
    if (config.orphanedAppsInactivityDays < 0) config.orphanedAppsInactivityDays = 0;
    // Minimo 5: a varredura leve roda em worker thread propria mas ainda faz
    // I/O real (Shell API + soma recursiva de %TEMP%), entao um intervalo
    // muito baixo faria essa thread disparar quase sem parar.
    if (config.backgroundScanIntervalMinutes < 5) config.backgroundScanIntervalMinutes = 5;
    // Uma janela menor que isso (config.json editado à mão) deixaria a UI
    // ilegível; melhor voltar ao tamanho padrão. Limite baixo de propósito:
    // o app não é DPI-aware, então em escala 125-150% as coordenadas salvas
    // são lógicas e bem menores que os pixels físicos na tela.
    // Teto: coordenadas de janela do Win32 cabem em ~±32k, e x + width é
    // somado em int ao montar o RECT.
    const WindowPlacement& w = config.window;
    if (w.valid && (w.width < 400 || w.height < 300 || w.width > 32767 || w.height > 32767 ||
                    w.x < -32768 || w.x > 32767 || w.y < -32768 || w.y > 32767))
        config.window.valid = false;
    config.lowDiskAlertPercent = std::clamp(config.lowDiskAlertPercent, 1, 50);

    // Entradas vazias não excluiriam nada e duplicadas só poluem a lista.
    std::vector<std::wstring> uniquePaths;
    for (const auto& path : config.excludedPaths) {
        std::wstring key = NormalizePathForCompare(path);
        if (key.empty()) continue;
        bool seen = std::any_of(uniquePaths.begin(), uniquePaths.end(), [&](const std::wstring& p) {
            return NormalizePathForCompare(p) == key;
        });
        if (!seen) uniquePaths.push_back(path);
    }
    config.excludedPaths = std::move(uniquePaths);
}

const char* AutoCleanFrequencyName(AutoCleanFrequency f) {
    switch (f) {
        case AutoCleanFrequency::Daily:       return "Todo dia";
        case AutoCleanFrequency::Every7Days:  return "A cada 7 dias";
        case AutoCleanFrequency::Every15Days: return "A cada 15 dias";
        case AutoCleanFrequency::Monthly:     return "Uma vez por mês";
    }
    return "Desconhecido";
}

const char* AutoCleanModeName(AutoCleanMode m) {
    switch (m) {
        case AutoCleanMode::AutoDelete:      return "Excluir automaticamente";
        case AutoCleanMode::AskConfirmation: return "Mostrar e pedir confirmação";
    }
    return "Desconhecido";
}

} // namespace storagecleaner
