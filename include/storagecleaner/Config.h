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
// escaneia e avisa por balão na bandeja (sem roubar o foco), deixando a
// exclusão para o usuário.
enum class AutoCleanMode {
    AutoDelete,
    AskConfirmation,
};

struct CategoryConfig {
    bool enabled = true;
};

// Posição/tamanho da janela do Windows na última saída do app. Coordenadas
// de GetWindowPlacement (rcNormalPosition): mesmo sistema que
// SetWindowPlacement consome, então o ida-e-volta é exato.
struct WindowPlacement {
    bool valid = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool maximized = false;
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

    // Pastas/arquivos que nunca são oferecidos para limpeza, nem uma pasta
    // que os contenha (ver PathExclusion.h).
    std::vector<std::wstring> excludedPaths;

    // Balão na bandeja quando o espaço livre de uma unidade fixa cai abaixo
    // desse percentual; rearma quando volta a ficar acima.
    bool lowDiskAlertEnabled = true;
    int lowDiskAlertPercent = 10;

    // Gravada só ao sair do app (App.cpp), independente do botão "Salvar".
    WindowPlacement window;

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
