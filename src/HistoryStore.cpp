#include "storagecleaner/HistoryStore.h"
#include "storagecleaner/ScanItem.h"
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

fs::path HistoryPath() { return AppDataDir() / L"history.json"; }

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

std::int64_t ToUnixSeconds(std::chrono::system_clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
}

std::chrono::system_clock::time_point FromUnixSeconds(std::int64_t seconds) {
    return std::chrono::system_clock::time_point(std::chrono::seconds(seconds));
}

json ToJson(const HistoryItemRecord& item) {
    json j;
    j["path"] = util::WideToUtf8(item.path);
    j["sizeBytes"] = item.sizeBytes;
    j["category"] = CategoryToKey(item.category);
    return j;
}

HistoryItemRecord ItemFromJson(const json& j) {
    HistoryItemRecord item;
    item.path = util::Utf8ToWide(j.value("path", std::string()));
    item.sizeBytes = j.value("sizeBytes", std::uint64_t{0});
    Category category = Category::TempFiles;
    CategoryFromKey(j.value("category", std::string()), category);
    item.category = category;
    return item;
}

json ToJson(const HistoryEntry& entry) {
    json j;
    j["timestamp"] = ToUnixSeconds(entry.timestamp);
    j["totalBytesFreed"] = entry.totalBytesFreed;
    j["success"] = entry.success;
    j["errorSummary"] = entry.errorSummary;
    j["wasAutomatic"] = entry.wasAutomatic;

    json byCategory = json::object();
    for (const auto& [category, bytes] : entry.bytesFreedByCategory)
        byCategory[CategoryToKey(category)] = bytes;
    j["bytesFreedByCategory"] = byCategory;

    for (const auto& item : entry.items) j["items"].push_back(ToJson(item));
    return j;
}

HistoryEntry EntryFromJson(const json& j) {
    HistoryEntry entry;
    entry.timestamp = FromUnixSeconds(j.value("timestamp", std::int64_t{0}));
    entry.totalBytesFreed = j.value("totalBytesFreed", std::uint64_t{0});
    entry.success = j.value("success", true);
    entry.errorSummary = j.value("errorSummary", std::string());
    entry.wasAutomatic = j.value("wasAutomatic", false);

    if (j.contains("bytesFreedByCategory")) {
        for (const auto& [key, value] : j["bytesFreedByCategory"].items()) {
            Category category;
            if (CategoryFromKey(key, category))
                entry.bytesFreedByCategory[category] = value.get<std::uint64_t>();
        }
    }
    if (j.contains("items"))
        for (const auto& item : j["items"]) entry.items.push_back(ItemFromJson(item));
    return entry;
}

} // namespace

std::vector<HistoryEntry> LoadHistory() {
    std::ifstream in(HistoryPath());
    if (!in.is_open()) return {};

    try {
        json j;
        in >> j;
        std::vector<HistoryEntry> entries;
        for (const auto& entryJson : j) entries.push_back(EntryFromJson(entryJson));
        return entries;
    } catch (const std::exception&) {
        return {};
    }
}

void AppendHistoryEntry(const HistoryEntry& entry) {
    std::vector<HistoryEntry> entries = LoadHistory();

    HistoryEntry capped = entry;
    if (capped.items.size() > HistoryEntry::kMaxItemsPerEntry)
        capped.items.resize(HistoryEntry::kMaxItemsPerEntry);
    entries.push_back(capped);

    json arr = json::array();
    for (const auto& e : entries) arr.push_back(ToJson(e));

    fs::path dir = AppDataDir();
    std::error_code ec;
    fs::create_directories(dir, ec);

    fs::path finalPath = HistoryPath();
    fs::path tmpPath = finalPath;
    tmpPath += L".tmp";
    {
        std::ofstream out(tmpPath);
        out << arr.dump(2);
    }
    ::MoveFileExW(tmpPath.c_str(), finalPath.c_str(), MOVEFILE_REPLACE_EXISTING);
}

std::optional<std::chrono::system_clock::time_point> LastAutoCleanTimestamp() {
    std::vector<HistoryEntry> entries = LoadHistory();
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        if (it->wasAutomatic) return it->timestamp;
    }
    return std::nullopt;
}

} // namespace storagecleaner
