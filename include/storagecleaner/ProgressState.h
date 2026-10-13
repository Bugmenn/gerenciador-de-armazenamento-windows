#pragma once

#include <cstdint>
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
    ScanningWindowsUpdate,
    ScanningCrashDumps,
    ScanningThumbnails,
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
    // Fração da etapa atual (0..1). A maioria dos scanners não sabe o total
    // de antemão e só a completa no fim; o progresso geral vem das etapas.
    float fractionComplete = 0.0f;
    std::string lastError;
    // Etapa atual de uma operação com várias (ex.: categoria 3 de 9). Gravadas
    // por ProgressChannel::SetStage e preservadas por Update().
    int stageIndex = 0;
    int stageCount = 0;
};

// Progresso geral 0..1: etapas concluídas + a fração da etapa atual. Sem
// etapas definidas, é a própria fração. Livre de Win32 (tests/test_progress.cpp).
inline float OverallFraction(const ProgressSnapshot& s) {
    float stage = s.fractionComplete < 0.0f ? 0.0f : (s.fractionComplete > 1.0f ? 1.0f : s.fractionComplete);
    if (s.stageCount <= 0) return stage;
    int index = s.stageIndex < 0 ? 0 : (s.stageIndex >= s.stageCount ? s.stageCount - 1 : s.stageIndex);
    return (static_cast<float>(index) + stage) / static_cast<float>(s.stageCount);
}

// Canal de comunicação entre a worker thread (scan/clean) e a thread de UI.
// Deliberadamente simples: mutex + snapshot copiado uma vez por frame, em vez
// de uma fila lock-free — a taxa de atualização (poucas vezes por segundo) não
// justifica a complexidade extra, e mutex+cópia é trivial de revisar.
class ProgressChannel {
public:
    // Os scanners criam um snapshot novo a cada etapa sem saber em qual etapa
    // estão; quem orquestra (ScanEngine, Cleaner) grava a etapa aqui, e
    // Update() a mantém.
    void Update(const ProgressSnapshot& snapshot) {
        std::lock_guard<std::mutex> lock(mutex_);
        int index = snapshot_.stageIndex, count = snapshot_.stageCount;
        snapshot_ = snapshot;
        snapshot_.stageIndex = index;
        snapshot_.stageCount = count;
    }

    void SetStage(int index, int count) {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.stageIndex = index;
        snapshot_.stageCount = count;
        snapshot_.fractionComplete = 0.0f; // a fração anterior era da etapa passada
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
