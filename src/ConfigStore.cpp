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

// Categorias precisam ser chaves de string em JSON (mapas em nlohmann::json
// exigem chave textual) — serializamos pelo nome legível já existente para
// manter o arquivo humano/depurável, com um lookup reverso para desserializar.
std::string CategoryToKey(Category c) { return CategoryName(c); }

bool CategoryFromKey(const std::string& key, Category& out) {
    for (Category c : kAllCategories) {
        if (CategoryToKey(c) == key) {
            out = c;
            return true;
        }
    }
    return false;
}

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
    j["oldLogsThresholdDays"] = config.oldLogsThresholdDays;
    j["restorePointsToKeep"] = config.restorePointsToKeep;
    j["orphanedAppsInactivityDays"] = config.orphanedAppsInactivityDays;
    for (const auto& p : config.duplicateScanRoots)
        j["duplicateScanRoots"].push_back(util::WideToUtf8(p));

    json perCategory = json::object();
    for (const auto& [category, cc] : config.perCategory)
        perCategory[CategoryToKey(category)] = ToJson(cc);
    j["perCategory"] = perCategory;

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

Config LoadConfig() {
    fs::path path = ConfigPath();
    std::ifstream in(path);
    if (!in.is_open()) return Config::Defaults();

    try {
        json j;
        in >> j;
        return ConfigFromJson(j);
    } catch (const std::exception&) {
        // Arquivo corrompido ou de uma versão incompatível: preferimos cair
        // para os padrões a impedir o app de abrir.
        return Config::Defaults();
    }
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
    }
    // Escrita atômica: grava em arquivo temporário e só então substitui o
    // arquivo final, para nunca deixar um config.json truncado se o processo
    // for encerrado no meio da gravação.
    ::MoveFileExW(tmpPath.c_str(), finalPath.c_str(), MOVEFILE_REPLACE_EXISTING);
}

} // namespace storagecleaner
