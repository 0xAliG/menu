#include "menu.h"
#include "menu_style.h"
#include "ui_components.h"
#include "keybind_system.h"
#include "menu_builder.h"
#include "user_system.h"
#include "user_profile.h"
#include "notifications.h"
#include "debug_window.h"
#include "config_system.h"
#include "imgui.h"
#include <cstring>
#include <cstdio>
#include <string>
#include <algorithm>
#include <cctype>

// Shorthand for DPI-scaled pixel values
#define S(x) (MenuStyle::S(x))

// FontAwesome icon codes (Font Awesome 6 Free Solid) — public so clients
// can spawn their own tabs/subtabs with matching iconography.
#include "fa_icons.h"

namespace Menu {

// Inner shadow helper
static void DrawInnerShadow(ImDrawList* dl, ImVec2 rMin, ImVec2 rMax,
                            float pad = 1.0f, float depth = 4.0f,
                            ImU32 col = MenuStyle::Colors::ShadowInk.alpha(45 / 255.0f).u32()) {
    ImU32 s0 = MenuStyle::Colors::Ink.alpha(0.0f).u32();
    ImVec2 a(rMin.x + pad, rMin.y + pad);
    ImVec2 b(rMax.x - pad, rMax.y - pad);
    dl->PushClipRect(rMin, rMax, true);
    dl->AddRectFilledMultiColor(a, ImVec2(b.x, a.y + depth), col, col, s0, s0);
    dl->AddRectFilledMultiColor(ImVec2(a.x, b.y - depth), b, s0, s0, col, col);
    dl->AddRectFilledMultiColor(a, ImVec2(a.x + depth, b.y), col, s0, s0, col);
    dl->AddRectFilledMultiColor(ImVec2(b.x - depth, a.y), b, s0, col, col, s0);
    dl->PopClipRect();
}

// ─── State ─────────────────────────────────────────────────────────────────

static bool s_visible = true;
static bool s_initialized = false;

// Open animation — 0 when the menu just became visible, 1 when fully open.
// Lerped each Render(). Starts at 0 so the very first frame animates in.
static float s_openAnim = 0.0f;

static bool s_lockLayout = false;
static int s_dpiIndex = 2; // index into dpi options (default 100%)
static const char* const s_dpiOptions[] = { "75%", "90%", "100%", "110%", "125%", "150%", "175%", "200%" };
static const float s_dpiValues[] = { 0.75f, 0.90f, 1.00f, 1.10f, 1.25f, 1.50f, 1.75f, 2.00f };

// Menu-toggle virtual-key code. Default VK_INSERT — matches every
// previous apex / rust client. Engine settings popup rebinds via
// SetMenuKey; clients poll Input::IsHotkeyDown(GetMenuKey()) each frame.
static int s_menuKey = 0x2D; // VK_INSERT

// Keybind-list panel gate. Default true so the new menu standalone demo
// shows it without any client-side wiring. Apex mirrors panels[1] into
// here every TickClient; rust flips it to false in SetupClient because
// rust uses its own Menu::Panels dispatcher and doesn't want the engine
// double-rendering the list.
static bool s_keybindListPanel = true;

// ─── Client hooks ──────────────────────────────────────────────────────────
//
// SetupClient() / TickClient() are the only two entry points every client
// must supply. Init() calls SetupClient once; Render() calls TickClient
// every frame BEFORE the menu draw. See demo/demo_menu.cpp for the demo's
// implementation. Apex and rust provide their own via their build systems.



// ─── Init ──────────────────────────────────────────────────────────────────

void Init() {
    KeybindSystem::Init();
    // Hand off to the client — every consumer supplies its own SetupClient
    // (demo does it in demo/demo_menu.cpp; apex/rust in *_menu_setup.cpp).
    SetupClient();

    // Force menu open on first show — every freshly-injected session
    // starts with the menu visible so the user doesn't have to hunt for
    // the toggle key. Any later ConfigSystem::Apply / explicit toggle
    // still moves it freely from here.
    s_visible = true;

    s_initialized = true;
}

void ToggleVisibility() {
    s_visible = !s_visible;
    if (s_visible) {
        UI::ResetAnimations();
        s_openAnim = 0.0f; // replay the open-in animation
    }
}
bool IsVisible() { return s_visible; }

float GetOpenAnim() { return s_openAnim; }

bool GetLockLayout()       { return s_lockLayout; }
void SetLockLayout(bool v) { s_lockLayout = v; }

int  GetMenuKey()          { return s_menuKey; }
void SetMenuKey(int vk)    { if (vk > 0 && vk < 256) s_menuKey = vk; }

bool GetKeybindListPanel()        { return s_keybindListPanel; }
void SetKeybindListPanel(bool en) { s_keybindListPanel = en; }

// PollMenuToggle state. s_menuKeyArmed is the gate: when false, every
// down-edge gets ignored until the key has been seen released at least
// once. Bind completion flips it to false so the bind key-press itself
// (which is what closed the popup capture loop) can't immediately
// toggle the menu — the user has to actually let go and press again.
static bool s_menuKeyWasDown = false;
static bool s_menuKeyArmed   = true;

bool PollMenuToggle(bool keyDown) {
    bool toggle = false;
    if (!s_menuKeyArmed) {
        // Disarmed — wait for a release before re-enabling. The key
        // being down right now is the leftover of the bind press, so
        // we silently absorb it.
        if (!keyDown) s_menuKeyArmed = true;
    } else if (keyDown && !s_menuKeyWasDown) {
        toggle = true;
    }
    s_menuKeyWasDown = keyDown;
    return toggle;
}

int  GetDpiIndex()         { return s_dpiIndex; }
void SetDpiIndex(int idx)  {
    if (idx < 0) idx = 0;
    if (idx >= (int)(sizeof(s_dpiValues) / sizeof(s_dpiValues[0])))
        idx = (int)(sizeof(s_dpiValues) / sizeof(s_dpiValues[0])) - 1;
    s_dpiIndex = idx;
    MenuStyle::g_dpiScale = s_dpiValues[idx];
}

// ─── Title Bar ─────────────────────────────────────────────────────────────

// Forward decl — RenderSettingsPopup lives below RenderTitleBar in this
// file but is called from inside the title bar's cog click handler.
static void RenderSettingsPopup();

// Pandora-style top strip. Layout, left → right:
//   [ pdx logo ] [ main tabs as plain text ]           [ cog icon ]
//
// No filled background — the strip sits on the window's --ground so the
// tabs read as part of the same surface. A single --border line separates
// the strip from the content below.
static void RenderTopStrip(float menuW) {
    float stripH = S(28.5f);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Strip bg = --surface (matches bottom footer, sidebar box, container
    // fill). Slightly darker than the window bg so the strip reads as a
    // recessed frame.
    dl->AddRectFilled(pos, ImVec2(pos.x + menuW, pos.y + stripH),
                      MenuStyle::Colors::PanelBg.u32());
    // Bottom border under the strip. Drawn as a 1px-tall AddRectFilled
    // rather than an AddLine — ImGui's anti-aliased lines can visually
    // land at ~1.5px when the y coordinate is fractional; a filled rect
    // on floored integer coords guarantees a hard 1px stroke.
    {
        float lineY = std::floor(pos.y + stripH - 1.0f);
        dl->AddRectFilled(ImVec2(pos.x, lineY),
                          ImVec2(pos.x + menuW, lineY + 1.0f),
                          MenuStyle::Colors::Border.u32());
    }

    // ── Wordmark (far left) — "pdx" in accent, ".dev" in text colour ────
    //
    // Drawn in two AddText calls so the two segments carry different
    // fills; the total width is measured to know where the tabs begin.
    ImFont* wordFont = MenuStyle::g_fontBold ? MenuStyle::g_fontBold : ImGui::GetFont();
    float wordSize = ImGui::GetFontSize();
    const char* accentSeg = "pdx";
    const char* whiteSeg  = ".dev";
    ImVec2 accentTS = wordFont->CalcTextSizeA(wordSize, FLT_MAX, 0.0f, accentSeg);
    ImVec2 whiteTS  = wordFont->CalcTextSizeA(wordSize, FLT_MAX, 0.0f, whiteSeg);
    // Symmetric padding on both sides of the wordmark so it centers
    // horizontally in the [menu_left, first_tab_start] region.
    const float logoPad = S(16.0f);
    float wordX = pos.x + logoPad;
    float wordY = pos.y + (stripH - accentTS.y) * 0.5f;
    dl->AddText(wordFont, wordSize, ImVec2(wordX, wordY),
                MenuStyle::Colors::Accent.u32(), accentSeg);
    dl->AddText(wordFont, wordSize, ImVec2(wordX + accentTS.x, wordY),
                MenuStyle::Colors::Text.u32(), whiteSeg);
    float logoRightX = wordX + accentTS.x + whiteTS.x;

    // ── Tabs (right of the wordmark) ────────────────────────────────────
    // Clip the tab strip to the strip's rect so the slide-in animation
    // (tabs coming up from below the strip's bottom edge) stays hidden
    // beneath until it reaches the strip. Bottom edge extends 1 px past
    // stripH so the selected tab's fill overshoot (which already draws
    // 1 px past the strip to cover the border line) isn't clipped away.
    ImGui::PushClipRect(pos,
                        ImVec2(pos.x + menuW, pos.y + stripH + 1.0f),
                        true);
    // Same logoPad after the wordmark as before it, so the wordmark
    // sits centered in the [menu_left, first_tab_start_x] region.
    ImGui::SetCursorScreenPos(ImVec2(logoRightX + logoPad, pos.y));
    MenuBuilder::Builder::Get().RenderTopTabs(stripH, S(12.0f));
    ImGui::PopClipRect();

    // ── Settings cog (far right) ────────────────────────────────────────
    {
        float cogBtnW = stripH;
        ImVec2 cogMin(pos.x + menuW - cogBtnW, pos.y);
        ImGui::SetCursorScreenPos(cogMin);

        if (ImGui::InvisibleButton("##settings_cog_btn", ImVec2(cogBtnW, stripH)))
            ImGui::OpenPopup("##settings_popup");
        bool cogHovered = ImGui::IsItemHovered();

        ImGui::SetNextWindowPos(
            ImVec2(pos.x + menuW + S(6.0f), pos.y),
            ImGuiCond_Always,
            ImVec2(0.0f, 0.0f));
        RenderSettingsPopup();

        if (MenuStyle::g_fontIcon) {
            const char* cogIcon = ICON_FA_COG;
            float fs = S(14.0f);
            ImVec2 ico = MenuStyle::g_fontIcon->CalcTextSizeA(fs, FLT_MAX, 0.0f, cogIcon);
            float ix = cogMin.x + (cogBtnW - ico.x) * 0.5f;
            float iy = cogMin.y + (stripH  - ico.y) * 0.5f;
            ImU32 cogCol = cogHovered ? MenuStyle::Colors::Text.u32()
                                      : MenuStyle::Colors::TextMuted.u32();
            dl->AddText(MenuStyle::g_fontIcon, fs, ImVec2(ix, iy), cogCol, cogIcon);
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + stripH));
}

// Legacy title-bar entry point — pandora-style redesign replaced it with
// RenderTopStrip. Kept as a thin forward so any external caller doesn't
// break during transition; delete once nothing else references it.
static void RenderTitleBar(float menuW) { RenderTopStrip(menuW); }

// ─── Settings Popup ────────────────────────────────────────────────────────

static void RenderSettingsPopup() {
    static float s_popupAlpha = 0.0f;
    bool isOpen = ImGui::IsPopupOpen("##settings_popup");
    float dt = ImGui::GetIO().DeltaTime;
    s_popupAlpha += ((isOpen ? 1.0f : 0.0f) - s_popupAlpha) * std::min(20.0f * dt, 1.0f);
    if (std::abs((isOpen ? 1.0f : 0.0f) - s_popupAlpha) < 0.005f) s_popupAlpha = isOpen ? 1.0f : 0.0f;

    float a = s_popupAlpha;
    // Transparent popup shell — BeginContainer draws the whole card/title/
    // border on its own drawlist channel (same trick the settings-cog
    // popup uses so it matches menu containers exactly).
    ImGui::PushStyleColor(ImGuiCol_PopupBg,     ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,      ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_NavHighlight,ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,  0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,          a);

    // Menu-toggle key is captured by UI::StandaloneKeybindChip below via
    // KeybindSystem's standalone key-listen path (see BeginStandaloneKeyEdit).

    const float popupW = S(260.0f);
    ImGui::SetNextWindowSize(ImVec2(popupW, 0));
    if (ImGui::BeginPopup("##settings_popup")) {
        // Render the whole body as a proper container titled "settings" —
        // matches every other floating panel in the app.
        UI::BeginContainer("settings", popupW, nullptr, /*collapsable=*/false);

        if (UI::Combo("dpi scale", &s_dpiIndex, s_dpiOptions, 8))
            MenuStyle::g_dpiScale = s_dpiValues[s_dpiIndex];

        UI::Checkbox("lock menu layout", &s_lockLayout);

        // Menu-toggle key — standalone chip. Click the chip to enter key-
        // listen; the next key press is captured into s_menuKey. Locked so
        // it can't be deleted (the toggle key must always exist).
        UI::StandaloneKeybindChip("menu key", &s_menuKey, /*locked=*/true);

        UI::EndContainer();
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(4);  // WindowPadding, PopupRounding, PopupBorderSize, Alpha
    ImGui::PopStyleColor(3); // PopupBg, Border, NavHighlight
}

// ─── Top SubTab Bar ────────────────────────────────────────────────────────

static void RenderTopSubTabBar(float menuW, float barH) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background strip
    dl->AddRectFilled(pos, ImVec2(pos.x + menuW, pos.y + barH),
                      MenuStyle::Colors::PanelBg.u32());

    // Bottom border separating subtab bar from content
    dl->AddLine(ImVec2(pos.x, pos.y + barH - 1.0f),
                ImVec2(pos.x + menuW, pos.y + barH - 1.0f),
                MenuStyle::Colors::Border.u32());

    // Tabs — cursor already at subtab bar origin
    ImGui::SetCursorScreenPos(pos);
    MenuBuilder::Builder::Get().RenderTopSubTabs(menuW, barH);

    // Snap cursor to exactly the end of the bar (no trailing Dummy — it would add
    // ItemSpacing.y overshoot, pushing the content child 8 px too low and causing
    // its clip rect to bleed into the bottom bar).
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + barH));
}

// ─── Bottom Tab Bar ────────────────────────────────────────────────────────

// ─── Bottom footer ─────────────────────────────────────────────────────────
//
// Thin non-interactive strip pinned to the bottom of the menu window. Shows:
//   left  : subscription state — "expires in 30 days" / "lifetime" / "expired"
//   right : "active user: <username>"
// Prefers the HV-fetched UserProfile when available; falls back to
// UserSystem's demo defaults otherwise. Matches the pandora reference footer.
static void RenderBottomFooter(float menuW, float footerH) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec2 barMax(pos.x + menuW, pos.y + footerH);
    // Same tone as the top strip + sidebar + container fills so the frame
    // reads as one system.
    dl->AddRectFilled(pos, barMax, MenuStyle::Colors::PanelBg.u32());
    // Top border on the footer strip — same 1px-tall AddRectFilled trick
    // as the top-strip border so the line lands as a hard 1px stroke on
    // integer coordinates rather than a fuzzy anti-aliased pair of rows.
    {
        float lineY = std::floor(pos.y);
        dl->AddRectFilled(ImVec2(pos.x, lineY),
                          ImVec2(barMax.x, lineY + 1.0f),
                          MenuStyle::Colors::Border.u32());
    }

    const bool profileReady = UserProfile::IsReady();
    const std::string& uname = profileReady
        ? UserProfile::GetUsername()
        : UserSystem::GetUser().username;

    std::string expiryLine;
    if (profileReady) {
        expiryLine = UserProfile::GetExpiryDisplay();
    } else {
        const int days = UserSystem::GetUser().subscriptionDaysLeft;
        if (days < 0) expiryLine = "lifetime";
        else if (days == 0) expiryLine = "expired";
        else {
            char buf[32];
            std::snprintf(buf, sizeof(buf),
                "expires in %d day%s", days, days == 1 ? "" : "s");
            expiryLine = buf;
        }
    }

    ImFont* f = ImGui::GetFont();
    float fs = S(11.5f);
    float padX = S(14.0f);
    float ty = std::floor(pos.y + (footerH - f->CalcTextSizeA(fs, FLT_MAX, 0.0f, "M").y) * 0.5f);

    if (!expiryLine.empty()) {
        dl->AddText(f, fs, ImVec2(pos.x + padX, ty),
                    MenuStyle::Colors::TextMuted.u32(), expiryLine.c_str());
    }

    if (!uname.empty()) {
        const char* label = "user: ";
        ImVec2 lSz = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, label);
        ImVec2 uSz = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, uname.c_str());
        float tx = barMax.x - padX - (lSz.x + uSz.x);
        dl->AddText(f, fs, ImVec2(tx, ty),
                    MenuStyle::Colors::TextMuted.u32(), label);
        dl->AddText(f, fs, ImVec2(tx + lSz.x, ty),
                    MenuStyle::Colors::Accent.u32(), uname.c_str());
    }

    ImGui::SetCursorScreenPos(ImVec2(pos.x, barMax.y));
}

// ─── Left-rail tab bar ─────────────────────────────────────────────────────
//
// Vertical column of icon-only tabs pinned to the far left of the menu,
// under the title bar. Replaces the old horizontal bottom bar. Profile
// bubble (avatar + username + expiry) stacks at the bottom of the rail
// on top of the last tab slot's row.
static void RenderSideTabRail(float railW, float railH) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec2 railMax(pos.x + railW, pos.y + railH);
    dl->AddRectFilled(pos, railMax, MenuStyle::Colors::BarBg.u32());
    // Right border separating rail from content region.
    dl->AddLine(ImVec2(railMax.x - 0.5f, pos.y),
                ImVec2(railMax.x - 0.5f, railMax.y),
                MenuStyle::Colors::Border.u32());

    // Tabs occupy the top portion of the rail; leave space at the bottom
    // for the profile bubble (avatar + name + expiry stacked vertically).
    const float tabH = S(56.0f);
    const float profileH = S(78.0f);
    (void)profileH; // reserved by the layout math below

    // Render the tabs first — Builder places them starting at the current
    // cursor pos, one slot per tab going down.
    ImGui::SetCursorScreenPos(pos);
    MenuBuilder::Builder::Get().RenderSideTabs(railW, tabH);

    // ── Bottom profile bubble ───────────────────────────────────────────
    // Same content as the old bottom-bar bubble, stacked vertically inside
    // the narrow rail: circular avatar centered, username + expiry below.
    {
        const float avatarR = S(14.0f);
        const float bubbleTopPad = S(14.0f);
        const float bubbleY0 = railMax.y - profileH + bubbleTopPad;
        const float avatarCX = pos.x + railW * 0.5f;
        const float avatarCY = bubbleY0 + avatarR;

        // Subtle divider above the profile bubble so it doesn't visually
        // merge with the last tab.
        dl->AddLine(ImVec2(pos.x + S(8.0f), bubbleY0 - S(4.0f)),
                    ImVec2(railMax.x - S(8.0f), bubbleY0 - S(4.0f)),
                    MenuStyle::Colors::Border.u32());

        const bool profileReady = UserProfile::IsReady();
        const std::string& uname = profileReady
            ? UserProfile::GetUsername()
            : UserSystem::GetUser().username;

        ID3D11ShaderResourceView* tex = profileReady
            ? UserProfile::GetAvatarTexture() : nullptr;
        if (tex) {
            dl->AddCircleFilled(ImVec2(avatarCX, avatarCY), avatarR,
                                MenuStyle::Colors::Border.u32());
            dl->AddImageRounded(reinterpret_cast<ImTextureID>(tex),
                ImVec2(avatarCX - avatarR, avatarCY - avatarR),
                ImVec2(avatarCX + avatarR, avatarCY + avatarR),
                ImVec2(0, 0), ImVec2(1, 1),
                MenuStyle::Colors::White.u32(), avatarR);
        } else if (!uname.empty()) {
            dl->AddCircleFilled(ImVec2(avatarCX, avatarCY), avatarR,
                                MenuStyle::Colors::Accent.u32());
            char letter[2] = {
                static_cast<char>(std::toupper(
                    static_cast<unsigned char>(uname[0]))),
                0
            };
            ImFont* letterFont = MenuStyle::g_fontBold
                ? MenuStyle::g_fontBold : ImGui::GetFont();
            const float letterSize = S(16.0f);
            ImVec2 ls = letterFont->CalcTextSizeA(
                letterSize, FLT_MAX, 0.0f, letter);
            dl->AddText(letterFont, letterSize,
                ImVec2(avatarCX - ls.x * 0.5f, avatarCY - ls.y * 0.5f),
                MenuStyle::Colors::White.u32(), letter);
        }

        // Username centered under the avatar.
        if (!uname.empty()) {
            ImFont* nameFont = MenuStyle::g_fontBold
                ? MenuStyle::g_fontBold : ImGui::GetFont();
            const float nameSize = S(11.5f);
            ImVec2 ns = nameFont->CalcTextSizeA(nameSize, FLT_MAX, 0.0f,
                                                uname.c_str());
            dl->AddText(nameFont, nameSize,
                ImVec2(avatarCX - ns.x * 0.5f, avatarCY + avatarR + S(6.0f)),
                MenuStyle::Colors::Text.u32(), uname.c_str());
        }
    }

    // Advance the ImGui cursor past the rail so subsequent SetCursorPos
    // calls in Render() don't collide.
    ImGui::SetCursorScreenPos(ImVec2(pos.x + railW, pos.y));
    ImGui::Dummy(ImVec2(0, 0));
}

static void RenderBottomTabBar(float menuW) {
    float tabBarH = S(52.0f);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec2 barMax(pos.x + menuW, pos.y + tabBarH);
    dl->AddRectFilled(pos, barMax,
                      MenuStyle::Colors::BarBg.u32(), 10.0f, ImDrawFlags_RoundCornersBottom);

    // Full-width gradient drawn BEHIND the icons (right after the bar bg).
    // Darker at the bottom, transparent at the top. 1 px top inset keeps the border line visible.
    // AddRectFilledMultiColor doesn't support rounded corners, so split it:
    //   upper part = gradient ending at y = bottom - cornerRadius
    //   lower part = solid rounded band of the same alpha, with RoundCornersBottom flag.
    {
        const float cornerR = 10.0f; // matches the bar bg's bottom rounding
        const ImU32 bottomCol = MenuStyle::Colors::TopSubtabSelectedBg.alpha(170 / 255.0f).u32();
        const ImU32 topCol    = MenuStyle::Colors::TopSubtabSelectedBg.alpha(0.0f).u32();
        const float gradTop  = pos.y + 1.0f;
        const float gradBot  = barMax.y - cornerR;
        if (gradBot > gradTop) {
            dl->AddRectFilledMultiColor(
                ImVec2(pos.x, gradTop), ImVec2(barMax.x, gradBot),
                topCol, topCol, bottomCol, bottomCol);
        }
        dl->AddRectFilled(ImVec2(pos.x, gradBot), barMax,
                          bottomCol, cornerR, ImDrawFlags_RoundCornersBottom);
    }

    dl->AddLine(ImVec2(pos.x, pos.y), ImVec2(pos.x + menuW, pos.y),
                MenuStyle::Colors::Border.u32());

    // ── Bottom-left profile bubble ───────────────────────────────────────
    // Non-interactive display (settings cog stays in the title bar). Layout:
    //   [avatar circle]  username
    //                    expires in N days
    // Prefers the live HV-fetched UserProfile (avatar PNG, real expiry);
    // falls back to the UserSystem placeholder when no HV data is in hand.
    {
        const float avatarR = S(12.0f);
        const float padLeft = S(12.0f);
        const float padTextX = S(8.0f);
        const float avatarCX = pos.x + padLeft + avatarR;
        const float avatarCY = pos.y + tabBarH * 0.5f;

        const bool profileReady = UserProfile::IsReady();
        const std::string& uname = profileReady
            ? UserProfile::GetUsername()
            : UserSystem::GetUser().username;

        // Avatar — texture if we have it, otherwise tinted accent disc with
        // the first letter of the username (matches the rust profile bubble).
        ID3D11ShaderResourceView* tex = profileReady
            ? UserProfile::GetAvatarTexture() : nullptr;
        if (tex) {
            dl->AddCircleFilled(ImVec2(avatarCX, avatarCY), avatarR,
                                MenuStyle::Colors::Border.u32());
            dl->AddImageRounded(reinterpret_cast<ImTextureID>(tex),
                ImVec2(avatarCX - avatarR, avatarCY - avatarR),
                ImVec2(avatarCX + avatarR, avatarCY + avatarR),
                ImVec2(0, 0), ImVec2(1, 1),
                MenuStyle::Colors::White.u32(), avatarR);
        } else if (!uname.empty()) {
            dl->AddCircleFilled(ImVec2(avatarCX, avatarCY), avatarR,
                                MenuStyle::Colors::Accent.u32());
            char letter[2] = {
                static_cast<char>(std::toupper(
                    static_cast<unsigned char>(uname[0]))),
                0
            };
            ImFont* letterFont = MenuStyle::g_fontBold
                ? MenuStyle::g_fontBold : ImGui::GetFont();
            const float letterSize = S(15.0f);
            ImVec2 ls = letterFont->CalcTextSizeA(
                letterSize, FLT_MAX, 0.0f, letter);
            dl->AddText(letterFont, letterSize,
                ImVec2(avatarCX - ls.x * 0.5f, avatarCY - ls.y * 0.5f),
                MenuStyle::Colors::White.u32(), letter);
        }

        // Username + expiry stacked to the right of the avatar.
        const float textX = avatarCX + avatarR + padTextX;
        const float textY = pos.y + tabBarH * 0.5f - S(14.0f);

        if (!uname.empty()) {
            ImFont* nameFont = MenuStyle::g_fontBold
                ? MenuStyle::g_fontBold : ImGui::GetFont();
            dl->AddText(nameFont, S(13.0f),
                ImVec2(textX, textY),
                MenuStyle::Colors::Text.u32(),
                uname.c_str());
        }

        // Expiry line — UserProfile's formatted string when available,
        // else build it from UserSystem's day count.
        std::string expiryLine;
        if (profileReady) {
            expiryLine = UserProfile::GetExpiryDisplay();
        } else {
            const int days = UserSystem::GetUser().subscriptionDaysLeft;
            if (days < 0) expiryLine = "lifetime";
            else if (days == 0) expiryLine = "expired";
            else {
                char buf[32];
                std::snprintf(buf, sizeof(buf),
                    "%d day%s left", days, days == 1 ? "" : "s");
                expiryLine = buf;
            }
        }
        if (!expiryLine.empty()) {
            ImFont* expFont = ImGui::GetFont();
            dl->AddText(expFont, S(11.0f),
                ImVec2(textX, textY + S(16.0f)),
                MenuStyle::Colors::TextMuted.u32(),
                expiryLine.c_str());
        }
    }

    // Tabs render. RenderBottomTabs() uses SameLine(offsetX) internally to
    // position the first tab — anchor the ImGui line origin at the bottom-
    // bar's top-left first. The profile bubble above is drawn straight via
    // ImDrawList so it doesn't perturb ImGui's cursor.
    {
        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y));
        ImGui::Dummy(ImVec2(0, 0));
        MenuBuilder::Builder::Get().RenderBottomTabs(menuW, tabBarH);
    }

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + tabBarH));
    ImGui::Dummy(ImVec2(menuW, 0));
}

// ─── Main Render ───────────────────────────────────────────────────────────

void Render() {
    if (!s_initialized) Init();

    // Per-frame client hook — runs BEFORE the menu draw so the client can
    // sync any state its widget schema depends on (rust mirrors world-category
    // booleans, refreshes filtered config view, etc.). Demo build is a no-op.
    TickClient();

    // Per-frame UI tick — periodic GC on the animation-state map so stale
    // entries (widgets that stopped rendering) don't accumulate across the
    // session. Cheap when not on a GC boundary frame, fast sweep otherwise.
    UI::FrameTick();

    KeybindSystem::ProcessKeybinds();

    // Debounced flush of settings.json + periodic active-config auto-save.
    // Clients that expose an autosave toggle push into ConfigSystem from
    // their own TickClient (see demo/demo_menu.cpp).
    ConfigSystem::Tick();

    if (!s_visible) {
        if (s_keybindListPanel)
            KeybindSystem::RenderKeybindListWindow(false);
        DebugWindow::Render(false);
        Notifications::Render();
        return;
    }

    // Advance the open-in animation — lerps 0 → 1 at ~15 dt-scaled per
    // frame (~0.2 s to reach full at 60 fps). Sampled by the participating
    // draw paths (top accent bar, top tabs slide, selected-tab fill grow,
    // MenuStyle::g_globalAlpha) via Menu::GetOpenAnim().
    {
        float dt = ImGui::GetIO().DeltaTime;
        s_openAnim += (1.0f - s_openAnim) * std::min(15.0f * dt, 1.0f);
        if (s_openAnim > 0.999f) s_openAnim = 1.0f;
    }

    // Base menu width — grown at runtime by 2 * the OVERFLOW of the
    // widest chip block beyond what each widget's natural right margin can
    // absorb. Sliders / combos / etc already leave a NonCheckboxIndent gap
    // on the right; small chips fit there for free. Only chips that need
    // more than that gap contribute to menu growth. No binds → no growth.
    const float baseMenuW = S(650.0f);
    float chipGutter = KeybindSystem::MaxChipBlockOverflow(UI::NonCheckboxIndent());
    float menuW = baseMenuW + 2.0f * chipGutter;
    float menuH = S(500.0f);
    float minW  = menuW;
    float minH  = S(500.0f);

    // Persisted size/pos via ConfigSystem — applied with Always after a load,
    // otherwise FirstUseEver so the user can drag/resize freely.
    static ConfigSystem::WindowState* s_menuWin =
        ConfigSystem::RegisterWindow("menu", true);
    if (s_menuWin && s_menuWin->size.x > 0.0f) {
        ImGuiCond cond = s_menuWin->pendingApply ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        ImGui::SetNextWindowSize(s_menuWin->size, cond);
    } else {
        ImGui::SetNextWindowSize(ImVec2(menuW, menuH), ImGuiCond_Once);
    }
    if (s_menuWin && s_menuWin->pos.x >= 0.0f) {
        ImGuiCond cond = s_menuWin->pendingApply ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        ImGui::SetNextWindowPos(s_menuWin->pos, cond);
        s_menuWin->pendingApply = false;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(minW, minH), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    // Open-in fade — two layers, together they cover every draw in the
    // window:
    //  • ImGui::PushStyleVar(Alpha) — scales every color ImGui itself
    //    reads via GetColorU32 (WindowBg, ImGuiCol_Text on native widgets,
    //    frame bgs, etc). Applied around Begin/End.
    //  • MenuStyle::g_globalAlpha — read inside Color::u32() so every raw
    //    color we emit for chrome (container fills, title bar gradient,
    //    tab bodies, accent bar, text shadows, …) fades too. Restored to
    //    1 immediately after ImGui::End() so anything drawn outside the
    //    menu (notifications, popups, debug windows) stays fully opaque.
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_openAnim);
    MenuStyle::g_globalAlpha = s_openAnim;
    // Window bg = MenuLight. Bar / sidebar / container fills all use
    // MenuDark (see MenuStyle::Colors::PanelBg). Change the two values
    // in menu_style.h — this call site never needs touching.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, MenuStyle::Colors::MenuLight);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.149f, 0.149f, 0.161f, 1.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse;
    if (s_lockLayout) flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    auto& builder = MenuBuilder::Builder::Get();

    if (ImGui::Begin("##pdx_menu", &s_visible, flags)) {
        ImGui::SetWindowFontScale(MenuStyle::g_dpiScale);
        menuW = ImGui::GetWindowWidth();
        menuH = ImGui::GetWindowHeight();
        if (s_menuWin) {
            s_menuWin->pos  = ImGui::GetWindowPos();
            s_menuWin->size = ImVec2(menuW, menuH);
        }

        // ── Pandora-style layout ────────────────────────────────────────
        //
        //   ┌──────────────────────────────────────┐
        //   │ [logo]  Tabs...            [cog]     │  top strip (topStripH)
        //   ├──────────┬───────────────────────────┤
        //   │ Subtab   │ [icon selector strip?]    │
        //   │ Subtab   │                           │  sidebar (subtabW) +
        //   │ Subtab   │  content columns          │  scrollable content
        //   ├──────────┴───────────────────────────┤
        //   │ expires in N days  active user: name │  footer (footerH)
        //   └──────────────────────────────────────┘
        //
        // Reserve a fixed subtab column so the frame doesn't reflow when
        // the user switches tabs. Chosen "always show" per Q4.
        float topStripH = S(28.5f);
        float footerH   = S(26.0f);
        float subtabW   = S(140.0f);
        // Outer margin around the sidebar + content region so the sidebar
        // sits "floating" with the same breathing space as the containers.
        // Matches the column pad inside RenderSubTab (kContentPad = 14).
        float outerPad  = S(14.0f);

        // Reserve the full window area up front — every SetCursorPos below
        // moves inside bounds already, so ImGui doesn't spam
        // "SetCursorPos extends window/parent boundaries" errors.
        ImGui::Dummy(ImVec2(menuW, menuH));
        ImGui::SetCursorPos(ImVec2(0, 0));

        // Top strip (logo + tabs + cog)
        RenderTopStrip(menuW);

        // Region between top strip and footer, then inset by outerPad on
        // every edge so the sidebar + content share the same margin as
        // the containers do inside the content area.
        float innerY = topStripH + outerPad;
        float innerH = menuH - topStripH - footerH - 2.0f * outerPad;
        float sidebarX = outerPad;
        float contentX = outerPad + subtabW + outerPad;
        float contentH = innerH;
        float contentW = menuW - contentX - outerPad;

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);

        // Content child matches the window bg (MenuLight) so the space
        // around containers stays as one continuous "lighter" surface.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, MenuStyle::Colors::MenuLight);

        // ── Smooth scroll ──────────────────────────────────────────────────
        // Wheel events adjust s_scrollTarget; s_scrollCurrent lerps toward it each frame.
        // s_lastAppliedScroll snapshots what we wrote into ImGui's scroll
        // state last frame so we can detect drift from the scrollbar drag:
        // if GetScrollY differs from the value we set, the user dragged the
        // grip and we adopt that as the new target (otherwise SetScrollY
        // below would snap it back every frame).
        static float s_scrollTarget       = 0.0f;
        static float s_scrollCurrent      = 0.0f;
        static float s_scrollMax          = 0.0f;
        static float s_lastAppliedScroll  = 0.0f;
        static int   s_prevScrollTab      = -1;
        static int   s_prevScrollSub      = -1;

        int scrollTabKey = builder.GetCurrentTab();
        int scrollSubKey = builder.GetCurrentSubTab();
        if (scrollTabKey != s_prevScrollTab || scrollSubKey != s_prevScrollSub) {
            s_scrollTarget  = 0.0f;
            s_scrollCurrent = 0.0f;
            s_scrollMax     = 0.0f;
            s_prevScrollTab = scrollTabKey;
            s_prevScrollSub = scrollSubKey;
        }

        // Accumulate wheel delta whenever the cursor is anywhere INSIDE the
        // menu window — title bar, subtab strip, bottom bar all forward the
        // wheel to the content scroller. Floating popups (color picker,
        // combo dropdown, keybind config) live in their own root windows
        // and aren't part of this window's child tree, so IsWindowHovered
        // with ChildWindows-only returns false when a popup is hovered —
        // the popup's own wheel handling wins by default.
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
                s_scrollTarget -= wheel * S(90.0f);
        }
        if (s_scrollTarget < 0.0f) s_scrollTarget = 0.0f;
        if (s_scrollTarget > s_scrollMax) s_scrollTarget = s_scrollMax;

        // Lerp current toward target
        {
            float sdt = ImGui::GetIO().DeltaTime;
            s_scrollCurrent += (s_scrollTarget - s_scrollCurrent) * std::min(18.0f * sdt, 1.0f);
            if (std::abs(s_scrollTarget - s_scrollCurrent) < 0.5f) s_scrollCurrent = s_scrollTarget;
        }

        // ── Left subtab sidebar ────────────────────────────────────────
        //
        // Always 140px wide, always shown, even when the current tab has
        // one subtab — keeps the frame stable across tab switches (Q4).
        // Rendered in its own child so SidebarTab's cursor-relative
        // hit-testing is scoped to the sidebar area only.
        ImGui::SetCursorPos(ImVec2(sidebarX, innerY));
        {
            // Sidebar bg = MenuDark, same as the top/bottom bars + the
            // container fills. Window bg is one step brighter (MenuLight)
            // so the sidebar box reads as an inset frame.
            // Kill any inherited WindowPadding for the child so the first
            // subtab sits flush at y=0 and rows stack tight against each
            // other. Also zero ItemSpacing before the SidebarTab loop
            // (also handled in the builder for defence in depth).
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg,
                                  MenuStyle::Colors::MenuDark);
            ImGui::BeginChild("##subtabs", ImVec2(subtabW, innerH),
                              ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoScrollWithMouse |
                              ImGuiWindowFlags_NoScrollbar);
            builder.RenderSubtabSidebar();
            ImVec2 sbWinPos = ImGui::GetWindowPos();
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

            // Sidebar outline drawn on the OUTER window draw list, after
            // EndChild — the child's clip rect stops at innerH, which was
            // clipping the bottom edge of the AddRect when drawn inline.
            {
                ImVec2 mn(sbWinPos.x, sbWinPos.y);
                ImVec2 mx(sbWinPos.x + subtabW, sbWinPos.y + innerH);
                ImGui::GetWindowDrawList()->AddRect(
                    mn, mx, MenuStyle::Colors::Border.u32(), 0.0f, 0, 1.0f);
            }
        }

        // Content child sits to the right of the subtab sidebar. Kill the
        // child's inherited WindowPadding so the vertical scrollbar sits
        // flush against the right edge — no margin, so the containers can
        // extend the full contentW without the scrollbar overpainting them.
        ImGui::SetCursorPos(ImVec2(contentX, innerY));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##center", ImVec2(contentW, contentH),
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();

        // Adopt scrollbar drag: ImGui's scroll value is whatever the user
        // dragged it to BEFORE we re-write it. If it differs from what we
        // pushed last frame, the user is driving — snap our target/current
        // to that value so the lerp doesn't fight the drag.
        {
            float scrollNow = ImGui::GetScrollY();
            if (std::abs(scrollNow - s_lastAppliedScroll) > 0.5f) {
                s_scrollTarget  = scrollNow;
                s_scrollCurrent = scrollNow;
            }
        }

        ImGui::SetScrollY(s_scrollCurrent);
        s_lastAppliedScroll = s_scrollCurrent;

        // Only reserve room for the scrollbar when it was actually shown
        // last frame (s_scrollMax > 0). When content fits, we hand
        // builder the full contentW so the right column stretches to the
        // edge with no wasted gutter. On the first frame content over-
        // flows, the scrollbar briefly overlaps the right column; the
        // next frame's s_scrollMax > 0 kicks in and layout shrinks.
        bool scrollbarWasShown = s_scrollMax > 0.5f;
        float scrollbarReserve = scrollbarWasShown ? ImGui::GetStyle().ScrollbarSize : 0.0f;
        builder.Render(contentW - scrollbarReserve);

        // Capture max scroll for next frame's clamping. EndContainer emits
        // a trailing S(14) Dummy after every container, INCLUDING the last
        // one, which inflates the raw scroll extent past the visible
        // content bottom by that amount. Subtract it back out so the wheel
        // stops right when the last container's bottom aligns with the
        // viewport bottom instead of overscrolling past.
        s_scrollMax = std::max(0.0f, ImGui::GetScrollMaxY() - S(14.0f));

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        // Bottom footer (expiry left, active user right).
        // Footer pinned to the very bottom — sits below the padded content
        // region so the outerPad also breathes above the footer strip.
        ImGui::SetCursorPos(ImVec2(0, menuH - footerH));
        RenderBottomFooter(menuW, footerH);

        // Window border — hard-edged rectangle.
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winMax(winPos.x + menuW, winPos.y + menuH);
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRect(winPos, winMax,
                        MenuStyle::Colors::Border.u32(), 0.0f, 0, 1.0f);
            // Additional black outline wrapping the gray border. Drawn on the
            // foreground drawlist so it escapes the window's clip rect and
            // lands one pixel outside the menu edge.
            ImGui::GetForegroundDrawList()->AddRect(
                ImVec2(winPos.x - 1.0f, winPos.y - 1.0f),
                ImVec2(winMax.x + 1.0f, winMax.y + 1.0f),
                MenuStyle::Colors::Ink.u32(), 0.0f, 0, 1.0f);
            // Accent cap — 1 px tall left-to-right gradient sitting INSIDE
            // the border, just under the 1 px gray outline. Inset 1 px on
            // the left and 2 px on the right. Full Accent on the left,
            // fading to transparent on the right. During the open animation
            // the bar's WIDTH lerps from 0 → full, so it visibly extends
            // from the left as the menu opens in.
            const ImU32 accent = MenuStyle::Colors::Accent.u32();
            const ImU32 clear  = MenuStyle::Colors::Accent.alpha(0.0f).u32();
            float x0 = winPos.x + 1.0f;
            float x1full = winMax.x - 2.0f;
            float x1 = x0 + (x1full - x0) * s_openAnim;
            float y0 = winPos.y + 1.0f;
            float y1 = y0 + 1.0f;
            if (x1 > x0 + 0.5f) {
                dl->AddRectFilledMultiColor(
                    ImVec2(x0, y0), ImVec2(x1, y1),
                    accent, clear, clear, accent);
            }
        }

        KeybindSystem::RenderContextMenu();
        KeybindSystem::RenderKeybindConfigPopup();
        UI::RenderSettingsCogPopup();
        UI::RenderKeybindChipContextMenu();
        KeybindSystem::TickEdit();

        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4); // Alpha + WindowPadding + WindowRounding + WindowBorderSize

    // Restore global alpha to full before drawing anything outside the
    // menu window (notifications, keybind list, debug overlay). Every
    // MenuStyle::Colors::X.u32() call past this point renders opaque.
    MenuStyle::g_globalAlpha = 1.0f;

    if (s_keybindListPanel)
        KeybindSystem::RenderKeybindListWindow(true);

    // Debug overlay — same z-order tier as the keybind list (above menu, below toasts).
    // Watermark, if the client wants one, is drawn by the client between
    // Menu::Render() and its own frame-present (see demo/main.cpp).
    DebugWindow::Render(true);

    // Toast notifications — drawn last on the foreground layer so they overlay everything.
    Notifications::Render();
}

} // namespace Menu
