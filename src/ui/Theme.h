#pragma once

#include <imgui.h>

namespace storagecleaner::ui {

// Paleta central da UI — unica fonte de verdade para cores, derivada do
// icone do app (resources/app.ico: disco azul em gradiente com sparkle
// dourado). Substitui os ImVec4 hardcoded que antes viviam soltos em cada
// painel (Dashboard/Results/History), cada um com um tom ligeiramente
// diferente para o mesmo significado (sucesso/aviso/erro).
namespace theme {

inline constexpr ImVec4 kBg          = ImVec4(0x12 / 255.f, 0x18 / 255.f, 0x1F / 255.f, 1.f);
inline constexpr ImVec4 kSurface     = ImVec4(0x1B / 255.f, 0x23 / 255.f, 0x2D / 255.f, 1.f);
inline constexpr ImVec4 kBorder      = ImVec4(0x2A / 255.f, 0x35 / 255.f, 0x42 / 255.f, 1.f);
inline constexpr ImVec4 kTextPrimary = ImVec4(0xE8 / 255.f, 0xED / 255.f, 0xF2 / 255.f, 1.f);
inline constexpr ImVec4 kTextMuted   = ImVec4(0x8A / 255.f, 0x97 / 255.f, 0xA6 / 255.f, 1.f);
inline constexpr ImVec4 kAccentBlue  = ImVec4(0x4F / 255.f, 0xA3 / 255.f, 0xF7 / 255.f, 1.f);
inline constexpr ImVec4 kAccentGold  = ImVec4(0xFF / 255.f, 0xD5 / 255.f, 0x4F / 255.f, 1.f);
inline constexpr ImVec4 kSuccess     = ImVec4(0x4C / 255.f, 0xAF / 255.f, 0x80 / 255.f, 1.f);
inline constexpr ImVec4 kWarning     = ImVec4(0xE8 / 255.f, 0xA3 / 255.f, 0x3D / 255.f, 1.f);
inline constexpr ImVec4 kDanger      = ImVec4(0xE5 / 255.f, 0x60 / 255.f, 0x5A / 255.f, 1.f);

} // namespace theme

// Fontes carregadas uma unica vez em LoadFonts(); os ponteiros ficam
// validos pelo tempo de vida do contexto ImGui (o atlas nao e reconstruido
// em runtime). `body` e a fonte padrao do app; `hero` so e usada via
// Push/PopFont, no titulo/numero de destaque.
struct AppFonts {
    ImFont* body = nullptr;
    ImFont* hero = nullptr;
};

// Seta ImGui::GetStyle().Colors[...] a partir da paleta acima e ajusta
// espacamento/arredondamento. Chamar uma unica vez, logo apos
// StyleColorsDark() (serve de base para os slots que este tema nao
// sobrescreve) e antes do primeiro NewFrame().
void ApplyTheme();

// Resolve segoeui.ttf/seguisb.ttf em %WINDIR%\Fonts e adiciona ao atlas de
// `io`. Usa a fonte nativa do Windows em vez de bundlar uma fonte externa —
// e a escolha certa para um utilitario nativo do sistema, e evita
// distribuir/licenciar um arquivo de fonte. Se algum arquivo nao existir
// (cenario raro), cai em io.Fonts->AddFontDefault() para aquele slot sem
// abortar o app.
void LoadFonts(ImGuiIO& io, AppFonts& outFonts);

// Gauge circular customizado (elemento de assinatura da UI, substitui
// ImGui::ProgressBar nos pontos de progresso de scan/clean): desenha um
// trilho de fundo (circulo completo em `trackColor`) e um arco de progresso
// de `fraction` (0..1, clampado internamente) com gradiente entre
// `colorStart` e `colorEnd` — PathStroke nao suporta gradiente nativo por
// segmento, entao o arco e desenhado em varios sub-segmentos com cor
// interpolada. Nao desenha nenhum texto/label; quem chama sobrepoe o que
// quiser no centro. `drawList` default usa a janela atual.
void DrawCircularGauge(ImVec2 center, float radius, float thickness, float fraction,
                       ImU32 colorStart, ImU32 colorEnd, ImU32 trackColor,
                       ImDrawList* drawList = nullptr);

// Conveniencia sobre DrawCircularGauge para uso em fluxo normal de widgets:
// calcula o centro a partir do cursor atual do ImGui e reserva o espaco no
// layout vertical via ImGui::Dummy (o desenho em si e via ImDrawList, que
// nao avanca o cursor sozinho). Usado nos pontos de progresso de scan/clean
// no lugar de ImGui::ProgressBar.
void DrawInlineCircularGauge(float radius, float thickness, float fraction,
                             ImU32 colorStart, ImU32 colorEnd, ImU32 trackColor);

} // namespace storagecleaner::ui
