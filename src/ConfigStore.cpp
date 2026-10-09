#include "storagecleaner/ConfigStore.h"
#include "storagecleaner/Utils.h"

#include <windows.h>
#include <shlobj.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace storagecleaner {

namespace {

fs::path AppDataDir() {
    PWSTR path = nullptr;
    fs::path result;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path))) {
        result = fs::path(path) / L"StorageCleaner";
        ::CoTaskMemFree(path);
    }
    return result;
}

fs::path ConfigPath() { return AppDataDir() / L"config.json"; }

json ToJson(const CategoryConfig& cc) {
    json j;
    j["enabled"] = cc.enabled;
    return j;
}

CategoryConfig CategoryConfigFromJson(const json& j) {
    CategoryConfig cc;
    cc.enabled = j.value("enabled", true);
    return cc;
}

json ToJson(const Config& config) {
    json j;
    j["minFileSizeBytes"] = config.minFileSizeBytes;
    j["tempFilesMinAgeHours"] = config.tempFilesMinAgeHours;
    j["oldLogsThresholdDays"] = config.oldLogsThresholdDays;
    j["restorePointsToKeep"] = config.restorePointsToKeep;
    j["orphanedAppsInactivityDays"] = config.orphanedAppsInactivityDays;
    for (const auto& p : config.duplicateScanRoots)
        j["duplicateScanRoots"].push_back(util::WideToUtf8(p));

    // Mapas em nlohmann::json exigem chave textual — CategoryKey (ScanItem.h)
    // dá uma chave legível e estável, desacoplada do nome exibido na UI.
    json perCategory = json::object();
    for (const auto& [category, cc] : config.perCategory)
        perCategory[CategoryKey(category)] = ToJson(cc);
    j["perCategory"] = perCategory;

    j["excludedPaths"] = json::array();
    for (const auto& p : config.excludedPaths) j["excludedPaths"].push_back(util::WideToUtf8(p));
    j["lowDiskAlertEnabled"] = config.lowDiskAlertEnabled;
    j["lowDiskAlertPercent"] = config.lowDiskAlertPercent;

    if (config.window.valid) {
        j["window"] = {{"x", config.window.x},
                       {"y", config.window.y},
                       {"width", config.window.width},
                       {"height", config.window.height},
                       {"maximized", config.window.maximized}};
    }

    j["startWithWindows"] = config.startWithWindows;
    j["minimizeToTrayOnClose"] = config.minimizeToTrayOnClose;
    j["backgroundScanIntervalMinutes"] = config.backgroundScanIntervalMinutes;

    j["autoCleanEnabled"] = config.autoCleanEnabled;
    j["autoCleanFrequency"] = static_cast<int>(config.autoCleanFrequency);
    j["autoCleanMode"] = static_cast<int>(config.autoCleanMode);
    j["autoCleanTimeOfDay"] = config.autoCleanTimeOfDay;
    return j;
}

Config ConfigFromJson(const json& j) {
    Config config = Config::Defaults();
    config.minFileSizeBytes = j.value("minFileSizeBytes", config.minFileSizeBytes);
    config.tempFilesMinAgeHours = j.value("tempFilesMinAgeHours", config.tempFilesMinAgeHours);
    config.oldLogsThresholdDays = j.value("oldLogsThresholdDays", config.oldLogsThresholdDays);
    config.restorePointsToKeep = j.value("restorePointsToKeep", config.restorePointsToKeep);
    config.orphanedAppsInactivityDays =
        j.value("orphanedAppsInactivityDays", config.orphanedAppsInactivityDays);

    if (j.contains("duplicateScanRoots")) {
        config.duplicateScanRoots.clear();
        for (const auto& p : j["duplicateScanRoots"])
            config.duplicateScanRoots.push_back(util::Utf8ToWide(p.get<std::string>()));
    }

    if (j.contains("perCategory")) {
        for (const auto& [key, value] : j["perCategory"].items()) {
            Category category;
            if (CategoryFromKey(key, category))
                config.perCategory[category] = CategoryConfigFromJson(value);
        }
    }

    if (j.contains("excludedPaths")) {
        for (const auto& p : j["excludedPaths"])
            if (p.is_string()) config.excludedPaths.push_back(util::Utf8ToWide(p.get<std::string>()));
    }
    config.lowDiskAlertEnabled = j.value("lowDiskAlertEnabled", config.lowDiskAlertEnabled);
    config.lowDiskAlertPercent = j.value("lowDiskAlertPercent", config.lowDiskAlertPercent);

    // Um tipo errado aqui (ex.: "x" como texto) só invalida a janela — sem o
    // try local, o type_error derrubaria a config inteira para os padrões.
    if (j.contains("window") && j["window"].is_object()) {
        try {
            const json& w = j["window"];
            // Lido como int64 e só convertido se couber nas coordenadas do
            // Win32 (~±32k): converter direto para int um número enorme é UB.
            auto coord = [&w](const char* key, bool& ok) {
                std::int64_t v = w.value(key, std::int64_t{0});
                if (v < -32768 || v > 32767) ok = false;
                return ok ? static_cast<int>(v) : 0;
            };
            bool ok = true;
            config.window.x = coord("x", ok);
            config.window.y = coord("y", ok);
            config.window.width = coord("width", ok);
            config.window.height = coord("height", ok);
            config.window.maximized = w.value("maximized", false);
            config.window.valid = ok; // SanitizeConfig descarta tamanho pequeno demais
        } catch (const json::exception&) {
            config.window = WindowPlacement{};
        }
    }

    config.startWithWindows = j.value("startWithWindows", config.startWithWindows);
    config.minimizeToTrayOnClose = j.value("minimizeToTrayOnClose", config.minimizeToTrayOnClose);
    config.backgroundScanIntervalMinutes =
        j.value("backgroundScanIntervalMinutes", config.backgroundScanIntervalMinutes);

    config.autoCleanEnabled = j.value("autoCleanEnabled", config.autoCleanEnabled);
    config.autoCleanFrequency = static_cast<AutoCleanFrequency>(
        j.value("autoCleanFrequency", static_cast<int>(config.autoCleanFrequency)));
    config.autoCleanMode = static_cast<AutoCleanMode>(
        j.value("autoCleanMode", static_cast<int>(config.autoCleanMode)));
    config.autoCleanTimeOfDay = j.value("autoCleanTimeOfDay", config.autoCleanTimeOfDay);

    // Defesa contra config.json editado a mão ou de uma versão incompatível:
    // valores fora desses limites fariam a varredura em segundo plano rodar
    // sem parar ou tratariam qualquer arquivo/pasta como "antigo"/"órfão".
    SanitizeConfig(config);
    return config;
}

} // namespace

std::optional<Config> TryLoadConfig() {
    fs::path path = ConfigPath();
    std::error_code ec;
    if (!fs::exists(path, ec) && !ec) return Config::Defaults();

    std::ifstream in(path);
    if (!in.is_open()) return std::nullopt;
    try {
        json j;
        in >> j;
        return ConfigFromJson(j);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

Config LoadConfig() {
    // Arquivo corrompido ou de uma versão incompatível: preferimos cair para
    // os padrões a impedir o app de abrir.
    return TryLoadConfig().value_or(Config::Defaults());
}

void SaveConfig(const Config& config) {
    fs::path dir = AppDataDir();
    std::error_code ec;
    fs::create_directories(dir, ec);

    fs::path finalPath = ConfigPath();
    fs::path tmpPath = finalPath;
    tmpPath += L".tmp";

    {
        std::ofstream out(tmpPath);
        out << ToJson(config).dump(2);
        out.flush();
        // Disco cheio/erro de escrita: um .tmp incompleto não pode substituir
        // o config.json bom.
        if (!out) return;
    }
    // Escrita atômica: grava em arquivo temporário e só então substitui o
    // arquivo final, para nunca deixar um config.json truncado se o processo
    // for encerrado no meio da gravação.
    ::MoveFileExW(tmpPath.c_str(), finalPath.c_str(), MOVEFILE_REPLACE_EXISTING);
}

} // namespace storagecleaner
