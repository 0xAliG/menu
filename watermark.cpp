// ============================================================================
// watermark.cpp — see watermark.h.
//
// Style refresh vs the original rust panel:
//   * bgRound dropped from S(6) → S(3) so the box matches the rest of the
//     menu's "barely-rounded" look (containers, popups, sub-frames).
//   * Accent stripe moved from the TOP edge to the BOTTOM edge — clip rect
//     covers the bottom bgRound band, so AddRect's rounded outline only
//     paints the bottom edge + the two bottom corner arcs.
//   * Right-anchored layout, opaque dark fill, hairline gray vertical
//     separators between segments (Border color @ ~55% alpha).
// ============================================================================

#include "watermark.h"
#include "menu_style.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Watermark {

namespace {
    // Vertical-line separator metrics. Same total slot width as the old
    // dot separator so the visual rhythm is unchanged; only the mark
    // itself is now a 1 px tall gray line instead of an accent-colored dot.
    constexpr float kSepPad    = 7.0f;   // padding either side of the line
    constexpr float kSepLineW  = 1.0f;   // stroke thickness
    constexpr float kSepHeight = 10.0f;  // total line height inside the box
    inline float sep_slot_w() {
        return MenuStyle::S(kSepPad * 2.0f + kSepLineW);
    }

    // Per-frame right-edge gap + vertical position. Right-anchored: slot 0 is
    // the gap to the screen's right edge (NOT an absolute X), slot 1 is the
    // absolute Y. Hidden inside this TU — the demo build doesn't need it
    // exposed.
    float s_pos[2] = { -1.0f, -1.0f };
}

void Render(const char* clientName, const char* statusText)
{
    using namespace MenuStyle;

    const float padY    = S(7.0f);
    const float padX    = S(12.0f);
    // Watermark is a hard rectangle (no rounding) — the accent stripe sits
    // flush against the TOP edge as the visual anchor.
    const float bgRound = 0.0f;

    ImFont* bodyFont = MenuStyle::g_fontBold ? MenuStyle::g_fontBold : ImGui::GetFont();
    const float bodyFontSize = ImGui::GetFontSize();

    // Body: "paradox • <fps>fps [• <client>] • <status>". Caller owns
    // identity ("paradox" stays fixed) and the FPS source (ImGui's
    // smoothed framerate — same value everywhere, no per-client publisher
    // needed).
    char fpsBuf[24];
    std::snprintf(fpsBuf, sizeof(fpsBuf), "%dfps",
                  (int)(ImGui::GetIO().Framerate + 0.5f));
    const char* identityText = "paradox";
    if (!statusText || !statusText[0]) statusText = "active";

    auto measureBody = [&](const char* s) {
        return bodyFont->CalcTextSizeA(bodyFontSize, FLT_MAX, 0.0f, s).x;
    };
    const float sepW = sep_slot_w();

    float bodyW = measureBody(identityText)
                + sepW
                + measureBody(fpsBuf);
    if (clientName && clientName[0]) {
        bodyW += sepW + measureBody(clientName);
    }
    bodyW += sepW + measureBody(statusText);

    const float boxW = bodyW + padX * 2.0f;
    const float boxH = bodyFontSize + padY * 2.0f;

    // Clamp stale positions so the box can never end up off-screen if a
    // future settings file gets carried over from an older build.
    ImGuiIO& io = ImGui::GetIO();
    if (s_pos[0] < 0.0f || s_pos[0] > 200.0f) s_pos[0] = S(8.0f);
    if (s_pos[1] < 0.0f)                       s_pos[1] = S(8.0f);

    const float rightMargin = s_pos[0];
    const float anchorX     = io.DisplaySize.x - boxW - rightMargin;
    const float maxY        = io.DisplaySize.y - boxH - S(2.0f);
    const float anchorY     = std::clamp(s_pos[1],
                                          S(2.0f),
                                          maxY > S(2.0f) ? maxY : S(2.0f));

    // ImGui's window clip rect is exactly [winPos, winPos+size]. A 1 px
    // border drawn at the very right/bottom edges loses its outer AA pixel
    // to that clip — visually reads as a "cut off" gray outline on the
    // right side. Pad the window by 1 px on right + bottom so the border
    // has room to AA cleanly. The painted box stays at the original boxW
    // × boxH; only the window's clip rect grows.
    ImGui::SetNextWindowPos(ImVec2(anchorX, anchorY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(boxW + 1.0f, boxH + 1.0f), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(S(8.0f), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
      | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
      | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing
      | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs;

    if (ImGui::Begin("##pdx_watermark", nullptr, flags)) {
        ImDrawList* dl     = ImGui::GetWindowDrawList();
        ImVec2      winPos = ImGui::GetWindowPos();

        // Dark opaque fill + 1 px border — matches the menu's PanelBg /
        // Border colours so the watermark visually belongs to the chrome.
        ImVec2 bgMin = winPos, bgMax(winPos.x + boxW, winPos.y + boxH);
        dl->AddRectFilled(bgMin, bgMax, MenuStyle::Panel::Bg.u32(),     bgRound);
        dl->AddRect      (bgMin, bgMax, MenuStyle::Panel::Border.u32(), bgRound, 0, 1.0f);

        // Vertical centering — pin separator midline to box mid, drop the
        // text baseline so its x-height midline lands on that same line.
        // Pure geometric centering looks visually high because Tahoma's
        // ascender region is taller than its descender, so the painted
        // glyphs sit BELOW the line-box centre.
        ImFontBaked* baked = bodyFont->GetFontBaked(bodyFontSize);
        const float ascent = baked ? baked->Ascent : bodyFontSize;
        const float midY   = winPos.y + boxH * 0.5f;
        const float bodyY  = midY - ascent * 0.75f;
        float       x      = winPos.x + padX;
        const ImU32 white  = MenuStyle::Colors::White.u32();
        // Border color — matches the watermark's outer 1 px frame so the
        // separator reads as part of the same chrome.
        const ImU32 sepCol = MenuStyle::Colors::Border.u32();
        const float sepPad  = S(kSepPad);
        const float sepLineW = S(kSepLineW);
        const float sepH    = S(kSepHeight);

        auto drawText = [&](const char* s) {
            MenuStyle::TextShadow(dl, bodyFont, bodyFontSize,
                                  ImVec2(x, bodyY), white, s);
            x += measureBody(s);
        };
        auto drawSep = [&]() {
            x += sepPad;
            float lineX = std::floor(x + 0.5f);
            float lineY0 = std::floor(midY - sepH * 0.5f + 0.5f);
            float lineY1 = lineY0 + sepH;
            dl->AddRectFilled(ImVec2(lineX, lineY0),
                              ImVec2(lineX + sepLineW, lineY1),
                              sepCol);
            x += sepLineW + sepPad;
        };

        drawText(identityText);
        drawSep();
        drawText(fpsBuf);
        if (clientName && clientName[0]) {
            drawSep();
            drawText(clientName);
        }
        drawSep();
        drawText(statusText);

        // Accent stripe — 1 px tall inside the border, just under the
        // 1 px gray outline. Inset 1 px on the left and 2 px on the
        // right. Left-to-right transparent→Accent→transparent gradient,
        // matching the main menu and debug windows.
        {
            const ImU32 accent = MenuStyle::Colors::Accent.u32();
            const ImU32 clear  = MenuStyle::Colors::Accent.alpha(0.0f).u32();
            float x0 = bgMin.x + 1.0f;
            float x1 = bgMax.x - 2.0f;
            float y0 = bgMin.y + 1.0f;
            float y1 = y0 + 1.0f;
            dl->AddRectFilledMultiColor(
                ImVec2(x0, y0), ImVec2(x1, y1),
                accent, clear, clear, accent);
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

} // namespace Watermark
