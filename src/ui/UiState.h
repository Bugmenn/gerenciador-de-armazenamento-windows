#pragma once

#include "storagecleaner/Cleaner.h"
#include "storagecleaner/Config.h"
#include "storagecleaner/HistoryEntry.h"
#include "storagecleaner/ProgressState.h"
#include "storagecleaner/ScanResult.h"
#include "storagecleaner/Scanner.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace storagecleaner::ui {

// Todo o estado que a janela ImGui lê/escreve a cada frame. Fica de propósito
// fora dos structs "puros" em include/storagecleaner — scanner e cleaner não
// sabem nada sobre seleção de checkbox ou aba ativa.
struct UiState {
    // Config editada nas Configurações; só é persistida (ConfigStore::SaveConfig)
    // quando o usuário clica em "Salvar", para não gravar estado parcial/errado
    // enquanto ele ainda está digitando.
    Config config;

    // Varredura sob demanda (aba Dashboard/Resultados).
    // scanProgress precisa ser declarado antes de scanEngine: a destruição de
    // membros ocorre na ordem inversa da declaração, e ~ScanEngine() dá join()
    // na worker thread, que ainda referencia scanProgress enquanto roda — se
    // scanProgress fosse destruído primeiro, seria use-after-free.
    ProgressChannel scanProgress;
    ScanEngine scanEngine;
    ScanResult lastScanResult;
    // Um vetor de seleção paralelo a cada bucket de lastScanResult.
    std::vector<std::vector<bool>> selection;
    bool hasScanResult = false;

    // Limpeza (compartilha o mesmo canal de progresso de "Cleaning").
    std::thread cleanThread;
    std::atomic<bool> cleaning{false};
    std::atomic<bool> cleanCancel{false};
    ProgressChannel cleanProgress;
    std::mutex cleanResultMutex;
    std::optional<HistoryEntry> pendingCleanResult;
    bool showCleanConfirmModal = false;

    // Histórico (aba Histórico), recarregado ao abrir a aba.
    std::vector<HistoryEntry> history;
    std::atomic<bool> historyLoaded{false};

    // Segundo plano / bandeja. A varredura leve roda em worker thread própria
    // (mesmo padrão de scan/clean) porque, apesar do nome, ainda faz I/O
    // bloqueante (Shell API + varredura recursiva de %TEMP%) — rodar direto
    // em Tick() travaria a UI a cada intervalo (ver ScanEngine::RunLightBackgroundScan).
    std::thread lightScanThread;
    std::atomic<bool> lightScanInProgress{false};
    mutable std::mutex lightScanMutex;
    LightScanTotals lightTotals;
    std::chrono::steady_clock::time_point lastLightScan{};
    std::chrono::system_clock::time_point lastAutoCleanCheck{};
    bool windowVisible = true;
    bool requestExit = false;
    // true enquanto a varredura em andamento (ou a última concluída, até o
    // usuário agir) foi disparada pelo agendador de limpeza automática, e não
    // por um clique manual em "Escanear" — usado tanto para decidir se limpa
    // sozinho (modo AutoDelete) quanto para marcar o HistoryEntry resultante
    // como automático mesmo quando o usuário precisa confirmar manualmente
    // (modo AskConfirmation).
    bool currentScanIsAutoClean = false;
    // Armado logo antes de disparar uma limpeza automática (modo AutoDelete);
    // Tick() mostra um balão na bandeja quando ela terminar, já que nesse
    // fluxo o usuário pode nem estar olhando para a janela.
    bool autoCleanNotifyArmed = false;

    int activeTab = 0;

    void StartScan(bool triggeredByAutoClean = false);
    void StartClean(const std::vector<ScanItem>& selected, bool automatic);
    // Chamado uma vez por frame: absorve resultados prontos das worker
    // threads e roda o timer de agendamento. Mantém toda a lógica de estado
    // fora dos arquivos *Panel.cpp, que só desenham.
    void Tick();

    std::vector<ScanItem> CollectSelectedItems() const;

    // Thread-safe: lightTotals é escrito pela worker thread da varredura
    // leve (ver Tick()), então painéis de UI devem ler por aqui, nunca o
    // campo diretamente.
    LightScanTotals GetLightTotals() const {
        std::lock_guard<std::mutex> lock(lightScanMutex);
        return lightTotals;
    }

    ~UiState() {
        // Precisa parar as duas worker threads (scan e clean) ANTES que
        // qualquer membro comece a ser destruído: ambas capturam referências
        // a ProgressChannel/Config/etc. desta struct, e a ordem de
        // destruição de membros por si só não seria uma garantia segura o
        // suficiente (é frágil a reordenações futuras dos campos abaixo).
        scanEngine.StopAndWait();
        if (cleanThread.joinable()) {
            cleanCancel.store(true);
            cleanThread.join();
        }
        if (lightScanThread.joinable()) {
            lightScanThread.join();
        }
    }
};

} // namespace storagecleaner::ui
