#include "menu_style.h"
#include "fa_font_data.h" // embedded FontAwesome bytes for LoadIconFont

namespace MenuStyle {

ImFont* LoadIconFont(float sizePixels, const ImWchar* ranges) {
    ImGuiIO& io = ImGui::GetIO();

    // Default range covers every FA6 Free Solid glyph the framework uses.
    static const ImWchar kDefaultRanges[] = { 0xf000, 0xf999, 0 };
    if (!ranges) ranges = kDefaultRanges;

    ImFontConfig cfg;
    cfg.MergeMode = false;
    cfg.PixelSnapH = true;
    cfg.GlyphMinAdvanceX = 13.0f;
    // The embedded byte array has static storage duration — tell ImGui not
    // to IM_FREE it on atlas teardown, since it wasn't heap-allocated.
    cfg.FontDataOwnedByAtlas = false;

    // AddFontFromMemoryTTF wants a mutable void*; the cast is safe because
    // FontDataOwnedByAtlas=false means ImGui only reads the buffer.
    g_fontIcon = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(g_faSolidFontData),
        (int)g_faSolidFontSize,
        sizePixels, &cfg, ranges);
    return g_fontIcon;
}


// Apply the theme. Every color slot routes through the Colors namespace in
// menu_style.h — to reskin the menu, edit the values there, not here.
void SetupStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    // ── Rounding — zero across the board ────────────────────────────
    // Pandora-style: every rect stays a hard rectangle so the split-panel
    // layout (top strip, sidebar box, container title bar) reads as a
    // grid of aligned frames.
    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 0.0f;
    style.FrameRounding     = 0.0f;
    style.PopupRounding     = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding      = 0.0f;
    style.TabRounding       = 0.0f;

    // ── Borders ─────────────────────────────────────────────────────
    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.TabBorderSize     = 0.0f;

    // ── Spacing ─────────────────────────────────────────────────────
    style.WindowPadding     = ImVec2(0.0f, 0.0f);
    style.FramePadding      = ImVec2(8.0f, 5.0f);  // matches web input padding
    style.ItemSpacing       = ImVec2(10.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
    style.IndentSpacing     = 16.0f;
    style.ScrollbarSize     = 10.0f;
    style.GrabMinSize       = 8.0f;

    // ── Colors ──────────────────────────────────────────────────────
    // Every slot references a named token from Colors. No literals here.
    using namespace Colors;

    style.Colors[ImGuiCol_Text]                 = Text;
    style.Colors[ImGuiCol_TextDisabled]         = TextMuted;
    style.Colors[ImGuiCol_WindowBg]             = WindowBg;
    style.Colors[ImGuiCol_ChildBg]              = Ink.alpha(0.0f);   // transparent
    style.Colors[ImGuiCol_PopupBg]              = PopupBg.alpha(0.98f);
    style.Colors[ImGuiCol_Border]               = Border;
    style.Colors[ImGuiCol_BorderShadow]         = Ink.alpha(0.0f);

    // Widgets sit on --surface cards, so the fill needs to be VISIBLY
    // brighter than the card to read as interactive (raised chip style).
    style.Colors[ImGuiCol_FrameBg]              = SecondaryBg;
    style.Colors[ImGuiCol_FrameBgHovered]       = Border;
    style.Colors[ImGuiCol_FrameBgActive]        = Border;

    style.Colors[ImGuiCol_TitleBg]              = SecondaryBg;
    style.Colors[ImGuiCol_TitleBgActive]        = SecondaryBg;
    style.Colors[ImGuiCol_TitleBgCollapsed]     = SecondaryBg;
    style.Colors[ImGuiCol_MenuBarBg]            = SecondaryBg;

    // Scrollbars — track transparent, grab in border-strong so it reads
    // as a soft raised bar. Matches .udp-scroll::-webkit-scrollbar.
    style.Colors[ImGuiCol_ScrollbarBg]          = Ink.alpha(0.0f);
    style.Colors[ImGuiCol_ScrollbarGrab]        = Border;
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = TextMuted;
    style.Colors[ImGuiCol_ScrollbarGrabActive]  = Text;

    style.Colors[ImGuiCol_CheckMark]            = Accent;
    style.Colors[ImGuiCol_SliderGrab]           = Accent;
    style.Colors[ImGuiCol_SliderGrabActive]     = Accent.scaleRGB(0.9f);

    // Buttons — rest on secondary, hover strong, press briefly to popup
    // (subtle sink). Web pattern; no accent flash on click.
    style.Colors[ImGuiCol_Button]               = SecondaryBg;
    style.Colors[ImGuiCol_ButtonHovered]        = Border;
    style.Colors[ImGuiCol_ButtonActive]         = PopupBg;

    style.Colors[ImGuiCol_Header]               = SecondaryBg;
    style.Colors[ImGuiCol_HeaderHovered]        = Border.mix(SecondaryBg, 0.5f);
    style.Colors[ImGuiCol_HeaderActive]         = Accent.alpha(0.55f);

    style.Colors[ImGuiCol_Separator]            = Border;
    style.Colors[ImGuiCol_SeparatorHovered]     = Ink.alpha(0.0f);
    style.Colors[ImGuiCol_SeparatorActive]      = Ink.alpha(0.0f);

    style.Colors[ImGuiCol_ResizeGrip]           = Ink.alpha(0.0f);
    style.Colors[ImGuiCol_ResizeGripHovered]    = Ink.alpha(0.0f);
    style.Colors[ImGuiCol_ResizeGripActive]     = Ink.alpha(0.0f);

    style.Colors[ImGuiCol_Tab]                  = WindowBg;
    style.Colors[ImGuiCol_TabHovered]           = SecondaryBg;
    style.Colors[ImGuiCol_TabSelected]          = PopupBg;

    style.Colors[ImGuiCol_PlotLines]            = TextMuted;
    style.Colors[ImGuiCol_PlotLinesHovered]     = Accent;
    style.Colors[ImGuiCol_PlotHistogram]        = Accent;
    style.Colors[ImGuiCol_PlotHistogramHovered] = Accent.scaleRGB(1.1f);

    style.Colors[ImGuiCol_TextSelectedBg]       = Accent.alpha(0.35f);
    style.Colors[ImGuiCol_NavHighlight]         = Ink.alpha(0.0f);
    style.Colors[ImGuiCol_ModalWindowDimBg]     = Ink.alpha(0.50f);
}

} // namespace MenuStyle
