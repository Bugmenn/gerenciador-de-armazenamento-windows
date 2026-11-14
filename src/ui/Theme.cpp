#include "Theme.h"

#include "storagecleaner/Utils.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace storagecleaner::ui {

namespace {

constexpr float kTau = 6.28318530717958647692f;
constexpr float kStartAngle = -kTau * 0.25f; // 12h, arco cresce em sentido horario

ImU32 LerpColorU32(ImU32 a, ImU32 b, float t) {
    ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
    ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
    ImVec4 result(ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t, ca.z + (cb.z - ca.z) * t,
                 ca.w + (cb.w - ca.w) * t);
    return ImGui::ColorConvertFloat4ToU32(result);
}

// ImVec4 com o mesmo RGB de `c` mas alpha customizado — usado para derivar
// os estados hover/active dos widgets a partir das mesmas 3 cores base
// (accentBlue/surface/border), em vez de declarar uma constante por estado.
ImVec4 WithAlpha(const ImVec4& c, float alpha) { return ImVec4(c.x, c.y, c.z, alpha); }

} // namespace

void ApplyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowPadding = ImVec2(14, 14);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 8);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.WindowRounding = 8.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = theme::kBg;
    colors[ImGuiCol_ChildBg] = theme::kSurface;
    colors[ImGuiCol_PopupBg] = theme::kSurface;
    colors[ImGuiCol_Border] = theme::kBorder;
    colors[ImGuiCol_Text] = theme::kTextPrimary;
    colors[ImGuiCol_TextDisabled] = theme::kTextMuted;

    // Mais claro que kSurface de proposito: checkboxes/inputs precisam se
    // destacar do fundo dos paineis/linhas de tabela (que usam kSurface), ou
    // fica dificil perceber que existe uma caixa clicavel ali.
    colors[ImGuiCol_FrameBg] = theme::kBorder;
    colors[ImGuiCol_FrameBgHovered] = WithAlpha(theme::kAccentBlue, 0.25f);
    colors[ImGuiCol_FrameBgActive] = WithAlpha(theme::kAccentBlue, 0.35f);

    colors[ImGuiCol_TitleBg] = theme::kSurface;
    colors[ImGuiCol_TitleBgActive] = theme::kSurface;

    colors[ImGuiCol_CheckMark] = theme::kAccentBlue;
    colors[ImGuiCol_SliderGrab] = theme::kAccentBlue;
    colors[ImGuiCol_SliderGrabActive] = theme::kAccentGold;

    colors[ImGuiCol_Button] = WithAlpha(theme::kAccentBlue, 0.55f);
    colors[ImGuiCol_ButtonHovered] = WithAlpha(theme::kAccentBlue, 0.75f);
    colors[ImGuiCol_ButtonActive] = theme::kAccentBlue;

    colors[ImGuiCol_Header] = WithAlpha(theme::kAccentBlue, 0.30f);
    colors[ImGuiCol_HeaderHovered] = WithAlpha(theme::kAccentBlue, 0.45f);
    colors[ImGuiCol_HeaderActive] = WithAlpha(theme::kAccentBlue, 0.55f);

    colors[ImGuiCol_Separator] = theme::kBorder;
    colors[ImGuiCol_SeparatorHovered] = theme::kAccentBlue;
    colors[ImGuiCol_SeparatorActive] = theme::kAccentBlue;

    colors[ImGuiCol_ResizeGrip] = WithAlpha(theme::kAccentBlue, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = WithAlpha(theme::kAccentBlue, 0.55f);
    colors[ImGuiCol_ResizeGripActive] = theme::kAccentBlue;

    colors[ImGuiCol_Tab] = theme::kSurface;
    colors[ImGuiCol_TabHovered] = WithAlpha(theme::kAccentBlue, 0.55f);
    colors[ImGuiCol_TabActive] = WithAlpha(theme::kAccentBlue, 0.65f);
    colors[ImGuiCol_TabUnfocused] = theme::kSurface;
    colors[ImGuiCol_TabUnfocusedActive] = WithAlpha(theme::kAccentBlue, 0.45f);

    colors[ImGuiCol_TableHeaderBg] = theme::kSurface;
    colors[ImGuiCol_TableBorderStrong] = theme::kBorder;
    colors[ImGuiCol_TableBorderLight] = theme::kBorder;
    colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt] = WithAlpha(theme::kSurface, 0.5f);

    colors[ImGuiCol_ScrollbarBg] = theme::kBg;
    colors[ImGuiCol_ScrollbarGrab] = theme::kBorder;
    colors[ImGuiCol_ScrollbarGrabHovered] = theme::kAccentBlue;
    colors[ImGuiCol_ScrollbarGrabActive] = theme::kAccentBlue;
}

void LoadFonts(ImGuiIO& io, AppFonts& outFonts) {
    wchar_t winDir[MAX_PATH];
    UINT len = ::GetWindowsDirectoryW(winDir, MAX_PATH);

    auto resolve = [&](const wchar_t* fileName) -> std::wstring {
        if (len == 0 || len >= MAX_PATH) return L"";
        return std::wstring(winDir) + L"\\Fonts\\" + fileName;
    };
    auto fileExists = [](const std::wstring& path) {
        return !path.empty() && ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    };

    std::wstring bodyPath = resolve(L"segoeui.ttf");
    std::wstring heroPath = resolve(L"seguisb.ttf");

    outFonts.body = fileExists(bodyPath)
                        ? io.Fonts->AddFontFromFileTTF(util::WideToUtf8(bodyPath).c_str(), 17.0f)
                        : nullptr;
    if (!outFonts.body) outFonts.body = io.Fonts->AddFontDefault();

    outFonts.hero = fileExists(heroPath)
                        ? io.Fonts->AddFontFromFileTTF(util::WideToUtf8(heroPath).c_str(), 28.0f)
                        : nullptr;
    // Sem Semibold disponivel: reaproveita a fonte body (sempre nao-nula) em
    // vez de crescer o atlas com mais uma fonte default redundante.
    if (!outFonts.hero) outFonts.hero = outFonts.body;
}

void DrawCircularGauge(ImVec2 center, float radius, float thickness, float fraction,
                       ImU32 colorStart, ImU32 colorEnd, ImU32 trackColor,
                       ImDrawList* drawList) {
    if (!drawList) drawList = ImGui::GetWindowDrawList();
    if (fraction < 0.f) fraction = 0.f;
    if (fraction > 1.f) fraction = 1.f;

    constexpr int kTotalSegments = 48;
    drawList->AddCircle(center, radius, trackColor, 64, thickness);

    int segCount = static_cast<int>(kTotalSegments * fraction + 0.5f);
    if (segCount <= 0) return;

    float anglePerSeg = kTau / kTotalSegments;
    for (int i = 0; i < segCount; ++i) {
        float a0 = kStartAngle + i * anglePerSeg;
        float a1 = kStartAngle + (i + 1) * anglePerSeg;
        ImU32 segColor = LerpColorU32(colorStart, colorEnd, (i + 0.5f) / kTotalSegments);
        drawList->PathArcTo(center, radius, a0, a1, 6);
        drawList->PathStroke(segColor, 0, thickness);
    }
}

void DrawInlineCircularGauge(float radius, float thickness, float fraction,
                             ImU32 colorStart, ImU32 colorEnd, ImU32 trackColor,
                             const char* centerText) {
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImVec2 center(cursor.x + radius, cursor.y + radius);
    DrawCircularGauge(center, radius, thickness, fraction, colorStart, colorEnd, trackColor);
    if (centerText) {
        ImVec2 size = ImGui::CalcTextSize(centerText);
        ImGui::GetWindowDrawList()->AddText(ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f),
                                            ImGui::GetColorU32(ImGuiCol_Text), centerText);
    }
    ImGui::Dummy(ImVec2(radius * 2.0f, radius * 2.0f));
}

int DrawSegmentedDonut(ImVec2 center, float radius, float thickness, const DonutSlice* slices,
                       int count, ImU32 trackColor, ImDrawList* drawList) {
    if (!drawList) drawList = ImGui::GetWindowDrawList();
    drawList->AddCircle(center, radius, trackColor, 64, thickness);

    double total = 0.0;
    int nonZero = 0;
    for (int i = 0; i < count; ++i) {
        total += static_cast<double>(slices[i].value);
        if (slices[i].value > 0) ++nonZero;
    }
    if (nonZero == 0) return -1;

    const float gap = nonZero > 1 ? 0.035f : 0.0f;
    const float minAngle = 0.08f;
    std::vector<float> angles(count, 0.0f);
    float stolen = 0.0f, bigTotal = 0.0f;
    for (int i = 0; i < count; ++i) {
        if (slices[i].value == 0) continue;
        angles[i] = static_cast<float>(slices[i].value / total) * kTau;
        if (angles[i] < minAngle) {
            stolen += minAngle - angles[i];
            angles[i] = minAngle;
        } else {
            bigTotal += angles[i];
        }
    }
    // O arco extra das fatias minúsculas sai proporcionalmente das maiores,
    // para a soma continuar uma volta completa.
    if (stolen > 0.0f && bigTotal > 0.0f)
        for (int i = 0; i < count; ++i)
            if (slices[i].value > 0 && angles[i] > minAngle) angles[i] -= stolen * angles[i] / bigTotal;

    int hovered = -1;
    if (ImGui::IsWindowHovered()) {
        ImVec2 mouse = ImGui::GetIO().MousePos;
        float dx = mouse.x - center.x, dy = mouse.y - center.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist >= radius - thickness * 0.5f - 4.0f && dist <= radius + thickness * 0.5f + 4.0f) {
            float a = std::atan2(dy, dx) - kStartAngle;
            while (a < 0.0f) a += kTau;
            while (a >= kTau) a -= kTau;
            float acc = 0.0f;
            for (int i = 0; i < count; ++i) {
                if (slices[i].value == 0) continue;
                if (a < acc + angles[i]) {
                    hovered = i;
                    break;
                }
                acc += angles[i];
            }
        }
    }

    float start = kStartAngle;
    for (int i = 0; i < count; ++i) {
        if (slices[i].value == 0) continue;
        float a0 = start + gap * 0.5f;
        float a1 = start + angles[i] - gap * 0.5f;
        start += angles[i];
        if (a1 <= a0) a1 = a0 + 0.01f;
        int segments = std::max(2, static_cast<int>((a1 - a0) / kTau * 96.0f));
        drawList->PathArcTo(center, radius, a0, a1, segments);
        drawList->PathStroke(slices[i].color, 0, i == hovered ? thickness * 1.4f : thickness);
    }
    return hovered;
}

int DrawInlineSegmentedDonut(float radius, float thickness, const DonutSlice* slices, int count,
                             ImU32 trackColor, ImVec2* outCenter) {
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    // Meia espessura da fatia em destaque (1.4x) + 1 px: sem isso o anel
    // sai cortado na borda da janela/child.
    float pad = thickness * 0.7f + 1.0f;
    ImVec2 center(cursor.x + radius + pad, cursor.y + radius + pad);
    int hovered = DrawSegmentedDonut(center, radius, thickness, slices, count, trackColor);
    if (outCenter) *outCenter = center;
    ImGui::Dummy(ImVec2((radius + pad) * 2.0f, (radius + pad) * 2.0f));
    return hovered;
}

void DrawProgressRing(const ProgressSnapshot& snapshot) {
    float overall = OverallFraction(snapshot);
    char percent[8];
    std::snprintf(percent, sizeof(percent), "%d%%", static_cast<int>(overall * 100.0f));
    DrawInlineCircularGauge(28.0f, 6.0f, overall, ImGui::ColorConvertFloat4ToU32(theme::kAccentBlue),
                            ImGui::ColorConvertFloat4ToU32(theme::kAccentGold),
                            ImGui::ColorConvertFloat4ToU32(theme::kBorder), percent);
    if (snapshot.stagesDone >= 0 && snapshot.stageCount > 0)
        ImGui::Text("%d de %d categorias concluídas - %s", snapshot.stagesDone, snapshot.stageCount,
                    PhaseLabel(snapshot.phase));
    else if (snapshot.stageCount > 0)
        ImGui::Text("Etapa %d de %d - %s", snapshot.stageIndex + 1, snapshot.stageCount,
                    PhaseLabel(snapshot.phase));
    else
        ImGui::TextUnformatted(PhaseLabel(snapshot.phase));
}

} // namespace storagecleaner::ui
