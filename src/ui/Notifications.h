#pragma once

#include <cstdint>
#include <string>

namespace storagecleaner::ui {

// Todos os balões da bandeja, um por tipo. Ficam num lugar só para que o modo
// --test-balloons mostre exatamente os mesmos textos dos fluxos reais.
// `titlePrefix` só é usado pelo modo de teste (ex.: L"[Teste 1/6] ").
//
// Não mostram na hora: entram numa fila que PumpNotifications() esvazia com
// um intervalo mínimo entre balões, porque um Shell_NotifyIcon substitui o
// balão anterior — dois avisos próximos fariam o primeiro sumir.

// Fim de uma varredura. `scheduled` = disparada pelo agendamento.
void NotifyScanDone(std::uint64_t bytesFound, bool scheduled, const std::wstring& titlePrefix = L"");

void NotifyAutoCleanDone(std::uint64_t bytesFreed, bool dryRun, const std::wstring& titlePrefix = L"");

// `drivesText`: um ou mais LowDiskDriveText() separados por espaço.
void NotifyLowDisk(const std::wstring& drivesText, const std::wstring& titlePrefix = L"");

// Trecho de uma unidade no balão de pouco espaço, ex.: "C:\ com 9.50 GB livres (4%)."
std::wstring LowDiskDriveText(const std::wstring& root, std::uint64_t freeBytes, int freePercent);

// Chamada uma vez por frame (UiState::Tick): mostra o próximo balão da fila
// se o intervalo mínimo desde o anterior já passou.
void PumpNotifications();

} // namespace storagecleaner::ui
