#pragma once

#include <mutex>
#include <optional>
#include <string>

namespace storagecleaner {

enum class Phase {
    Idle,
    ScanningTemp,
    ScanningBrowserCache,
    ScanningDuplicates,
    ScanningLogs,
    ScanningRecycleBin,
    ScanningRestorePoints,
    ScanningOrphanedApps,
    Cleaning,
    Done,
    Cancelled,
    Error,
};

const char* PhaseLabel(Phase phase);

struct ProgressSnapshot {
    Phase phase = Phase::Idle;
    std::wstring currentItem;
    std::uint64_t itemsProcessed = 0;
    std::uint64_t bytesFoundSoFar = 0;
    float fractionComplete = 0.0f;
    std::string lastError;
};

// Canal de comunicação entre a worker thread (scan/clean) e a thread de UI.
// Deliberadamente simples: mutex + snapshot copiado uma vez por frame, em vez
// de uma fila lock-free — a taxa de atualização (poucas vezes por segundo) não
// justifica a complexidade extra, e mutex+cópia é trivial de revisar.
class ProgressChannel {
public:
    void Update(const ProgressSnapshot& snapshot) {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = snapshot;
    }

    ProgressSnapshot Read() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshot_;
    }

private:
    mutable std::mutex mutex_;
    ProgressSnapshot snapshot_;
};

} // namespace storagecleaner
