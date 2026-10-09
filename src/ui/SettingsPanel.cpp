#include "FileDialogs.h"
#include "Panels.h"
#include "Theme.h"
#include "TrayIcon.h"
#include "storagecleaner/ConfigStore.h"
#include "storagecleaner/Utils.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <optional>

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
    if (ImGui::InputInt("Tamanho mínimo de arquivo (KB)", &minSizeKb)) {
        if (minSizeKb < 0) minSizeKb = 0;
        config.minFileSizeBytes = static_cast<std::uint64_t>(minSizeKb) * 1024;
    }
    // SanitizeConfig e' chamada uma unica vez apos os tres campos (em vez de
    // um `if` por campo) para nao precisar lembrar de repetir a chamada a
    // cada novo campo clampado que for adicionado aqui no futuro.
    bool tempAgeChanged = ImGui::InputInt("Temporários: idade mínima (horas)",
                                          &config.tempFilesMinAgeHours);
    bool logsChanged = ImGui::InputInt("Logs mais antigos que (dias)", &config.oldLogsThresholdDays);
    bool restoreChanged =
        ImGui::InputInt("Pontos de restauração a manter", &config.restorePointsToKeep);
    bool orphanChanged = ImGui::InputInt("Dados órfãos: inativos há pelo menos (dias)",
                                         &config.orphanedAppsInactivityDays);
    if (tempAgeChanged || logsChanged || restoreChanged || orphanChanged) SanitizeConfig(config);

    ImGui::Separator();
    ImGui::TextUnformatted("Alerta de pouco espaço");
    ImGui::Checkbox("Avisar na bandeja quando uma unidade estiver quase cheia",
                    &config.lowDiskAlertEnabled);
    ImGui::BeginDisabled(!config.lowDiskAlertEnabled);
    if (ImGui::InputInt("Avisar abaixo de (% livre)", &config.lowDiskAlertPercent))
        SanitizeConfig(config);
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextUnformatted("Pastas protegidas (nunca entram na limpeza)");
    ImGui::TextDisabled("Uma pasta que contenha uma pasta protegida também não é oferecida.");
    std::optional<std::size_t> toRemove;
    for (std::size_t i = 0; i < config.excludedPaths.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton("Remover")) toRemove = i;
        ImGui::SameLine();
        ImGui::TextUnformatted(util::WideToUtf8(config.excludedPaths[i]).c_str());
        ImGui::PopID();
    }
    bool exclusionsChanged = false;
    if (toRemove) {
        config.excludedPaths.erase(config.excludedPaths.begin() + *toRemove);
        exclusionsChanged = true;
    }
    if (config.excludedPaths.empty()) ImGui::TextColored(theme::kTextMuted, "Nenhuma pasta protegida.");
    if (ImGui::Button("Adicionar pasta...")) {
        if (auto folder = PickFolder()) {
            config.excludedPaths.push_back(*folder);
            SanitizeConfig(config); // descarta duplicata
            state.ApplyExclusionsToLastResult();
            exclusionsChanged = true;
        }
    }
    if (exclusionsChanged) {
        // Salva na hora, e só esta lista: esquecer o "Salvar" aqui faria a
        // proteção sumir no próximo início (inclusive na limpeza automática),
        // e as demais edições ainda não salvas continuam pendentes.
        // Nunca por cima de um config.json ilegível (perderia o resto da
        // config do usuário); nesse caso só vale até fechar o app, e avisa.
        auto onDisk = TryLoadConfig();
        state.exclusionsSaveFailed = !onDisk.has_value();
        if (onDisk) {
            onDisk->excludedPaths = config.excludedPaths;
            SaveConfig(*onDisk);
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(salvo automaticamente)");
    if (state.exclusionsSaveFailed)
        ImGui::TextColored(theme::kWarning,
                           "Não foi possível ler o config.json atual: as pastas protegidas valem só "
                           "até fechar o app. Use \"Salvar configurações\" para regravar o arquivo.");

    ImGui::Separator();
    ImGui::TextUnformatted("Segundo plano");
    if (ImGui::Checkbox("Iniciar com o Windows", &config.startWithWindows))
        SetStartWithWindows(config.startWithWindows);
    ImGui::Checkbox("Minimizar para a bandeja ao fechar", &config.minimizeToTrayOnClose);
    // Minimo de 5 min (ver SanitizeConfig): a varredura leve roda em worker
    // thread propria mas ainda faz I/O real (Shell API + soma recursiva de
    // %TEMP%), entao um intervalo muito baixo faria essa thread disparar
    // quase sem parar. UiState::Tick tambem clampa isso como segunda linha
    // de defesa.
    if (ImGui::InputInt("Intervalo da varredura leve (minutos)",
                       &config.backgroundScanIntervalMinutes))
        SanitizeConfig(config);

    ImGui::Separator();
    ImGui::TextUnformatted("Limpeza automática");
    ImGui::Checkbox("Habilitar limpeza automática", &config.autoCleanEnabled);

    static const std::array<AutoCleanFrequency, 4> kFrequencies = {
        AutoCleanFrequency::Daily, AutoCleanFrequency::Every7Days,
        AutoCleanFrequency::Every15Days, AutoCleanFrequency::Monthly};
    if (ImGui::BeginCombo("Frequência", AutoCleanFrequencyName(config.autoCleanFrequency))) {
        for (auto freq : kFrequencies) {
            bool isSelected = config.autoCleanFrequency == freq;
            if (ImGui::Selectable(AutoCleanFrequencyName(freq), isSelected))
                config.autoCleanFrequency = freq;
        }
        ImGui::EndCombo();
    }

    char timeBuf[8];
    std::snprintf(timeBuf, sizeof(timeBuf), "%s", config.autoCleanTimeOfDay.c_str());
    if (ImGui::InputText("Horário (HH:MM)", timeBuf, sizeof(timeBuf)))
        config.autoCleanTimeOfDay = timeBuf;

    static const std::array<AutoCleanMode, 2> kModes = {AutoCleanMode::AutoDelete,
                                                        AutoCleanMode::AskConfirmation};
    for (auto mode : kModes) {
        if (ImGui::RadioButton(AutoCleanModeName(mode), config.autoCleanMode == mode))
            config.autoCleanMode = mode;
    }
    ImGui::TextDisabled(
        "\"Dados órfãos\" nunca entra na limpeza automática, mesmo com \"Excluir "
        "automaticamente\" selecionado - exige sempre revisão manual.");

    ImGui::Separator();
    if (ImGui::Button("Salvar configurações", ImVec2(200, 0))) {
        SaveConfig(config);
        state.exclusionsSaveFailed = false;
    }
}

} // namespace storagecleaner::ui
