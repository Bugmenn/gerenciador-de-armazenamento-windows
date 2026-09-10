#pragma once

namespace storagecleaner::ui {

// Ponto de entrada da UI. `startMinimized` é usado quando o app é iniciado
// junto com o Windows (auto-inicio) — abre direto na bandeja, sem mostrar a
// janela, para não interromper o login do usuário com uma janela inesperada.
int RunApp(bool startMinimized);

} // namespace storagecleaner::ui
