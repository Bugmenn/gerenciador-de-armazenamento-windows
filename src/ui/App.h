#pragma once

namespace storagecleaner::ui {

// Ponto de entrada da UI. `startMinimized` é usado quando o app é iniciado
// junto com o Windows (auto-inicio) — abre direto na bandeja, sem mostrar a
// janela, para não interromper o login do usuário com uma janela inesperada.
// `dryRun` (--dry-run) liga o modo simulação: nenhuma limpeza exclui nada.
// `testBalloons` (--test-balloons) mostra cada tipo de balão da bandeja em
// sequência, para conferir que aparecem.
int RunApp(bool startMinimized, bool dryRun, bool testBalloons);

} // namespace storagecleaner::ui
