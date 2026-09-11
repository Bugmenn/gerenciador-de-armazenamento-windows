#include "Panels.h"
#include "TrayIcon.h"
#include "storagecleaner/ConfigStore.h"

#include <imgui.h>

#include <array>

namespace storagecleaner::ui {

namespace {

int MinFileSizeKb(const Config& config) {
    return static_cast<int>(config.minFileSizeBytes / 1024);
}

} // namespace

void DrawSettingsPanel(UiState& state) {
    Config& config = state.config;

    ImGui::TextUnformatted("Limites gerais");
    int minSizeKb = MinFileSizeKb(config);
    if (ImGui::InputInt("Tamanho minimo de arquivo (KB)", &minSizeKb)) {
        if (minSizeKb < 0) minSizeKb = 0;
        config.minFileSizeBytes = static_cast<std::uint64_t>(minSizeKb) * 1024;
    }
    // SanitizeConfig e' chamada uma unica vez apos os tres campos (em vez de
    // um `if` por campo) para nao precisar lembrar de repetir a chamada a
    // cada novo campo clampado que for adicionado aqui no futuro.
    bool logsChanged = ImGui::InputInt("Logs mais antigos que (dias)", &config.oldLogsThresholdDays);
    bool restoreChanged =
        ImGui::InputInt("Pontos de restauracao a manter", &config.restorePointsToKeep);
    bool orphanChanged = ImGui::InputInt("Dados orfaos: inativos ha pelo menos (dias)",
                                         &config.orphanedAppsInactivityDays);
    if (logsChanged || restoreChanged || orphanChanged) SanitizeConfig(config);

    ImGui::Separator();
    ImGui::TextUnformatted("Segundo plano");
    if (ImGui::Checkbox("Iniciar com o Windows", &config.startWithWindows))
        SetStartWithWindows(config.startWithWindows);
    ImGui::Checkbox("Minimizar para a bandeja ao fechar", &config.minimizeToTrayOnClose);
    if (ImGui::InputInt("Intervalo da varredura leve (minutos)",
                       &config.backgroundScanIntervalMinutes))
        SanitizeConfig(config);

    ImGui::Separator();
    ImGui::TextUnformatted("Limpeza automatica");
    ImGui::Checkbox("Habilitar limpeza automatica", &config.autoCleanEnabled);

    static const std::array<AutoCleanFrequency, 4> kFrequencies = {
        AutoCleanFrequency::Daily, AutoCleanFrequency::Every7Days,
        AutoCleanFrequency::Every15Days, AutoCleanFrequency::Monthly};
    if (ImGui::BeginCombo("Frequencia", AutoCleanFrequencyName(config.autoCleanFrequency))) {
        for (auto freq : kFrequencies) {
            bool isSelected = config.autoCleanFrequency == freq;
            if (ImGui::Selectable(AutoCleanFrequencyName(freq), isSelected))
                config.autoCleanFrequency = freq;
        }
        ImGui::EndCombo();
    }

    char timeBuf[8];
    std::snprintf(timeBuf, sizeof(timeBuf), "%s", config.autoCleanTimeOfDay.c_str());
    if (ImGui::InputText("Horario (HH:MM)", timeBuf, sizeof(timeBuf)))
        config.autoCleanTimeOfDay = timeBuf;

    static const std::array<AutoCleanMode, 2> kModes = {AutoCleanMode::AutoDelete,
                                                        AutoCleanMode::AskConfirmation};
    for (auto mode : kModes) {
        if (ImGui::RadioButton(AutoCleanModeName(mode), config.autoCleanMode == mode))
            config.autoCleanMode = mode;
    }
    ImGui::TextDisabled(
        "\"Dados orfaos\" nunca entra na limpeza automatica, mesmo com \"Excluir "
        "automaticamente\" selecionado - exige sempre revisao manual.");

    ImGui::Separator();
    if (ImGui::Button("Salvar configuracoes", ImVec2(200, 0))) SaveConfig(config);
}

} // namespace storagecleaner::ui
