#pragma once

#include <string>

namespace storagecleaner::ui {

// Ponto de entrada da UI. `startMinimized` é usado quando o app é iniciado
// junto com o Windows (auto-inicio) — abre direto na bandeja, sem mostrar a
// janela, para não interromper o login do usuário com uma janela inesperada.
// `dryRun` (--dry-run) liga o modo simulação: nenhuma limpeza exclui nada.
// `testBalloons` (--test-balloons) mostra cada tipo de balão da bandeja em
// sequência, para conferir que aparecem.
// `selfTestDir` não vazio (--ui-selftest <pasta>) roda o roteiro automático
// da UI (SelfTest.h) com dados fictícios e devolve o nº de falhas.
int RunApp(bool startMinimized, bool dryRun, bool testBalloons, const std::wstring& selfTestDir = L"");

} // namespace storagecleaner::ui
