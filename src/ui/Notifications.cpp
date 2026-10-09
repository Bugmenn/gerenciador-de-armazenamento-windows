#include "Notifications.h"

#include "TrayIcon.h"
#include "storagecleaner/Utils.h"

#include <chrono>
#include <deque>

namespace storagecleaner::ui {

namespace {

struct PendingBalloon {
    std::wstring title;
    std::wstring text;
};

// Só acessados pela UI thread (Tick e os Notify* chamados a partir dele).
std::deque<PendingBalloon> g_queue;
std::chrono::steady_clock::time_point g_lastShown{};

// Tempo para o usuário ler um balão antes de o próximo substituí-lo.
constexpr auto kMinInterval = std::chrono::seconds(8);

std::wstring Size(std::uint64_t bytes) { return util::Utf8ToWide(util::FormatSize(bytes)); }

void Enqueue(const std::wstring& titlePrefix, const wchar_t* title, const std::wstring& text) {
    g_queue.push_back({titlePrefix + title, text});
}

} // namespace

void NotifyScanDone(std::uint64_t bytesFound, bool scheduled, const std::wstring& titlePrefix) {
    const wchar_t* title = scheduled ? L"Varredura agendada concluída" : L"Varredura concluída";
    if (bytesFound == 0) {
        Enqueue(titlePrefix, title, L"Nada para limpar no momento.");
        return;
    }
    Enqueue(titlePrefix, title, Size(bytesFound) + L" encontrados para revisar. Clique aqui para abrir.");
}

void NotifyAutoCleanDone(std::uint64_t bytesFreed, bool dryRun, const std::wstring& titlePrefix) {
    std::wstring text =
        (dryRun ? L"Simulação - seria liberado: " : L"Espaço liberado: ") + Size(bytesFreed);
    Enqueue(titlePrefix, L"Limpeza automática concluída", text);
}

void NotifyLowDisk(const std::wstring& drivesText, const std::wstring& titlePrefix) {
    Enqueue(titlePrefix, L"Pouco espaço em disco", drivesText);
}

std::wstring LowDiskDriveText(const std::wstring& root, std::uint64_t freeBytes, int freePercent) {
    return root + L" com " + Size(freeBytes) + L" livres (" + std::to_wstring(freePercent) + L"%).";
}

void PumpNotifications() {
    if (g_queue.empty()) return;
    auto now = std::chrono::steady_clock::now();
    if (g_lastShown.time_since_epoch().count() != 0 && now - g_lastShown < kMinInterval) return;
    const PendingBalloon& next = g_queue.front();
    ShowTrayBalloon(next.title.c_str(), next.text.c_str());
    g_queue.pop_front();
    g_lastShown = now;
}

} // namespace storagecleaner::ui
