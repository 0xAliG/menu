#pragma once
#include "imgui.h"

// ─────────────────────────────────────────────────────────────────────────────
// MenuStyle — the ONLY place colors, fonts, and DPI scale live for the menu.
//
// To reskin the menu, edit values in the Colors namespace at the bottom of
// this file. There should be no hardcoded RGB anywhere else in the project;
// every draw site references one of these named tokens.
//
// The Color type stores an ImVec4 and computes an ImU32 on demand:
//   • Pass it directly where ImGui wants an ImVec4 (implicit conversion).
//   • Call .u32() where a draw-list API wants ImU32.
//   • .alpha(a) / .mix(other,t) / .scaleRGB(s) derive variants without
//     duplicating the base color.
// ─────────────────────────────────────────────────────────────────────────────

namespace MenuStyle {

// ── Global handles set during init ──────────────────────────────────────────
inline ImFont*     g_fontRegular = nullptr;
inline ImFont*     g_fontBold    = nullptr;
inline ImFont*     g_fontIcon    = nullptr;
inline float       g_dpiScale    = 1.0f;
inline ImTextureID g_logoTexture = ImTextureID();
inline float       g_logoW       = 0.0f;
inline float       g_logoH       = 0.0f;

// Global alpha multiplier applied by Color::u32() to every raw color the
// menu emits. Kept at 1.0 in steady state; the menu's open-in animation
// pushes it down toward 0 (via Menu::GetOpenAnim()) so the whole chrome
// — container fills, title bars, tabs, accent bar, etc — fades in as a
// unit alongside ImGui's own style-Alpha-driven draws.
inline float       g_globalAlpha = 1.0f;

// DPI scale helper — multiply any pixel value by the current DPI scale.
inline float S(float v) { return v * g_dpiScale; }

// ── Color type ──────────────────────────────────────────────────────────────
struct Color {
    ImVec4 v;

    constexpr Color() : v(0.0f, 0.0f, 0.0f, 0.0f) {}
    constexpr Color(float r, float g, float b, float a = 1.0f) : v(r, g, b, a) {}
    constexpr Color(const ImVec4& c) : v(c) {}

    // CSS-style integer channels (0..255). The ctor you'll use most.
    Color(int r, int g, int b, int a = 255)
        : v(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f) {}

    ImU32 u32() const {
        auto clamp = [](float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); };
        return IM_COL32((int)(clamp(v.x) * 255.0f + 0.5f),
                        (int)(clamp(v.y) * 255.0f + 0.5f),
                        (int)(clamp(v.z) * 255.0f + 0.5f),
                        (int)(clamp(v.w * g_globalAlpha) * 255.0f + 0.5f));
    }

    operator ImVec4() const { return v; }

    Color alpha(float a) const { return Color(v.x, v.y, v.z, a); }
    Color scaleRGB(float s) const { return Color(v.x * s, v.y * s, v.z * s, v.w); }
    Color mix(const Color& other, float t) const {
        return Color(v.x + (other.v.x - v.x) * t,
                     v.y + (other.v.y - v.y) * t,
                     v.z + (other.v.z - v.z) * t,
                     v.w + (other.v.w - v.w) * t);
    }
};

// ── Colors — every named token the menu uses ────────────────────────────────
//
// Edit values here to reskin. Ordering: base neutrals → surfaces → borders →
// text → icons → accent → semantic → component-scoped → overlays.
namespace Colors {

    // ── Base neutrals ───────────────────────────────────────────────
    // MenuDark  = sidebar / top strip / bottom footer / container fill.
    // MenuLight = window bg + content-child bg (a whisper brighter).
    inline Color Ink       (  0,   0,   0);
    inline Color White     (255, 255, 255);
    inline Color MenuDark  ( 8, 8, 8);
    inline Color MenuLight ( 10,  10,  10);

    // ── Surfaces ────────────────────────────────────────────────────
    inline Color WindowBg    = MenuDark;
    inline Color PanelBg     = MenuDark;
    inline Color BarBg       (24, 24, 24);   // top/bottom bars, sub-panels
    inline Color SecondaryBg (24, 24, 24);
    inline Color InputBg     (14, 14, 14);   // textboxes / dropdowns / floating panels
    inline Color WidgetBase  (18, 18, 18);   // top vertex of widget gradient
    inline Color PopupBg     (21, 21, 21);   // notifications / tooltips
    inline Color HexBg       (25, 25, 25);   // copy-hash chip (settings/user profile)

    // ── Borders / outlines ──────────────────────────────────────────
    // ONE border color for every non-widget outline in the app — every
    // container, panel, popup, chip frame, and the widget-effect gradient
    // overlay all reference Border. Change this one value to shift every
    // outline uniformly.
    inline Color Border        (35, 35, 35);
    inline Color WidgetOutline = Ink;         // crisp black silhouette on interactive widgets

    // ── Text (dark → light) ─────────────────────────────────────────
    inline Color TextFaint   ( 77,  77,  77);   // dead / non-interactive
    inline Color TextMuted   (110, 110, 110);   // disabled default
    inline Color TextNormal  (189, 189, 189);   // always-on widget baseline
    inline Color TextBright  (236, 236, 236);   // per-widget "on" state
    inline Color Text        (250, 250, 250);   // primary / hovered

    // ── Icons ───────────────────────────────────────────────────────
    inline Color IconGray     (149, 149, 149);  // dropdown arrow, glyph rest
    inline Color IconMuted    (122, 122, 122);  // question-mark glyph
    inline Color IconRest     (140, 140, 140);  // + button rest
    inline Color IconHover    (220, 220, 220);  // + button hover
    inline Color IconDisabled ( 80,  80,  80);
    inline Color IconSearch   (154, 154, 154);

    // ── Accent (Steel periwinkle #848bc2) ───────────────────────────
    // Mutate this to shift the accent hue globally.
    inline Color Accent      (172, 178, 232);
    inline Color AccentSoft() { return Accent.alpha(0.14f); }
    inline Color AccentDim()  { return Accent.alpha(0.60f); }

    // ── Semantic ────────────────────────────────────────────────────
    inline Color Danger       (229, 100, 109);
    inline Color DangerHover  (220,  60,  60);
    inline Color WarnRed      (255, 100, 100);   // inline validation text
    inline Color NotifInfo    (120, 170, 230);
    inline Color NotifWarning (230, 180,  90);
    inline Color NotifError   (220,  90,  90);
    inline Color NotifTitle   (235, 235, 235);   // toast title fill
    inline Color NotifBody    (190, 190, 190);   // toast body fill

    // Close-button (X glyph) colors — rest is a soft neutral gray, hover
    // shifts to a warm red to telegraph the destructive action.
    inline Color CloseGlyphRest  (150, 150, 150);
    inline Color CloseGlyphHover (200,  80,  80);

    // Settings-cog icon in the top-right of settings popups — slightly
    // darker/neutral than IconMuted so it recedes until hovered.
    inline Color IconCogRest     (102, 102, 102);

    // Settings-cog popup card — body & title-bar surfaces. One point
    // brighter than the base PopupBg/SecondaryBg tokens to lift the card
    // above the underlying panels.
    inline Color SettingsCardBg      (21, 21, 21);
    inline Color SettingsCardTitleBg (26, 26, 26);

    // ── Component-scoped tokens ─────────────────────────────────────
    //
    // Sidebar subtab hover/selected — left-to-right sheen anchored to the
    // accent bar, dissolving into the sidebar bg. Left endpoint is a tone
    // between Border and MenuDark so the gradient reads softer (lower
    // effective opacity). Right endpoint lands exactly on MenuDark so the
    // fade is seamless with the sidebar background. Tune the .mix() ratio
    // — 0.0 = full Border brightness, 1.0 = invisible (all MenuDark).
    inline Color SidebarSubtabSheenLeft  = Border.mix(MenuDark, 0.5f);
    inline Color SidebarSubtabSheenRight = MenuDark;
    inline Color SidebarSubtabHoverLeft  = Border.mix(MenuDark, 0.7f);
    inline Color SidebarSubtabHoverRight = MenuDark;

    // Top subtab selected background (behind the label of the active
    // horizontal top-strip tab).
    inline Color TopSubtabSelectedBg (19, 19, 19);

    // Main top tab (the big pandora-style nav tabs at the very top of the
    // menu). Selected state has two layers:
    //   • TopTabBodyFill — solid fill covering the whole tab body.
    //   • TopTabGlowTop  — top-edge gradient tone that fades to transparent
    //     over roughly the top half of the tab (soft accent wash).
    inline Color TopTabBodyFill = MenuLight;
    inline Color TopTabGlowTop  = Border;

    // Color-picker checkerboard behind the alpha channel slider.
    inline Color CheckerDark  (50, 50, 50);
    inline Color CheckerLight (80, 80, 80);

    // ── Overlays ────────────────────────────────────────────────────
    // Base tints designed to be used with .alpha(x) at the call site so
    // every drop-shadow / vignette in the app tunes to the same ink.
    inline Color ShadowInk   = Ink;         // text/drop shadow
    inline Color VignetteInk = MenuLight;   // bottom fade over content
    inline Color RaiseWhite  = White;       // subtle white lift on hover

} // namespace Colors

// ── Panel — shared design tokens for every floating window ─────────────────
//
// Watermark, notifications, debug overlay, keybind manager, keybind list,
// settings-cog popup, and color popups all render through these constants so
// they read as one system. Values are in un-scaled pixels; wrap in S() at the
// call site for DPI scaling.
//
// Defaults deliberately match the main menu window's chrome:
//   * Rounding = 0 — sharp rectangles, like the main menu itself.
//   * Bg      = MenuDark — same tone as the sidebar / container fill, so a
//     floating panel reads as belonging to the same surface family instead
//     of floating on a lighter background.
//   * TitleBg = SecondaryBg — the same title tone the main containers use.
//   * Border  = Border — the main menu's outer-frame outline.
//
// Tune from HERE to reskin every floating window in one edit.
namespace Panel {
    inline float Rounding  = 0.0f;
    inline float TitleBarH = 26.0f;
    inline Color Bg      = Colors::MenuDark;
    inline Color TitleBg = Colors::SecondaryBg;
    inline Color Border  = Colors::Border;
} // namespace Panel

// ── Text drop-shadow helper ─────────────────────────────────────────────────
// Draws a 1px offset ink-alpha shadow behind the glyphs then the real fill on
// top. Pairs well with small fonts where 1px of black adds crispness without
// turning the label into an outlined-cheat-menu look.
inline void TextShadow(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text) {
    dl->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f),
                Colors::ShadowInk.alpha(0.55f).u32(), text);
    dl->AddText(pos, col, text);
}
inline void TextShadow(ImDrawList* dl, ImFont* font, float fontSize, ImVec2 pos,
                       ImU32 col, const char* text) {
    dl->AddText(font, fontSize, ImVec2(pos.x + 1.0f, pos.y + 1.0f),
                Colors::ShadowInk.alpha(0.55f).u32(), text);
    dl->AddText(font, fontSize, pos, col, text);
}

// Apply the theme (fonts / rounding / borders / ImGuiCol_* slots).
void SetupStyle();

// Load the embedded FontAwesome 6 Free Solid font into the current
// ImGui context and populate MenuStyle::g_fontIcon with the result. The
// font bytes are compiled into the framework (see fa_font_data.h) so
// consumers don't need to ship or locate any .ttf file at runtime.
//
//   sizePixels — glyph size to bake (default 16). Larger sizes render
//                sharper at DPI > 1 but eat more atlas space.
//   ranges     — glyph ranges to include. Pass nullptr for the default
//                (FontAwesome 6 range 0xf000..0xf999). Pass a custom
//                ImWchar[] terminated with 0 to include additional /
//                fewer codepoints.
//
// Must be called AFTER ImGui::CreateContext() and BEFORE the first frame.
// Returns the ImFont* (also stored in g_fontIcon).
ImFont* LoadIconFont(float sizePixels = 16.0f, const ImWchar* ranges = nullptr);

} // namespace MenuStyle
