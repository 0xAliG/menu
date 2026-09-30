#include "notifications.h"
#include "menu_style.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#define S(x) (MenuStyle::S(x))

namespace Notifications {

struct Toast {
    Type        type;
    std::string text;
    float       duration;   // total visible time (excluding fade out)
    float       elapsed;    // time since spawn
    float       targetY;    // animated stacking offset
    float       currentY;
};

static std::vector<Toast> s_toasts;

static const float kFadeIn  = 0.18f;
static const float kFadeOut = 0.30f;

// FA icons (4-char glyph sequences in UTF-8)
static const char* IconForType(Type t) {
    switch (t) {
    case Type::Info:    return "\xef\x81\x9a"; // info-circle f05a
    case Type::Success: return "\xef\x81\x98"; // check-circle f058
    case Type::Warning: return "\xef\x81\xb1"; // triangle-exclamation f071
    case Type::Error:   return "\xef\x81\x97"; // circle-xmark f057
    }
    return "\xef\x81\x9a";
}

static MenuStyle::Color AccentForType(Type t) {
    switch (t) {
    case Type::Info:    return MenuStyle::Colors::NotifInfo;
    case Type::Success: return MenuStyle::Colors::Accent;
    case Type::Warning: return MenuStyle::Colors::NotifWarning;
    case Type::Error:   return MenuStyle::Colors::NotifError;
    }
    return MenuStyle::Colors::NotifInfo;
}

static const char* TitleForType(Type t) {
    switch (t) {
    case Type::Info:    return "info";
    case Type::Success: return "success";
    case Type::Warning: return "warning";
    case Type::Error:   return "error";
    }
    return "info";
}

void Push(Type type, const std::string& text, float duration) {
    Toast t;
    t.type     = type;
    t.text     = text;
    t.duration = duration;
    t.elapsed  = 0.0f;
    t.targetY  = 0.0f;
    t.currentY = 0.0f;
    s_toasts.push_back(std::move(t));
}

void Clear() {
    s_toasts.clear();
}

void Render() {
    if (s_toasts.empty()) return;

    float dt = ImGui::GetIO().DeltaTime;

    // Layout — mirrors UI::BeginContainer / EndContainer so notifications
    // read as the same design language as the menu's containers:
    //   • hard rectangles, zero rounding
    //   • title bar S(26) tall, SecondaryBg → PanelBg gradient with a
    //     Border@20% overlay (identical formula to the container header)
    //   • body = PanelBg (MenuDark), padSide (S(10)) internal padding on
    //     every edge
    //   • gray Border outline wrapped by an outer black (Ink) silhouette
    const float toastW    = S(280.0f);
    const float padSide   = S(10.0f);      // matches container padSide
    const float titleBarH = S(26.0f);      // matches container titleBarH
    const float gap       = S(10.0f);      // between stacked toasts
    const float marginR   = S(20.0f);
    const float marginB   = S(20.0f);
    const float iconSz    = S(12.0f);

    ImGuiViewport* vp = ImGui::GetMainViewport();
    float anchorRight  = vp->WorkPos.x + vp->WorkSize.x - marginR;
    float anchorBottom = vp->WorkPos.y + vp->WorkSize.y - marginB;

    ImFont* boldFont = MenuStyle::g_fontBold ? MenuStyle::g_fontBold : ImGui::GetFont();
    float fontSize   = ImGui::GetFontSize();

    // Pre-measure each toast's height (title bar + body with wrapped text)
    struct Layout { float h; float bodyH; };
    std::vector<Layout> layouts(s_toasts.size());
    {
        float curOffset = 0.0f;
        // Stack from bottom upward — newest (last in vec) sits at bottom
        for (int i = (int)s_toasts.size() - 1; i >= 0; i--) {
            const Toast& t = s_toasts[i];
            float wrapW = toastW - padSide * 2.0f;
            ImVec2 wrapSize = ImGui::CalcTextSize(t.text.c_str(), nullptr, false, wrapW);
            float bodyH = padSide * 2.0f + wrapSize.y;
            float h = titleBarH + bodyH;
            layouts[i].h     = h;
            layouts[i].bodyH = bodyH;
            s_toasts[i].targetY = curOffset;
            curOffset += h + gap;
        }
    }

    // Smoothly lerp current Y toward target Y so toasts slide as ones above expire
    for (size_t i = 0; i < s_toasts.size(); i++) {
        Toast& t = s_toasts[i];
        t.currentY += (t.targetY - t.currentY) * std::min(18.0f * dt, 1.0f);
        if (std::abs(t.targetY - t.currentY) < 0.5f) t.currentY = t.targetY;
    }

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    for (size_t i = 0; i < s_toasts.size(); i++) {
        Toast& t = s_toasts[i];
        t.elapsed += dt;

        // Fade in/out alpha curve
        float alpha = 1.0f;
        if (t.elapsed < kFadeIn)
            alpha = t.elapsed / kFadeIn;
        else if (t.elapsed > t.duration)
            alpha = std::max(0.0f, 1.0f - (t.elapsed - t.duration) / kFadeOut);

        const Layout& L = layouts[i];
        float toastH = L.h;

        float rectRight  = anchorRight;
        float rectBottom = anchorBottom - t.currentY;
        float rectLeft   = rectRight - toastW;
        float rectTop    = rectBottom - toastH;

        // Slide-in offset (newest toast slides up from below)
        if (t.elapsed < kFadeIn) {
            float slide = (1.0f - t.elapsed / kFadeIn) * S(12.0f);
            rectTop    += slide;
            rectBottom += slide;
        }

        ImVec2 mn(rectLeft, rectTop);
        ImVec2 mx(rectRight, rectBottom);
        float titleBottom = mn.y + titleBarH;

        // ── Body fill (full card) ──────────────────────────────────────
        ImU32 bodyBg = MenuStyle::Colors::PanelBg.alpha(alpha).u32();
        dl->AddRectFilled(mn, mx, bodyBg);

        // ── Title bar gradient — Border@20% at top → Border@0% at
        //     the title bar's bottom edge. Overlaid on the body so no
        //     hard seam is needed between title and body.
        {
            ImU32 topCol = MenuStyle::Colors::Border.alpha(0.20f * alpha).u32();
            ImU32 botCol = MenuStyle::Colors::Border.alpha(0.00f).u32();
            dl->AddRectFilledMultiColor(
                mn, ImVec2(mx.x, titleBottom),
                topCol, topCol, botCol, botCol);
        }

        // ── Outlines: gray Border + outer black Ink, matching containers.
        ImU32 borderCol = MenuStyle::Colors::Border.alpha(alpha).u32();
        ImU32 inkCol    = MenuStyle::Colors::Ink.alpha(alpha).u32();
        dl->AddRect(mn, mx, borderCol, 0.0f, 0, 1.0f);
        dl->AddRect(ImVec2(mn.x - 1.0f, mn.y - 1.0f),
                    ImVec2(mx.x + 1.0f, mx.y + 1.0f),
                    inkCol, 0.0f, 0, 1.0f);

        // ── Accent gradient bar — 1 px tall strip inside the gray border
        //     at the very top edge. Fades left→right from the type's accent
        //     color to fully transparent, matching the menu window's accent
        //     cap treatment in menu.cpp.
        {
            MenuStyle::Color accent = AccentForType(t.type);
            ImU32 accentCol = accent.alpha(alpha).u32();
            ImU32 clearCol  = accent.alpha(0.0f).u32();
            float bx0 = mn.x + 1.0f;
            float bx1 = mx.x - 1.0f;
            float by0 = mn.y + 1.0f;
            float by1 = by0 + 1.0f;
            dl->AddRectFilledMultiColor(
                ImVec2(bx0, by0), ImVec2(bx1, by1),
                accentCol, clearCol, clearCol, accentCol);
        }

        // ── Title bar contents: [icon] type_name ───────────────────────
        MenuStyle::Color accentCol = AccentForType(t.type);
        ImU32 iconColU  = accentCol.alpha(alpha).u32();
        ImU32 titleColU = MenuStyle::Colors::TextMuted.alpha(alpha).u32();
        float titleY    = mn.y + (titleBarH - fontSize) * 0.5f;
        float cursorX   = mn.x + padSide;

        if (MenuStyle::g_fontIcon) {
            ImVec2 isz = MenuStyle::g_fontIcon->CalcTextSizeA(
                iconSz, FLT_MAX, 0.0f, IconForType(t.type));
            float iconY = mn.y + (titleBarH - isz.y) * 0.5f;
            dl->AddText(MenuStyle::g_fontIcon, iconSz,
                        ImVec2(cursorX, iconY), iconColU, IconForType(t.type));
            cursorX += isz.x + S(6.0f);
        }

        // Title text with 1 px shadow — same treatment container titles use.
        ImU32 shadowCol = MenuStyle::Colors::ShadowInk.alpha(alpha * 0.35f).u32();
        dl->AddText(boldFont, fontSize,
                    ImVec2(cursorX + 1.0f, titleY + 1.0f),
                    shadowCol, TitleForType(t.type));
        dl->AddText(boldFont, fontSize,
                    ImVec2(cursorX, titleY),
                    titleColU, TitleForType(t.type));

        // ── Body text (wrapped) ────────────────────────────────────────
        ImU32 bodyColU = MenuStyle::Colors::Text.alpha(alpha).u32();
        float wrapW    = mx.x - mn.x - padSide * 2.0f;
        dl->AddText(ImGui::GetFont(), fontSize,
                    ImVec2(mn.x + padSide, titleBottom + padSide),
                    bodyColU, t.text.c_str(), nullptr, wrapW);
    }

    // Remove fully faded-out toasts
    s_toasts.erase(
        std::remove_if(s_toasts.begin(), s_toasts.end(),
            [](const Toast& t) {
                return t.elapsed > t.duration + kFadeOut;
            }),
        s_toasts.end());
}

} // namespace Notifications
