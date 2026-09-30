#include "ui_components.h"
#include "menu_style.h"
#include "keybind_system.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>

static int s_popupClickConsumedFrame = -1;
#include <vector>
#include <unordered_map>
#include <cmath>

// Shorthand for DPI-scaled pixel values
#define S(x) (MenuStyle::S(x))

namespace UI {

// ─── Animation system ───────────────────────────────────────────────────

// Each entry is (value, lastTouchedFrame). The lastFrame stamp lets us
// periodically GC stale entries (widgets that no longer render) — without
// it the map grows monotonically across the session as new ImGuiIDs are
// generated (listbox item anim IDs, transient widgets, etc.). Sessions that
// scroll long lists or churn through tabs would otherwise leak slowly.
struct AnimEntry {
    float v = 0.0f;
    int   lastFrame = 0;
    // Assignment from a bare float so legacy `s_animState[id] = value;` call
    // sites still compile. Stamps lastFrame at the same time so the GC sees
    // the write as a live touch.
    AnimEntry& operator=(float val) {
        v = val;
        lastFrame = ImGui::GetFrameCount();
        return *this;
    }
};
static std::unordered_map<ImGuiID, AnimEntry> s_animState;
static std::vector<ImGuiID> s_sliderFillIds; // track slider fill anim IDs for selective reset

// Inline color picker active tab per picker
static std::unordered_map<ImGuiID, int> s_colorPickerTab;

// Per-listbox vertical scroll offset. Hoisted from function-local-static
// inside ListBox() because magic-static guards crash under DWM manual-map
// (and other clients reusing this engine inherit the same constraint).
static std::unordered_map<ImGuiID, float> s_listScroll;

// Slider value editing state
static ImGuiID s_editingSlider = 0;        // ID of slider currently being text-edited (0 = none)
static char    s_editBuf[32] = "";         // text input buffer
static bool    s_editJustOpened = false;   // focus helper

// ─── Settings cog click suppression ───────���────────────────────────────
// Cog icon rects — two-frame buffered so the checkbox (rendered first) can check
// rects that the cog (rendered second) registered on the *previous* frame.
static std::vector<ImVec4> s_cogRectsPrev;
static std::vector<ImVec4> s_cogRectsCurrent;
static int s_cogRectsFrameNum = -1;
static std::string s_settingsCogOpen;

static void RegisterCogRect(ImVec2 rmin, ImVec2 rmax) {
    int frame = ImGui::GetFrameCount();
    if (frame != s_cogRectsFrameNum) {
        s_cogRectsPrev = std::move(s_cogRectsCurrent);
        s_cogRectsCurrent.clear();
        s_cogRectsFrameNum = frame;
    }
    s_cogRectsCurrent.push_back(ImVec4(rmin.x, rmin.y, rmax.x, rmax.y));
}

static bool IsMouseOverSettingsCog() {
    ImVec2 mp = ImGui::GetIO().MousePos;
    for (auto& r : s_cogRectsPrev) {
        if (mp.x >= r.x && mp.x <= r.z && mp.y >= r.y && mp.y <= r.w)
            return true;
    }
    return false;
}

// ─── Rounded rect helpers (bypass ImDrawList's cached tessellation) ──────
// Uses PathArcTo with explicit segment counts so small rects get smooth corners.

static void PathRoundRect(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, ImDrawFlags flags = ImDrawFlags_RoundCornersAll, int segments = 4) {
    const float PI = 3.14159265f;
    float rTL = (flags & ImDrawFlags_RoundCornersTopLeft)     ? r : 0.0f;
    float rTR = (flags & ImDrawFlags_RoundCornersTopRight)    ? r : 0.0f;
    float rBR = (flags & ImDrawFlags_RoundCornersBottomRight) ? r : 0.0f;
    float rBL = (flags & ImDrawFlags_RoundCornersBottomLeft)  ? r : 0.0f;

    if (rTL > 0.0f) dl->PathArcTo(ImVec2(a.x + rTL, a.y + rTL), rTL, PI * 1.0f, PI * 1.5f, segments);
    else            dl->PathLineTo(ImVec2(a.x, a.y));

    if (rTR > 0.0f) dl->PathArcTo(ImVec2(b.x - rTR, a.y + rTR), rTR, PI * 1.5f, PI * 2.0f, segments);
    else            dl->PathLineTo(ImVec2(b.x, a.y));

    if (rBR > 0.0f) dl->PathArcTo(ImVec2(b.x - rBR, b.y - rBR), rBR, 0.0f,      PI * 0.5f, segments);
    else            dl->PathLineTo(ImVec2(b.x, b.y));

    if (rBL > 0.0f) dl->PathArcTo(ImVec2(a.x + rBL, b.y - rBL), rBL, PI * 0.5f, PI * 1.0f, segments);
    else            dl->PathLineTo(ImVec2(a.x, b.y));
}

static void RoundRectFilled(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float r, ImDrawFlags flags = ImDrawFlags_RoundCornersAll) {
    if (r <= 0.0f) { dl->AddRectFilled(a, b, col); return; }
    PathRoundRect(dl, a, b, r, flags, 4);
    dl->PathFillConvex(col);
}

static void RoundRectStroke(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float r, float thickness = 1.0f, ImDrawFlags flags = ImDrawFlags_RoundCornersAll) {
    if (r <= 0.0f) { dl->AddRect(a, b, col, 0.0f, 0, thickness); return; }
    PathRoundRect(dl, a, b, r, flags, 4);
    dl->PathStroke(col, ImDrawFlags_Closed, thickness);
}

// Draw a widget fill with a translucent (12,12,12) shadow rising from
// the bottom. Solid-color base first, then a top-transparent /
// bottom-opaque overlay so every widget — regardless of base tone
// (WidgetBase, Accent, hover blends, etc.) — reads with the same
// "dark rises from the bottom" treatment. The overlay's tint is fixed
// to MenuDark; opacity is the per-call knob.
//
//   shadowAlpha  ~120–160  gentle fade (checkbox / textbox / combo bg)
//                ~180–220  strong fade (slider accent, button press)
static void DrawWidgetGradient(ImDrawList* dl, ImVec2 min, ImVec2 max,
                               ImU32 baseCol, int shadowAlpha = 160) {
    dl->AddRectFilled(min, max, baseCol);
    // Overlay tone matches the outline color (Border) so the gradient
    // reads as an extension of the frame's border rather than a
    // separate light source.
    ImU32 topOv = MenuStyle::Colors::Border.alpha(0.0f).u32();
    ImU32 botOv = MenuStyle::Colors::Border.alpha(shadowAlpha / 255.0f).u32();
    dl->AddRectFilledMultiColor(min, max, topOv, topOv, botOv, botOv);
}

// Lerp a stored float toward target. Returns the current animated value.
// New entries start from 0 so transitions are always visible.
//
// Every call stamps the entry's lastFrame so the periodic GC below can
// distinguish "live" entries (touched this session) from "stale" ones
// (widget no longer renders, ImGuiID will never come back). GC runs
// every kAnimGcInterval frames.
static float Animate(ImGuiID id, float target, float speed = 16.0f) {
    int curFrame = ImGui::GetFrameCount();
    float dt = ImGui::GetIO().DeltaTime;
    auto it = s_animState.find(id);
    if (it == s_animState.end()) {
        AnimEntry e; e.v = 0.0f; e.lastFrame = curFrame;
        s_animState[id] = e;
        return 0.0f;
    }
    AnimEntry& e = it->second;
    e.lastFrame = curFrame;
    e.v += (target - e.v) * std::min(speed * dt, 1.0f);
    // Snap when close enough
    if (std::abs(target - e.v) < 0.005f) e.v = target;
    return e.v;
}

// Periodically remove anim entries that haven't been touched for kAnimMaxAge
// frames. Called from EndFrame (hooked below) at kAnimGcInterval cadence.
//
// kAnimMaxAge — entries untouched this long are pruned. ~240 frames at 60Hz
// = ~4 s, more than enough margin for any animation to finish and re-fire,
// but short enough to drop transient listbox/item IDs that won't come back.
//
// kAnimGcInterval — how often the sweep runs. Every ~256 frames keeps the
// per-frame cost negligible.
static constexpr int kAnimMaxAge     = 240;
static constexpr int kAnimGcInterval = 256;

static void GCAnimStateIfDue() {
    int curFrame = ImGui::GetFrameCount();
    if ((curFrame & (kAnimGcInterval - 1)) != 0) return; // run on power-of-two boundaries
    for (auto it = s_animState.begin(); it != s_animState.end(); ) {
        if (curFrame - it->second.lastFrame > kAnimMaxAge) it = s_animState.erase(it);
        else ++it;
    }
}

static ImU32 LerpColor(ImU32 a, ImU32 b, float t) {
    int ra = (a >> 0) & 0xFF, ga = (a >> 8) & 0xFF, ba = (a >> 16) & 0xFF, aa = (a >> 24) & 0xFF;
    int rb = (b >> 0) & 0xFF, gb = (b >> 8) & 0xFF, bb = (b >> 16) & 0xFF, ab = (b >> 24) & 0xFF;
    return IM_COL32(
        (int)(ra + (rb - ra) * t),
        (int)(ga + (gb - ga) * t),
        (int)(ba + (bb - ba) * t),
        (int)(aa + (ab - aa) * t));
}

// Inner shadow: gradient from edges inward, clipped to rect
static void DrawInnerShadow(ImDrawList* dl, ImVec2 rMin, ImVec2 rMax,
                            float pad = 1.0f, float depth = 4.0f,
                            ImU32 col = MenuStyle::Colors::ShadowInk.alpha(45 / 255.0f).u32(),
                            bool skipTop = false, bool skipBottom = false) {
    ImU32 s0 = MenuStyle::Colors::Ink.alpha(0.0f).u32();
    ImVec2 a(rMin.x + pad, rMin.y + pad);
    ImVec2 b(rMax.x - pad, rMax.y - pad);
    dl->PushClipRect(rMin, rMax, true);
    if (!skipTop)    dl->AddRectFilledMultiColor(a, ImVec2(b.x, a.y + depth), col, col, s0, s0);
    if (!skipBottom) dl->AddRectFilledMultiColor(ImVec2(a.x, b.y - depth), b, s0, s0, col, col);
    // Left+right strips fade dark color top-to-bottom on their outer edge.
    // When a top/bottom strip is skipped, the matching corner of the L/R
    // strips must also fade to s0 — otherwise dark bleeds into the corner
    // at the skipped edge and reads as a tiny 1-2 px line near the seam.
    ImU32 lTop = skipTop    ? s0 : col;
    ImU32 lBot = skipBottom ? s0 : col;
    ImU32 rTop = skipTop    ? s0 : col;
    ImU32 rBot = skipBottom ? s0 : col;
    dl->AddRectFilledMultiColor(a, ImVec2(a.x + depth, b.y),
                                lTop, s0, s0, lBot);
    dl->AddRectFilledMultiColor(ImVec2(b.x - depth, a.y), b,
                                s0, rTop, rBot, s0);
    dl->PopClipRect();
}

// Renders a single font glyph rotated around its visual center. Used for the
// container collapse chevron so the same FA icon can spin smoothly between its
// open and collapsed orientations instead of swapping between two glyphs.
// ImFont::RenderChar can only emit axis-aligned quads, so we look up the glyph
// directly and emit a rotated quad via PrimQuadUV.
static void DrawRotatedGlyph(ImDrawList* dl, ImFont* font, float fontSize,
                             ImWchar codepoint, ImVec2 center, float angle, ImU32 col) {
    if (!font) return;
    ImFontBaked* baked = font->GetFontBaked(fontSize);
    if (!baked) return;
    ImFontGlyph* g = baked->FindGlyph(codepoint);
    if (!g || !g->Visible) return;

    // Glyph corners are in pixels at the baked size, relative to a baseline origin.
    // Translate to origin so we can rotate around the glyph's visual center.
    float gcx = (g->X0 + g->X1) * 0.5f;
    float gcy = (g->Y0 + g->Y1) * 0.5f;

    ImVec2 corners[4] = {
        ImVec2(g->X0 - gcx, g->Y0 - gcy),
        ImVec2(g->X1 - gcx, g->Y0 - gcy),
        ImVec2(g->X1 - gcx, g->Y1 - gcy),
        ImVec2(g->X0 - gcx, g->Y1 - gcy),
    };

    float c = std::cos(angle);
    float s = std::sin(angle);
    ImVec2 v[4];
    for (int i = 0; i < 4; i++) {
        v[i].x = center.x + corners[i].x * c - corners[i].y * s;
        v[i].y = center.y + corners[i].x * s + corners[i].y * c;
    }

    dl->PushTexture(font->ContainerAtlas->TexRef);
    dl->PrimReserve(6, 4);
    dl->PrimQuadUV(v[0], v[1], v[2], v[3],
                   ImVec2(g->U0, g->V0), ImVec2(g->U1, g->V0),
                   ImVec2(g->U1, g->V1), ImVec2(g->U0, g->V1), col);
    dl->PopTexture();
}

// ─── Container state stack ──────────────────────────────────────────────

struct CollapseAnim {
    bool  target = false;     // user toggle: false = open, true = collapsed
    float anim = 0.0f;        // 0 = fully open, 1 = fully collapsed
    float bodyHeight = 0.0f;  // last measured natural body height
    bool  measured = false;
};

struct ContainerState {
    ImVec2  startPos;
    ImVec2  titleBarStart;
    ImVec2  bodyTopScreenPos;  // screen pos at body start (just below title)
    const char* label;
    float   width;
    float   innerWidth;
    float   prevInnerWidth;
    bool    bodyOpen;          // body was started this frame
    bool    bodyClipped;       // PushClipRect was called for the body
    float   anim;              // local copy of collapse anim
    ImGuiID containerId;
};

static std::vector<ContainerState> s_containerStack;
static float s_currentInnerWidth = 0.0f;
// Per-frame anim state (visual lerp + body height) keyed by ImGuiID. Resets
// freely on label change — only the visible animation lives here.
static std::unordered_map<ImGuiID, CollapseAnim> s_collapseAnim;
// Stable-string-keyed snapshot of just the user's collapsed/expanded toggle,
// used when callers pass BeginContainer's persistKey. This is what gets
// serialized — the ImGuiID map cannot, because labels duplicate across tabs.
static std::unordered_map<std::string, bool> s_collapseByKey;

// User-saved color presets — see GetColorPresets/SetColorPresets in the
// header. Packed IM_COL32 (ABGR in memory). Drawn at the bottom of every
// picker popup. Capped at kColorPresetMax.
static std::vector<uint32_t> s_colorPresets;

float GetWidgetWidth() {
    if (s_currentInnerWidth > 0.0f) return s_currentInnerWidth;
    return ImGui::GetContentRegionAvail().x;
}

// Every non-checkbox widget gets a left inset equal to a checkbox's
// (boxSize + gap) so that ALL widget labels start at the same X inside a
// container. The right side gets the same inset so the widget body is
// centered with equal padding on both sides. Keep in sync with the
// Checkbox draw's boxSize + gap.
static const float kNonCheckboxIndentBase = 10.0f + 7.0f;
inline float NonCheckboxIndent() { return S(kNonCheckboxIndentBase); }

struct NonCheckboxIndentScope {
    float amount;
    explicit NonCheckboxIndentScope() : amount(NonCheckboxIndent()) {
        ImGui::Indent(amount);
    }
    ~NonCheckboxIndentScope() { ImGui::Unindent(amount); }
};

static float s_prevWidgetWidth = 0.0f;
void PushWidgetWidth(float w) {
    s_prevWidgetWidth = s_currentInnerWidth;
    s_currentInnerWidth = w;
}
void PopWidgetWidth() {
    s_currentInnerWidth = s_prevWidgetWidth;
    s_prevWidgetWidth = 0.0f;
}

// ─── Right-click keybind hook ───────────────────────────────────────────

bool HandleRightClickKeybind(const char* elementId, const char* label,
                             KeybindSystem::ElementType elemType,
                             float fMin, float fMax,
                             int iMin, int iMax,
                             int decimalPlaces,
                             const std::vector<std::string>& comboItems) {
    if (!elementId) return false;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        KeybindSystem::OpenContextMenu(elementId, label, elemType,
                                       fMin, fMax, iMin, iMax,
                                       decimalPlaces, comboItems);
        return true;
    }
    return false;
}

// ─── BeginContainer / EndContainer ──────────────────────────────────────

bool BeginContainer(const char* label, float width, const char* persistKey,
                    bool collapsable) {
    ImGui::PushID(label);

    if (width <= 0.0f) width = ImGui::GetContentRegionAvail().x;

    // Pandora-style: hard-rectangle card with title bar + name at the top.
    // padSide keeps widgets inset from the card border on both sides.
    const float padSide = S(10.0f);
    const float titleBarH = S(26.0f);
    // Top body padding. The title text is vertically centered in the title
    // bar (see the title-draw block below), so half the leftover space
    // (titleBarH - fontSize) sits between the visible title text and the
    // title bar's bottom edge — and because the title bar gradient fades
    // into the body color, that space reads as extra top padding. Subtract
    // it from padSide so the visual gap between the title text and the
    // first widget matches the side margins.
    float titleFontSize = ImGui::GetFontSize();
    float titleTextEmptyBelow = std::max(0.0f, (titleBarH - titleFontSize) * 0.5f);
    const float contentPadTop = std::max(0.0f, padSide - titleTextEmptyBelow);

    ImGuiID containerId = ImGui::GetID(label);
    CollapseAnim& ca = s_collapseAnim[containerId];

    // Non-collapsable containers (e.g. the settings-cog popups) force the
    // toggle open every frame and skip both the click handler and the
    // chevron below. The rest of the animation machinery still runs but
    // stays pinned at target=0 so anim never leaves 0.
    if (!collapsable) ca.target = false;

    // When a stable key is provided, the persistent toggle lives in
    // s_collapseByKey; reconcile it INTO the local anim record so the visual
    // lerp keeps using the same path. If the key has never been seen, leave
    // both at their current default (open) so first-frame doesn't snap.
    std::string persistKeyStr;
    if (collapsable && persistKey && *persistKey) {
        persistKeyStr.assign(persistKey);
        auto it = s_collapseByKey.find(persistKeyStr);
        if (it != s_collapseByKey.end()) ca.target = it->second;
    }

    // Lerp anim toward target each frame.
    {
        float dt = ImGui::GetIO().DeltaTime;
        float targetAnim = ca.target ? 1.0f : 0.0f;
        ca.anim += (targetAnim - ca.anim) * std::min(22.0f * dt, 1.0f);
        if (std::abs(targetAnim - ca.anim) < 0.0008f) ca.anim = targetAnim;
    }

    ContainerState state;
    state.startPos        = ImGui::GetCursorScreenPos();
    state.label           = label;
    state.width           = width;
    state.innerWidth      = width - 2.0f * padSide;
    state.prevInnerWidth  = s_currentInnerWidth;
    state.bodyOpen        = false;
    state.bodyClipped     = false;
    state.anim            = ca.anim;
    state.containerId     = containerId;
    s_containerStack.push_back(state);
    s_currentInnerWidth   = state.innerWidth;

    // Channel split: 0 = background, 1 = content (drawn on top)
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);

    state.titleBarStart = ImGui::GetCursorScreenPos();
    s_containerStack.back().titleBarStart = state.titleBarStart;
    ImVec2 titleMin = state.titleBarStart;
    ImVec2 titleMax(titleMin.x + width, titleMin.y + titleBarH);

    // Manual click detection — NO InvisibleButton. Using InvisibleButton would consume
    // the mouse input and stop ImGui's window-drag from initiating, which means the user
    // couldn't drag the menu by pressing on a container title. Instead we track mouse
    // press + release ourselves and only toggle collapse if the mouse didn't move past
    // a drag threshold (so a press-and-drag moves the menu as expected).
    if (collapsable) {
        static ImGuiID s_pressedTitleId = 0;
        static ImVec2  s_pressedPos;
        ImVec2 mp = ImGui::GetIO().MousePos;
        bool overTitle = mp.x >= titleMin.x && mp.x < titleMax.x &&
                         mp.y >= titleMin.y && mp.y < titleMax.y &&
                         ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

        if (overTitle && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            s_pressedTitleId = containerId;
            s_pressedPos = mp;
        }
        if (s_pressedTitleId == containerId && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            float dx = mp.x - s_pressedPos.x;
            float dy = mp.y - s_pressedPos.y;
            // Drag threshold: if the mouse moved less than ImGui's default drag threshold
            // (~6 px) since press AND is still over the title on release, treat it as a
            // click and toggle. Using the same threshold as ImGui avoids the dead zone
            // where a small movement wouldn't start a drag OR register as a click.
            const float kDragThreshold = ImGui::GetIO().MouseDragThreshold;
            if (dx * dx + dy * dy < kDragThreshold * kDragThreshold && overTitle) {
                ca.target = !ca.target;
                if (!persistKeyStr.empty())
                    s_collapseByKey[persistKeyStr] = ca.target;
            }
            s_pressedTitleId = 0;
        }
    }

    // Advance cursor past the title bar — no ItemSpacing gap.
    ImGui::SetCursorScreenPos(ImVec2(titleMin.x, titleMin.y + titleBarH));
    // Claim the title bar's vertical space in the layout with a size-matched Dummy
    // (since we no longer use an InvisibleButton for click capture). Place the Dummy
    // at titleMin, then rewind the cursor to just below the title.
    ImGui::SetCursorScreenPos(titleMin);
    ImGui::Dummy(ImVec2(width, titleBarH));
    ImGui::SetCursorScreenPos(ImVec2(titleMin.x, titleMin.y + titleBarH));

    // Title text + chevron. Bold label on the title bar strip — dimmed
    // (TextMuted) so it recedes and reads as chrome rather than active
    // widget copy.
    {
        float fontSize = ImGui::GetFontSize();
        float titleY = titleMin.y + (titleBarH - fontSize) * 0.5f;
        ImFont* titleFont = MenuStyle::g_fontBold ? MenuStyle::g_fontBold : ImGui::GetFont();
        float titlePadX = S(10.0f);
        dl->AddText(titleFont, fontSize, ImVec2(titleMin.x + titlePadX, titleY), MenuStyle::Colors::TextMuted.u32(), label);

        // Spinning chevron — same FA glyph as before (f078 down), rotated by ca.anim
        // so it sweeps 90° CCW (visually) into the f054-style right-pointing pose
        // when collapsed. ca.anim itself is already lerped at 22*dt above.
        // Suppressed for non-collapsable containers since it would be a lie.
        if (collapsable && MenuStyle::g_fontIcon) {
            const ImWchar kChevDown = 0xF078;
            float chevSize = S(10.0f);
            // Center the glyph against the right edge of the title bar.
            ImVec2 csize = MenuStyle::g_fontIcon->CalcTextSizeA(chevSize, FLT_MAX, 0.0f, "\xef\x81\xb8");
            ImVec2 chevCenter(titleMin.x + width - titlePadX - csize.x * 0.5f,
                              titleMin.y + titleBarH * 0.5f);
            // Y-down: negative angle = visually CCW. -PI/2 at full collapse takes
            // the down chevron to point right.
            float angle = ca.anim * -1.5707963f;
            DrawRotatedGlyph(dl, MenuStyle::g_fontIcon, chevSize, kChevDown,
                             chevCenter, angle, MenuStyle::Colors::TextMuted.u32());
        }
    }

    // Fully collapsed — skip the body entirely.
    if (ca.anim >= 0.9999f) {
        s_containerStack.back().anim = ca.anim;
        return false;
    }

    // Body uses a clip-rect approach (NOT BeginChild) so we can shrink the visible height
    // by sub-pixel amounts. BeginChild has a 4-9 px minimum which causes a visible jump
    // at the end of the collapse animation.
    ImVec2 bodyTop(titleMin.x, titleMin.y + titleBarH);
    s_containerStack.back().bodyTopScreenPos = bodyTop;
    s_containerStack.back().bodyOpen = true;

    // If animating, push a clip rect on the parent draw list so widgets get visually
    // clipped to the visible body region. The clip can be fractional.
    if (ca.anim > 0.0001f) {
        float visibleH = ca.bodyHeight * (1.0f - ca.anim);
        if (visibleH < 0.0f) visibleH = 0.0f;
        dl->PushClipRect(
            ImVec2(bodyTop.x, bodyTop.y),
            ImVec2(bodyTop.x + width, bodyTop.y + visibleH),
            true);
        s_containerStack.back().bodyClipped = true;
        // Disable input for the (partially-clipped) body widgets so the user can't
        // accidentally click invisible items mid-animation.
        ImGui::BeginDisabled();
    }

    // Body padding above first widget. Zero ItemSpacing.y around the Dummy
    // so the visible gap between the title bar and the first widget is
    // exactly contentPadTop, not contentPadTop + Style.ItemSpacing.y (which
    // is 6 px in this menu — see menu_style.cpp).
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
    ImGui::Dummy(ImVec2(width, contentPadTop));
    ImGui::PopStyleVar();
    ImGui::Indent(padSide);
    ImGui::PushItemWidth(state.innerWidth);

    return true;
}

const std::unordered_map<std::string, bool>& GetCollapseStates() {
    return s_collapseByKey;
}

const std::vector<uint32_t>& GetColorPresets() {
    return s_colorPresets;
}

void SetColorPresets(const std::vector<uint32_t>& presets) {
    s_colorPresets = presets;
    if ((int)s_colorPresets.size() > kColorPresetMax)
        s_colorPresets.resize(kColorPresetMax);
}

void ApplyCollapseStates(const std::unordered_map<std::string, bool>& states) {
    s_collapseByKey = states;
    // Seed the visual anim record at its end-state so a freshly loaded
    // collapsed container snaps closed immediately instead of lerping from
    // open on the first frame after Apply.
    for (auto& kv : states) {
        // The ImGuiID -> anim mapping is label-derived and we don't know it
        // here; rely on BeginContainer's first-frame seed instead. We still
        // want any previously-rendered container with this key to track the
        // new target — the next BeginContainer call will reconcile it.
        (void)kv;
    }
}

void EndContainer() {
    ContainerState& state = s_containerStack.back();
    CollapseAnim& ca = s_collapseAnim[state.containerId];

    if (state.bodyOpen) {
        ImGui::PopItemWidth();
        ImGui::Unindent(S(10.0f)); // matches padSide in BeginContainer
        // Body bottom padding. The last widget already advanced the cursor
        // by Style.ItemSpacing.y (its trailing gap) — subtract it from the
        // Dummy height and zero ItemSpacing.y around the Dummy so the total
        // gap from the last widget's bottom edge to the container's bottom
        // edge is exactly padSide (S(10)), matching top + side padding.
        {
            float itemSpacingY = ImGui::GetStyle().ItemSpacing.y;
            float bottomPad = std::max(0.0f, S(10.0f) - itemSpacingY);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                                ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
            ImGui::Dummy(ImVec2(0, bottomPad));
            ImGui::PopStyleVar();
        }

        // Measure the natural body height by tracking how far the cursor advanced.
        float bodyEndY = ImGui::GetCursorScreenPos().y;
        float naturalH = bodyEndY - state.bodyTopScreenPos.y;

        // Update bodyHeight only when fully open (to avoid stale measurements during animation)
        if (ca.anim < 0.0001f && !ca.target) {
            ca.bodyHeight = naturalH;
            ca.measured = true;
        } else if (!ca.measured) {
            // First-time measurement (e.g. container starts collapsed, then opens)
            ca.bodyHeight = naturalH;
            ca.measured = true;
        }

        if (state.bodyClipped) {
            ImGui::EndDisabled();
            ImGui::GetWindowDrawList()->PopClipRect();

            // Manually rewind the cursor to (bodyTop + visibleH) so the layout shrinks
            // smoothly. visibleH is fractional — sub-pixel updates land cleanly.
            float visibleH = ca.bodyHeight * (1.0f - ca.anim);
            if (visibleH < 0.0f) visibleH = 0.0f;
            ImGui::SetCursorScreenPos(
                ImVec2(state.bodyTopScreenPos.x, state.bodyTopScreenPos.y + visibleH));
        }
    } else {
        // Body was skipped (fully collapsed) — cursor is already at title bottom.
    }

    // Card chrome — draw on channel 0 so it sits BEHIND the widgets
    // committed to channel 1 above. Hard rectangles, zero rounding.
    // Body = PanelBg (MenuDark). Title bar = MenuLight-top → PanelBg-
    // bottom gradient so the strip lifts subtly out of the body without
    // needing a separator line between them (the fade itself does the
    // dividing).
    ImVec2 mn = state.startPos;
    ImVec2 mx(mn.x + state.width, ImGui::GetCursorScreenPos().y);
    const float titleBarH = S(26.0f);
    if (mx.y < mn.y + titleBarH) mx.y = mn.y + titleBarH;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSetCurrent(0);
    // Body fill first (whole card).
    dl->AddRectFilled(mn, mx, MenuStyle::Colors::PanelBg.u32());
    // Title bar gradient — Border color at 20% alpha at the top, fading
    // to fully transparent at the title bar's bottom edge. Overlaid on the
    // already-drawn PanelBg body so the strip lifts subtly out of the
    // body without needing a hard separator line.
    {
        ImU32 topCol = MenuStyle::Colors::Border.alpha(0.20f).u32();
        ImU32 botCol = MenuStyle::Colors::Border.alpha(0.00f).u32();
        dl->AddRectFilledMultiColor(
            mn, ImVec2(mx.x, mn.y + titleBarH),
            topCol, topCol, botCol, botCol);
    }
    // Outer 1 px border around the whole card. No hairline separator
    // between title and body — the gradient replaces it.
    dl->AddRect(mn, mx, MenuStyle::Colors::Border.u32(), 0.0f, 0, 1.0f);
    // Additional black outline wrapping the gray border for extra silhouette.
    dl->AddRect(ImVec2(mn.x - 1.0f, mn.y - 1.0f),
                ImVec2(mx.x + 1.0f, mx.y + 1.0f),
                MenuStyle::Colors::Ink.u32(), 0.0f, 0, 1.0f);
    dl->ChannelsMerge();

    // Inter-container gap — match the column gap exactly (kContentPad = S(14)).
    // Push ItemSpacing.y to 0 around the Dummy so the gap isn't doubled by ImGui's
    // implicit ItemSpacing.y between items.
    // Cursor is already at the container's current bottom edge from the
    // body flow above (or from the collapsed title-only branch); use it
    // directly instead of recomputing from the removed card-bg bounds.
    ImVec2 gapAnchor = ImGui::GetCursorScreenPos();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::SetCursorScreenPos(gapAnchor);
    ImGui::Dummy(ImVec2(0, S(14.0f)));
    ImGui::PopStyleVar();

    s_currentInnerWidth = state.prevInnerWidth;
    s_containerStack.pop_back();
    ImGui::PopID();
}

// ─── Checkbox ───────────────────────────────────────────────────────────

bool Checkbox(const char* label, bool* v, const char* elementId, float reserveRight) {
    if (elementId) {
        KeybindSystem::RegisterVar(elementId, v);
    }

    // Pandora-style — small flat square, tight gap to the label. Sized
    // to match the slider track height so the two widgets read as the
    // same "unit" thickness. Row height is exactly the label line height
    // (or the box, whichever is bigger) so stacked checkboxes only leave
    // the shared ItemSpacing gap between rows — matches sliders/combos.
    const float boxSize = S(10.0f);
    const float gap = S(7.0f);
    const float rounding = 0.0f;
    const float rowH = std::max(ImGui::GetTextLineHeight(), boxSize);

    float width = GetWidgetWidth();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    float clickW = (reserveRight > 0.0f) ? (width - reserveRight) : width;
    ImGui::PushID(label);
    bool pressed = ImGui::InvisibleButton("##cb", ImVec2(clickW, rowH));
    ImGui::SetItemAllowOverlap(); // allow settings cog button to capture clicks on top
    // Suppress toggle when the click landed on a settings cog icon
    if (pressed && !IsMouseOverSettingsCog()) *v = !*v;
    bool hovered = ImGui::IsItemHovered();
    ImGuiID id = ImGui::GetItemID();
    HandleRightClickKeybind(elementId, label, KeybindSystem::ElementType::Checkbox);

    // Animate check state and hover
    float checkAnim = Animate(id, *v ? 1.0f : 0.0f);
    float hoverAnim = Animate(id + 1, hovered ? 1.0f : 0.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Snap cy to integer pixel — a fractional Y (rowH-boxSize odd) landed
    // the fill on a half-row while the 1px outline stayed on the whole
    // row above/below, exposing a 1px band of the checked fill (accent)
    // OR the WidgetBase top-line above the outline. Integer coords keep
    // the fill exactly under the outline.
    float cy = std::floor(pos.y + (rowH - boxSize) * 0.5f + 0.5f);
    ImVec2 boxMin(std::floor(pos.x), cy);
    ImVec2 boxMax(std::floor(pos.x + boxSize), cy + std::floor(boxSize));

    // Pandora-style — every state gets the top-down highlight gradient
    // on top of a WidgetBase base; checked interpolates the fill toward
    // accent. Uniform pure-black 1px outline, always drawn.
    ImU32 baseFill = MenuStyle::Colors::WidgetBase.u32();
    if (checkAnim > 0.01f) {
        baseFill = LerpColor(baseFill, MenuStyle::Colors::Accent.u32(), checkAnim);
    }
    // Off state fades to MenuDark at the bottom (widget bg); checked state
    // leaves darkCol == 0 so the accent picks its own subtle darker shade.
    if (checkAnim > 0.01f) {
        DrawWidgetGradient(dl, boxMin, boxMax, baseFill);
    } else {
        DrawWidgetGradient(dl, boxMin, boxMax, baseFill);
    }
    dl->AddRect(boxMin, boxMax, MenuStyle::Colors::WidgetOutline.u32(), 0.0f, 0, 1.0f);

    // Label — checked = --text, unchecked = --muted (hover lifts to --text).
    ImU32 offCol = LerpColor(MenuStyle::Colors::TextMuted.u32(), MenuStyle::Colors::Text.u32(), hoverAnim);
    ImU32 onCol  = MenuStyle::Colors::Text.u32();
    ImU32 textCol = LerpColor(offCol, onCol, checkAnim);
    // Center the visible glyph range (cap-top → baseline) on the box's
    // vertical centre, then nudge UP by kLabelUpShift pixels to
    // compensate for the font's rendering (the previous ink-centered
    // formula still landed the label visibly LOW on this build's font;
    // this fixed shift is the empirical correction). If the label reads
    // as too HIGH now, decrease kLabelUpShift; too LOW, increase it.
    const float kLabelUpShift = S(2.0f);
    ImFontBaked* baked = ImGui::GetFontBaked();
    float ascent = baked ? baked->Ascent : ImGui::GetFontSize();
    float boxCenterY = cy + boxSize * 0.5f;
    float textY = std::floor(boxCenterY - ascent * 0.5f - kLabelUpShift + 0.5f);
    ImVec2 textPos(pos.x + boxSize + gap, textY);
    MenuStyle::TextShadow(dl, textPos, textCol, label);

    ImGui::PopID();
    return pressed;
}

// ─── SliderFloat ────────────────────────────────────────────────────────

bool SliderFloat(const char* label, float* v, float v_min, float v_max,
                 int decimalPlaces, const char* unit, const char* elementId) {
    if (elementId) {
        KeybindSystem::RegisterVar(elementId, v);
    }

    ImGui::PushID(label);

    NonCheckboxIndentScope _indent;
    float width = GetWidgetWidth() - 2.0f * NonCheckboxIndent();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 base = ImGui::GetCursorScreenPos();
    float lineH = ImGui::GetTextLineHeight();
    // Pandora-style — sized to match the checkbox square so both widgets
    // read at the same "unit" thickness.
    float trackH = S(10.0f);
    float trackRound = 0.0f;
    float totalH = lineH + S(6.0f) + trackH;

    // Format value text
    char valueBuf[48];
    char fmt[16];
    snprintf(fmt, sizeof(fmt), "%%.%df", decimalPlaces);
    if (unit && unit[0]) {
        char num[32];
        snprintf(num, sizeof(num), fmt, *v);
        snprintf(valueBuf, sizeof(valueBuf), "%s%s", num, unit);
    } else {
        snprintf(valueBuf, sizeof(valueBuf), fmt, *v);
    }

    ImGuiID id = ImGui::GetID("##slider");
    bool isEditing = (s_editingSlider == id);

    // ── Value badge dimensions ──
    ImVec2 valTextSize = ImGui::CalcTextSize(valueBuf);
    float badgePadX = S(6.0f);
    float badgePadY = S(2.0f);
    float badgeW = valTextSize.x + badgePadX * 2.0f;
    float badgeH = valTextSize.y + badgePadY * 2.0f;
    float badgeX = base.x + width - badgeW;
    float badgeY = base.y + (lineH - badgeH) * 0.5f;
    ImVec2 badgeMin(badgeX, badgeY);
    ImVec2 badgeMax(badgeX + badgeW, badgeY + badgeH);

    if (!isEditing) {
        // ── Normal slider mode ──

        // Slider interaction area (full widget minus the badge area on top row)
        ImGui::InvisibleButton("##slider", ImVec2(width, totalH));
        bool active = ImGui::IsItemActive();
        bool hovered = ImGui::IsItemHovered();
        HandleRightClickKeybind(elementId, label, KeybindSystem::ElementType::SliderFloat,
                                v_min, v_max, 0, 0, decimalPlaces);

        // Check if click landed on the value badge
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        bool badgeHovered = mousePos.x >= badgeMin.x && mousePos.x <= badgeMax.x &&
                            mousePos.y >= badgeMin.y && mousePos.y <= badgeMax.y;

        // Settings cog sits inside the slider's row (label-trailing). The
        // InvisibleButton above already grabbed the down-event as a drag —
        // suppress drag + edit-mode entry whenever the mouse is over a cog
        // rect from the previous frame. Same trick Checkbox uses; the
        // cog's own click handler still fires via its raw rect hit-test
        // and opens the popup.
        bool overCog = IsMouseOverSettingsCog();
        if (overCog) active = false;

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && badgeHovered && !overCog) {
            // Enter edit mode
            s_editingSlider = id;
            snprintf(s_editBuf, sizeof(s_editBuf), fmt, *v);
            s_editJustOpened = true;
            active = false; // don't drag on this click
        } else if (active && !badgeHovered) {
            float mouseX = ImGui::GetIO().MousePos.x;
            float t = (mouseX - base.x) / width;
            t = std::clamp(t, 0.0f, 1.0f);
            *v = v_min + t * (v_max - v_min);
        }

        // Animate hover
        float hoverAnim = Animate(id, (hovered || active) ? 1.0f : 0.0f);

        // Off state: value at its natural resting point — v_min for a
        // one-sided range, 0 for a range that spans negatives. That's the
        // "hasn't been touched from default" state and we dim the label
        // to match the checkbox convention. Anything else lifts the label
        // to Text (white). Hover further lifts on top.
        float baseline = (v_min < 0.0f) ? 0.0f : v_min;
        bool isOff = (*v == baseline);
        ImU32 offCol = LerpColor(MenuStyle::Colors::TextMuted.u32(),
                                 MenuStyle::Colors::Text.u32(), hoverAnim);
        ImU32 onCol  = MenuStyle::Colors::Text.u32();
        ImU32 labelCol = isOff ? offCol : onCol;
        MenuStyle::TextShadow(dl, base, labelCol, label);

        // Value (right, right-aligned) — same color as the label so the
        // row reads as one unit; the accent lives on the fill bar only.
        {
            ImVec2 valTS = ImGui::CalcTextSize(valueBuf);
            float vx = base.x + width - valTS.x;
            MenuStyle::TextShadow(dl, ImVec2(vx, base.y), labelCol, valueBuf);

            // Cache these into badge* so the edit-mode branch can reuse
            // approximately the same InputText geometry.
            badgeW = valTS.x + badgePadX * 2.0f;
            badgeH = valTS.y + badgePadY * 2.0f;
            badgeX = vx - badgePadX;
            badgeY = base.y - badgePadY;
            (void)badgeMin; (void)badgeMax;
        }

        // Track — MenuDark base gradient across the whole track, accent
        // gradient over the filled portion, uniform black outline.
        float trackY = base.y + lineH + S(6.0f);
        ImVec2 trackMin(base.x, trackY);
        ImVec2 trackMax(base.x + width, trackY + trackH);
        DrawWidgetGradient(dl, trackMin, trackMax, MenuStyle::Colors::WidgetBase.u32());

        // Filled section (animated).
        float t = std::clamp((*v - v_min) / (v_max - v_min), 0.0f, 1.0f);
        ImGuiID fillId = id + 2;
        if (s_animState.find(fillId) == s_animState.end()) {
            s_animState[fillId] = 0.0f;
            s_sliderFillIds.push_back(fillId);
        }
        if (active) s_animState[fillId] = t;
        float animT = Animate(fillId, t, 16.0f);
        float fillX = trackMin.x + width * animT;
        // Accent fill sits INSIDE the black outline — inset by 1px on
        // every edge so the outline is visible around it, not painted
        // over it.
        ImVec2 fMin(trackMin.x + 1.0f, trackMin.y + 1.0f);
        float clampedFillX = std::min(fillX, trackMax.x - 1.0f);
        ImVec2 fMax(clampedFillX, trackMax.y - 1.0f);
        if (fMax.x > fMin.x + 0.5f && fMax.y > fMin.y + 0.5f) {
            DrawWidgetGradient(dl, fMin, fMax, MenuStyle::Colors::Accent.u32());
        }
        dl->AddRect(trackMin, trackMax, MenuStyle::Colors::WidgetOutline.u32(), 0.0f, 0, 1.0f);

        ImGui::PopID();
        return active;
    } else {
        // ── Editing mode: show InputText in place of the value badge ──

        // Draw label
        MenuStyle::TextShadow(dl, base, MenuStyle::Colors::Text.u32(), label);

        // Draw track (static, no interaction). Same gradient + outline
        // treatment as the display branch.
        float trackY = base.y + lineH + S(6.0f);
        ImVec2 trackMin(base.x, trackY);
        ImVec2 trackMax(base.x + width, trackY + trackH);
        DrawWidgetGradient(dl, trackMin, trackMax, MenuStyle::Colors::WidgetBase.u32());

        float t = std::clamp((*v - v_min) / (v_max - v_min), 0.0f, 1.0f);
        ImGuiID fillId = id + 2;
        if (s_animState.find(fillId) == s_animState.end()) {
            s_animState[fillId] = 0.0f;
            s_sliderFillIds.push_back(fillId);
        }
        float animT = Animate(fillId, t, 16.0f);
        float fillX = trackMin.x + width * animT;
        // Accent inset 1px inside the outline, same as the display branch.
        ImVec2 fMin(trackMin.x + 1.0f, trackMin.y + 1.0f);
        float clampedFillX = std::min(fillX, trackMax.x - 1.0f);
        ImVec2 fMax(clampedFillX, trackMax.y - 1.0f);
        if (fMax.x > fMin.x + 0.5f && fMax.y > fMin.y + 0.5f) {
            DrawWidgetGradient(dl, fMin, fMax, MenuStyle::Colors::Accent.u32());
        }
        dl->AddRect(trackMin, trackMax, MenuStyle::Colors::WidgetOutline.u32(), 0.0f, 0, 1.0f);

        // InputText sized to fit the current edit buffer, matching badge styling.
        // Width = text + 2*badgePadX + small extra for the caret. Right-aligned like the badge.
        const char* measureStr = s_editBuf[0] ? s_editBuf : "0";
        float editTextW = ImGui::CalcTextSize(measureStr).x;
        float inputW = editTextW + 2.0f * badgePadX + S(4.0f);
        float inputX = base.x + width - inputW;
        ImVec2 inputMin(inputX, badgeY);
        ImVec2 inputMax(inputX + inputW, badgeY + badgeH);

        // Frame matches every other widget: MenuDark gradient + black outline.
        DrawWidgetGradient(dl, inputMin, inputMax, MenuStyle::Colors::WidgetBase.u32());
        dl->AddRect(inputMin, inputMax, MenuStyle::Colors::WidgetOutline.u32(), 0.0f, 0, 1.0f);

        // Put InputText on top with a transparent frame so only the text/caret/selection draw
        ImGui::SetCursorScreenPos(inputMin);
        ImGui::SetNextItemWidth(inputW);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(badgePadX, badgePadY));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text,           ImVec4(0.843f, 0.843f, 0.843f, 1.0f));
        // Text selection uses the menu accent color (with alpha so the text stays readable)
        ImVec4 selCol = MenuStyle::Colors::Accent.v;
        selCol.w = 0.45f;
        ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, selCol);

        if (s_editJustOpened) {
            ImGui::SetKeyboardFocusHere();
            s_editJustOpened = false;
        }

        ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                    ImGuiInputTextFlags_AutoSelectAll;
        bool committed = ImGui::InputText("##edit", s_editBuf, sizeof(s_editBuf), flags);

        // Commit on Enter or when focus is lost
        bool focusLost = !ImGui::IsItemActive() && !s_editJustOpened && ImGui::IsItemDeactivated();
        if (committed || focusLost) {
            float parsed = (float)atof(s_editBuf);
            *v = std::clamp(parsed, v_min, v_max);
            s_editingSlider = 0;
        }

        // Cancel on Escape
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            s_editingSlider = 0;
        }

        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(3);

        // Reserve the total height so layout doesn't collapse
        ImGui::SetCursorScreenPos(ImVec2(base.x, base.y + totalH));
        ImGui::Dummy(ImVec2(0, 0));

        ImGui::PopID();
        return false;
    }
}

// ─── SliderInt ──────────────────────────────────────────────────────────

bool SliderInt(const char* label, int* v, int v_min, int v_max,
               const char* unit, const char* elementId) {
    if (elementId) {
        KeybindSystem::RegisterVar(elementId, v);
    }

    float fv = (float)*v;
    // Pass nullptr for elementId so SliderFloat doesn't register its own right-click
    bool changed = SliderFloat(label, &fv, (float)v_min, (float)v_max, 0, unit, nullptr);
    // Handle right-click as SliderInt type
    HandleRightClickKeybind(elementId, label, KeybindSystem::ElementType::SliderInt,
                            0, 0, v_min, v_max);
    int newV = (int)(fv + 0.5f);
    newV = std::clamp(newV, v_min, v_max);
    if (newV != *v) { *v = newV; return true; }
    return changed;
}

// ─── Combo ──────────────────────────────────────────────────────────────

// Per-combo animation state
static std::unordered_map<ImGuiID, float> s_comboAnim;

// Solid-triangle chevron that rotates 180° between closed (points down,
// anim=0) and open (points up, anim=1). Same three-vertex shape either
// way — only the orientation animates, so opening / closing the combo
// gives a smooth spin.
static void DrawComboArrow(ImDrawList* dl, ImVec2 center, float sz, float anim, ImU32 col) {
    ImVec2 local[3] = {
        ImVec2(-sz * 0.5f, -sz * 0.3f),
        ImVec2( sz * 0.5f, -sz * 0.3f),
        ImVec2( 0.0f,       sz * 0.3f),
    };

    const float PI = 3.14159265f;
    float angle = anim * PI; // 0 → π : 180° flip from down to up
    float c = std::cos(angle);
    float s = std::sin(angle);

    ImVec2 v[3];
    for (int i = 0; i < 3; i++) {
        v[i].x = center.x + local[i].x * c - local[i].y * s;
        v[i].y = center.y + local[i].x * s + local[i].y * c;
    }
    dl->AddTriangleFilled(v[0], v[1], v[2], col);
}

bool Combo(const char* label, int* current_item, const char* const items[],
           int items_count, const char* elementId) {
    if (elementId) {
        KeybindSystem::RegisterVar(elementId, current_item);
    }

    ImGui::PushID(label);

    NonCheckboxIndentScope _indent;
    float width = GetWidgetWidth() - 2.0f * NonCheckboxIndent();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 base = ImGui::GetCursorScreenPos();
    float lineH = ImGui::GetTextLineHeight();
    float rounding = 0.0f;

    // Label row — reserve layout space now; the actual label text is
    // drawn AFTER hover detection below so we can tint it on hover.
    ImGui::SetCursorScreenPos(base);
    ImGui::Dummy(ImVec2(width, lineH));

    // Frame dimensions
    float frameH = lineH + S(10.0f);
    ImVec2 frameMin = ImGui::GetCursorScreenPos();
    ImVec2 frameMax(frameMin.x + width, frameMin.y + frameH);

    // Animation
    ImGuiID comboId = ImGui::GetID("##cdrop");
    float& anim = s_comboAnim[comboId];
    bool popupOpen = ImGui::IsPopupOpen("##cdrop");
    float animTarget = popupOpen ? 1.0f : 0.0f;
    anim += (animTarget - anim) * ImGui::GetIO().DeltaTime * 22.0f;
    if (anim < 0.01f) anim = 0.0f;
    if (anim > 0.99f) anim = 1.0f;

    // Colors — MenuDark base with a small hover lift; uniform black outline.
    ImU32 frameBg     = MenuStyle::Colors::WidgetBase.u32();
    ImU32 frameBorder = MenuStyle::Colors::WidgetOutline.u32();

    // Click area
    ImGui::SetCursorScreenPos(frameMin);
    ImGui::InvisibleButton("##combo_btn", ImVec2(width, frameH));
    bool hovered = ImGui::IsItemHovered();
    bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        ImGui::OpenPopup("##cdrop");

    // Lerped hover — base color shifts a whisker brighter on hover; the
    // top-down gradient sits on top.
    float hoverAnim = Animate(comboId + 100, hovered ? 1.0f : 0.0f);
    ImU32 frameFill = LerpColor(frameBg, MenuStyle::Colors::Border.u32(), hoverAnim);

    // Draw gradient fill + black outline. The rounded-corners flag is
    // still forwarded but with 0 rounding the flag is a no-op.
    ImDrawFlags frameFlags = popupOpen
        ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll;
    DrawWidgetGradient(dl, frameMin, frameMax, frameFill);
    dl->AddRect(frameMin, frameMax, frameBorder, rounding, frameFlags, 1.0f);

    // Label — combos are always "on" (they always carry a selection), so
    // the label stays at Text (white) regardless of hover.
    MenuStyle::TextShadow(dl, base, MenuStyle::Colors::Text.u32(), label);

    // Selected item text — dimmed by default (reads as a "collapsed"
    // value), matching the old visual weight so the closed combo
    // doesn't shout its selection.
    ImVec2 textPos(frameMin.x + S(8.0f), frameMin.y + (frameH - lineH) * 0.5f);
    MenuStyle::TextShadow(dl, textPos, MenuStyle::Colors::TextMuted.u32(), items[*current_item]);

    // Arrow — single shape, rotates smoothly with anim (0=down, 1=up)
    float arrowSz = S(6.0f);
    ImVec2 arrowCenter(frameMax.x - S(16.0f) + arrowSz * 0.5f,
                       frameMin.y + frameH * 0.5f);
    DrawComboArrow(dl, arrowCenter, arrowSz, anim, MenuStyle::Colors::IconGray.u32());

    // Popup dropdown
    bool changed = false;
    float itemH = lineH + S(6.0f);
    float popupPadY = S(4.0f);
    float fullH = items_count * itemH + popupPadY * 2.0f;
    float animH = (std::max)(fullH * anim, 1.0f);

    ImGui::SetNextWindowPos(ImVec2(frameMin.x, frameMax.y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, animH), ImVec2(width, animH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

    if (ImGui::BeginPopup("##cdrop", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar)) {
        ImVec2 popMin = ImGui::GetWindowPos();
        ImVec2 popMax(popMin.x + width, popMin.y + animH);
        auto* popDl = ImGui::GetWindowDrawList();

        // Popup bg — solid WidgetBase fill (no top-down gradient) so the
        // dropdown reads as a flat surface distinct from the closed frame.
        popDl->AddRectFilled(popMin, popMax, frameBg);

        // Dropdown border: sides + rounded bottom (no top line)
        {
            auto* fgDl = ImGui::GetForegroundDrawList();
            fgDl->AddRect(popMin, popMax, frameBorder, rounding, ImDrawFlags_RoundCornersBottom, 1.0f);
            // Cover the top border line with bg
            fgDl->AddLine(ImVec2(popMin.x + 1.0f, popMin.y + 0.5f),
                          ImVec2(popMax.x - 1.0f, popMin.y + 0.5f), frameBg, 1.0f);
        }

        ImGui::PushClipRect(popMin, popMax, true);

        // Custom-drawn items (no ImGui::Selectable)
        float curY = popupPadY;
        for (int i = 0; i < items_count; i++) {
            bool sel = (*current_item == i);
            ImVec2 iMin(popMin.x + S(2), popMin.y + curY);
            ImVec2 iMax(popMax.x - S(2), popMin.y + curY + itemH);

            // Hit test
            ImGui::SetCursorScreenPos(iMin);
            char itemId[32];
            snprintf(itemId, sizeof(itemId), "##ci%d", i);
            ImGui::InvisibleButton(itemId, ImVec2(iMax.x - iMin.x, itemH));
            bool itemHovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) {
                s_popupClickConsumedFrame = ImGui::GetFrameCount();
                *current_item = i;
                changed = true;
                ImGui::CloseCurrentPopup();
            }

            // Lerped hover highlight
            ImGuiID itemAnimId = ImGui::GetID(itemId) + 200;
            float ihAnim = Animate(itemAnimId, itemHovered ? 1.0f : 0.0f, 20.0f);
            if (ihAnim > 0.01f) {
                ImU32 hoverCol = MenuStyle::Colors::ShadowInk.alpha((ihAnim * 30) / 255.0f).u32();
                popDl->AddRectFilled(iMin, iMax, hoverCol);
            }

            // Selected row: accent-coloured text only (no bold weight
            // change) so the picked option pops without shifting layout.
            ImU32 textCol = sel ? MenuStyle::Colors::Accent.u32() : MenuStyle::Colors::TextBright.alpha(220 / 255.0f).u32();
            ImVec2 tp(iMin.x + S(8.0f), iMin.y + (itemH - lineH) * 0.5f);
            popDl->AddText(tp, textCol, items[i]);

            curY += itemH;
        }

        ImGui::PopClipRect();
        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    // Right-click keybind (use saved state from invisible button)
    if (rightClicked && elementId) {
        std::vector<std::string> itemsVec;
        for (int i = 0; i < items_count; i++) itemsVec.push_back(items[i]);
        KeybindSystem::OpenContextMenu(elementId, label, KeybindSystem::ElementType::Combo,
                                       0, 0, 0, items_count - 1, 0, itemsVec);
    }

    ImGui::PopID();
    return changed;
}

// ─── MultiCombo ─────────────────────────────────────────────────────────

bool MultiCombo(const char* label, bool selected[], const char* const items[],
                int items_count, const char* elementId) {
    if (elementId) {
        KeybindSystem::RegisterVar(elementId, selected);
    }
    ImGui::PushID(label);

    NonCheckboxIndentScope _indent;
    float width = GetWidgetWidth() - 2.0f * NonCheckboxIndent();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 base = ImGui::GetCursorScreenPos();
    float lineH = ImGui::GetTextLineHeight();
    float rounding = 0.0f;

    // Label row — reserve layout space now; the actual label text is
    // drawn AFTER hover detection below so we can tint it on hover.
    ImGui::SetCursorScreenPos(base);
    ImGui::Dummy(ImVec2(width, lineH));

    // Build preview
    std::string preview;
    for (int i = 0; i < items_count; i++) {
        if (selected[i]) {
            if (!preview.empty()) preview += ", ";
            preview += items[i];
        }
    }
    if (preview.empty()) preview = "-";

    // Frame dimensions
    float frameH = lineH + S(10.0f);
    ImVec2 frameMin = ImGui::GetCursorScreenPos();
    ImVec2 frameMax(frameMin.x + width, frameMin.y + frameH);

    // Animation
    ImGuiID comboId = ImGui::GetID("##mdrop");
    float& anim = s_comboAnim[comboId];
    bool popupOpen = ImGui::IsPopupOpen("##mdrop");
    float animTarget = popupOpen ? 1.0f : 0.0f;
    anim += (animTarget - anim) * ImGui::GetIO().DeltaTime * 22.0f;
    if (anim < 0.01f) anim = 0.0f;
    if (anim > 0.99f) anim = 1.0f;

    // Same treatment as single-select Combo — MenuDark base, hover lifts
    // toward MenuLight, uniform top-down gradient, black outline.
    ImU32 frameBg     = MenuStyle::Colors::WidgetBase.u32();
    ImU32 frameBorder = MenuStyle::Colors::WidgetOutline.u32();

    // Click area
    ImGui::SetCursorScreenPos(frameMin);
    ImGui::InvisibleButton("##multi_btn", ImVec2(width, frameH));
    bool hovered = ImGui::IsItemHovered();
    bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        ImGui::OpenPopup("##mdrop");

    // Lerped hover
    float hoverAnim = Animate(comboId + 100, hovered ? 1.0f : 0.0f);
    ImU32 frameFill = LerpColor(frameBg, MenuStyle::Colors::Border.u32(), hoverAnim);

    // Draw gradient fill + black outline.
    ImDrawFlags frameFlags = popupOpen
        ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll;
    DrawWidgetGradient(dl, frameMin, frameMax, frameFill);

    // Frame border
    dl->AddRect(frameMin, frameMax, frameBorder, rounding, frameFlags, 1.0f);

    DrawInnerShadow(dl, frameMin, frameMax);

    // Label — drawn now that hover is known. "Off" state = no items
    // selected (empty preview) → dim TextMuted. "On" = at least one item
    // selected → Text (white). Hover lifts the off state toward white so
    // the affordance still reads.
    {
        int selCount = 0;
        for (int i = 0; i < items_count; i++) if (selected[i]) { ++selCount; break; }
        bool isOff = (selCount == 0);
        ImU32 offCol = LerpColor(MenuStyle::Colors::TextMuted.u32(),
                                 MenuStyle::Colors::Text.u32(), hoverAnim);
        ImU32 onCol  = MenuStyle::Colors::Text.u32();
        ImU32 labelCol = isOff ? offCol : onCol;
        MenuStyle::TextShadow(dl, base, labelCol, label);
    }

    // Preview text (clip to frame width minus arrow area)
    ImVec2 textPos(frameMin.x + S(8.0f), frameMin.y + (frameH - lineH) * 0.5f);
    dl->PushClipRect(ImVec2(frameMin.x + S(8.0f), frameMin.y),
                     ImVec2(frameMax.x - S(24.0f), frameMax.y), true);
    MenuStyle::TextShadow(dl, textPos, MenuStyle::Colors::TextMuted.u32(), preview.c_str());
    dl->PopClipRect();

    // Arrow — single shape, rotates smoothly with anim (0=down, 1=up)
    float arrowSz = S(6.0f);
    ImVec2 arrowCenter(frameMax.x - S(16.0f) + arrowSz * 0.5f,
                       frameMin.y + frameH * 0.5f);
    DrawComboArrow(dl, arrowCenter, arrowSz, anim, MenuStyle::Colors::IconGray.u32());

    // Popup dropdown
    bool changed = false;
    float itemH = lineH + S(6.0f);
    float popupPadY = S(4.0f);
    float fullH = items_count * itemH + popupPadY * 2.0f;
    float animH = (std::max)(fullH * anim, 1.0f);

    ImGui::SetNextWindowPos(ImVec2(frameMin.x, frameMax.y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, animH), ImVec2(width, animH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

    if (ImGui::BeginPopup("##mdrop", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar)) {
        ImVec2 popMin = ImGui::GetWindowPos();
        ImVec2 popMax(popMin.x + width, popMin.y + animH);
        auto* popDl = ImGui::GetWindowDrawList();

        // Popup bg — solid WidgetBase fill (no top-down gradient) so the
        // dropdown reads as a flat surface distinct from the closed frame.
        popDl->AddRectFilled(popMin, popMax, frameBg);

        // Dropdown border: sides + rounded bottom (no top line)
        {
            auto* fgDl = ImGui::GetForegroundDrawList();
            fgDl->AddRect(popMin, popMax, frameBorder, rounding, ImDrawFlags_RoundCornersBottom, 1.0f);
            fgDl->AddLine(ImVec2(popMin.x + 1.0f, popMin.y + 0.5f),
                          ImVec2(popMax.x - 1.0f, popMin.y + 0.5f), frameBg, 1.0f);
        }

        ImGui::PushClipRect(popMin, popMax, true);

        // Custom-drawn items
        float curY = popupPadY;
        for (int i = 0; i < items_count; i++) {
            bool val = selected[i];
            ImVec2 iMin(popMin.x + S(2), popMin.y + curY);
            ImVec2 iMax(popMax.x - S(2), popMin.y + curY + itemH);

            // Hit test
            ImGui::SetCursorScreenPos(iMin);
            char itemId[32];
            snprintf(itemId, sizeof(itemId), "##mi%d", i);
            ImGui::InvisibleButton(itemId, ImVec2(iMax.x - iMin.x, itemH));
            bool itemHovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) {
                selected[i] = !selected[i];
                changed = true;
            }

            // Lerped hover highlight
            ImGuiID itemAnimId = ImGui::GetID(itemId) + 200;
            float ihAnim = Animate(itemAnimId, itemHovered ? 1.0f : 0.0f, 20.0f);
            if (ihAnim > 0.01f) {
                ImU32 hoverCol = MenuStyle::Colors::ShadowInk.alpha((ihAnim * 30) / 255.0f).u32();
                popDl->AddRectFilled(iMin, iMax, hoverCol);
            }

            // Checked rows: bold + accent-colored text (matches single-select
            // combo — the checked items read at a glance). Accent colour
            // only; no bold weight change so layout doesn't jitter.
            ImU32 textCol = val ? MenuStyle::Colors::Accent.u32() : MenuStyle::Colors::TextBright.alpha(220 / 255.0f).u32();
            ImVec2 tp(iMin.x + S(8.0f), iMin.y + (itemH - lineH) * 0.5f);
            popDl->AddText(tp, textCol, items[i]);

            curY += itemH;
        }

        ImGui::PopClipRect();
        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    // Right-click keybind
    if (rightClicked && elementId) {
        std::vector<std::string> comboItems;
        comboItems.reserve(items_count);
        for (int i = 0; i < items_count; i++)
            comboItems.emplace_back(items[i] ? items[i] : "");
        KeybindSystem::OpenContextMenu(elementId, label, KeybindSystem::ElementType::MultiCombo,
                                       0, 1, 0, 100, 0, comboItems);
    }

    ImGui::PopID();
    return changed;
}

// ─── Button ─────────────────────────────────────────────────────────────

bool Button(const char* label, float width) {
    ImGui::PushID(label);
    NonCheckboxIndentScope _indent;
    if (width <= 0.0f) width = GetWidgetWidth() - 2.0f * NonCheckboxIndent();

    float lineH = ImGui::GetTextLineHeight();
    float frameH = lineH + S(6.0f);
    float rounding = 0.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 btnMin = pos;
    ImVec2 btnMax(pos.x + width, pos.y + frameH);

    ImGuiID btnId = ImGui::GetID("##btn");
    ImGui::InvisibleButton("##btn", ImVec2(width, frameH));
    bool pressed = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();

    // Lerped hover + press
    float hoverAnim = Animate(btnId + 100, hovered ? 1.0f : 0.0f, 25.0f);
    float pressAnim = Animate(btnId + 101, held ? 1.0f : 0.0f, 24.0f);

    // Rest → MenuDark, hover → MenuLight (small lift), press → MenuDark
    // darkened a touch (subtle sink). The top-down gradient sits on top.
    ImU32 baseBg = MenuStyle::Colors::WidgetBase.u32();
    ImU32 bg = LerpColor(baseBg, MenuStyle::Colors::MenuLight.u32(), hoverAnim);
    // Press flash — pull toward pure black for a very subtle darken.
    bg = LerpColor(bg, MenuStyle::Colors::Ink.u32(), pressAnim * 0.4f);

    // Explicit MenuDark bottom endpoint so the button fades to the panel
    // tone consistently with the other widgets.
    DrawWidgetGradient(dl, btnMin, btnMax, bg);
    // Black silhouette wraps a Border-colored inner rim — the crisp
    // outer edge reads first, the inner tone softens the seam against
    // the button fill.
    dl->AddRect(ImVec2(btnMin.x - 1.0f, btnMin.y - 1.0f),
                ImVec2(btnMax.x + 1.0f, btnMax.y + 1.0f),
                MenuStyle::Colors::WidgetOutline.u32(), 0.0f, 0, 1.0f);
    dl->AddRect(btnMin, btnMax, MenuStyle::Colors::Border.u32(), 0.0f, 0, 1.0f);

    // Centered label
    ImVec2 textSize = ImGui::CalcTextSize(label);
    ImVec2 textPos(btnMin.x + (width - textSize.x) * 0.5f,
                   btnMin.y + (frameH - textSize.y) * 0.5f);
    dl->AddText(textPos, MenuStyle::Colors::TextBright.u32(), label);

    ImGui::PopID();
    return pressed;
}

// ─── InputText ──────────────────────────────────────────────────────────

// Internal — actual render path. Shared between the labelled InputText and
// the no-label InputTextPlaceholder. When `label` is non-null, a muted label
// row is drawn above the frame. When `placeholder` is non-null, ImGui's
// `InputTextWithHint` is used so the placeholder text appears inside the
// frame while the buffer is empty.
static bool InputTextImpl(const char* id, const char* label,
                          const char* placeholder,
                          char* buf, size_t buf_size,
                          bool noTopBorder = false,
                          bool noBottomBorder = false) {
    ImGui::PushID(id);

    NonCheckboxIndentScope _indent;
    float width = GetWidgetWidth() - 2.0f * NonCheckboxIndent();
    ImVec2 base = ImGui::GetCursorScreenPos();
    float lineH = ImGui::GetTextLineHeight();
    float rounding = 0.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Label row — only rendered when a label is supplied. Callers that
    // want a labelless input pass nullptr / "" and the frame floats up
    // to the cursor's current y, saving a line of vertical space.
    if (label && label[0]) {
        MenuStyle::TextShadow(dl, base, MenuStyle::Colors::Text.u32(), label);
        ImGui::SetCursorScreenPos(base);
        ImGui::Dummy(ImVec2(width, lineH));
    }

    // Frame — matches Button height (lineH + S(6.0f))
    float frameH = lineH + S(6.0f);
    ImVec2 frameMin = ImGui::GetCursorScreenPos();
    ImVec2 frameMax(frameMin.x + width, frameMin.y + frameH);

    // Push transparent ImGui frame chrome so its own bg + border don't
    // draw on top of our custom gradient + black outline (same widget
    // language as Combo / Button / Slider).
    ImGui::SetNextItemWidth(width);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8.0f), S(3.0f)));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,         ImVec4(0, 0, 0, 0));

    // Top-down gradient over MenuDark, drawn BEFORE the InputText runs so
    // the caret / text render on top.
    DrawWidgetGradient(dl, frameMin, frameMax, MenuStyle::Colors::WidgetBase.u32());

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.9f));
    bool changed = (placeholder && placeholder[0])
        ? ImGui::InputTextWithHint("##text", placeholder, buf, buf_size)
        : ImGui::InputText("##text", buf, buf_size);
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(2);

    // Uniform black outline. When noTop/noBottom is set, extend the rect
    // past the suppressed edge and clip so that edge simply isn't drawn —
    // same trick the ListBox uses to seamlessly join with its input.
    if (noTopBorder || noBottomBorder) {
        ImVec2 extMin = frameMin;
        ImVec2 extMax = frameMax;
        if (noTopBorder)    extMin.y -= S(8.0f);
        if (noBottomBorder) extMax.y += S(8.0f);
        dl->PushClipRect(frameMin, frameMax, true);
        dl->AddRect(extMin, extMax, MenuStyle::Colors::WidgetOutline.u32(), 0.0f, 0, 1.0f);
        dl->PopClipRect();
    } else {
        dl->AddRect(frameMin, frameMax, MenuStyle::Colors::WidgetOutline.u32(), 0.0f, 0, 1.0f);
    }

    ImGui::PopID();
    return changed;
}

bool InputText(const char* label, char* buf, size_t buf_size) {
    // Original labelled path — id derived from the label as before.
    return InputTextImpl(label, label, /*placeholder*/ nullptr, buf, buf_size);
}

bool InputTextPlaceholder(const char* id, const char* placeholder,
                          char* buf, size_t buf_size,
                          bool noTopBorder,
                          bool noBottomBorder) {
    return InputTextImpl(id ? id : "##itph",
                         /*label*/ nullptr,
                         placeholder, buf, buf_size,
                         noTopBorder, noBottomBorder);
}

// ─── ListBox ────────────────────────────────────────────────────────────

bool ListBox(const char* label, int* current_item, const char* const items[],
             int items_count, int height_in_items, int boldItemIndex,
             bool noBottomBorder,
             const ListBoxItemCogProvider& itemCogProvider,
             const char* scopeKey) {
    ImGui::PushID(label);

    // Per-item cog popup keys need a scope distinct from the label — the
    // caller can pass one via `scopeKey`; otherwise reuse the label.
    const char* cogScope = (scopeKey && scopeKey[0]) ? scopeKey
                        : (label   && label[0])    ? label
                        :                            "##lb";

    NonCheckboxIndentScope _indent;
    float width = GetWidgetWidth() - 2.0f * NonCheckboxIndent();
    ImVec2 base = ImGui::GetCursorScreenPos();
    float lineH = ImGui::GetTextLineHeight();
    float rounding = 0.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Label row — skip entirely when no label is supplied so the
    // SearchableListBox composite (which renders the label above its
    // input + listbox pair) doesn't get a leftover blank row from us.
    if (label && label[0]) {
        MenuStyle::TextShadow(dl, base, MenuStyle::Colors::Text.u32(), label);
        ImGui::SetCursorScreenPos(base);
        ImGui::Dummy(ImVec2(width, lineH));
    }

    // Frame dimensions
    float itemH = lineH + S(6.0f);
    float padY = S(4.0f);
    int visibleItems = height_in_items > 0 ? height_in_items : (std::min)(items_count, 5);
    float frameH = visibleItems * itemH + padY * 2.0f;
    ImVec2 frameMin = ImGui::GetCursorScreenPos();
    ImVec2 frameMax(frameMin.x + width, frameMin.y + frameH);

    // Background + border. When noBottomBorder is set, draw the bg with
    // square bottom corners, then draw the border with frameMax extended
    // downward and clip the drawing to the original frame. The clip cuts
    // the extended bottom edge off entirely — it literally is not drawn
    // in the visible area, so there's no AA half-pixel leftover to erase.
    // Left+right edges go all the way down to the extended bottom but get
    // cut at the clip's bottom edge, so they remain visible end-to-end.
    // Solid MenuDark fill + uniform black outline. noBottomBorder still
    // uses the extend-and-clip trick to suppress the bottom edge for
    // stacked layouts.
    dl->AddRectFilled(frameMin, frameMax, MenuStyle::Colors::WidgetBase.u32());
    if (noBottomBorder) {
        ImVec2 extMax(frameMax.x, frameMax.y + S(8.0f));
        dl->PushClipRect(frameMin, frameMax, true);
        dl->AddRect(frameMin, extMax, MenuStyle::Colors::WidgetOutline.u32(), 0.0f, 0, 1.0f);
        dl->PopClipRect();
    } else {
        dl->AddRect(frameMin, frameMax, MenuStyle::Colors::WidgetOutline.u32(), 0.0f, 0, 1.0f);
    }

    // Clip items to frame
    dl->PushClipRect(frameMin, frameMax, true);

    // Scrolling state — s_listScroll lives at file scope (see hoist note above).
    ImGuiID scrollId = ImGui::GetID("##lbscroll");
    float& scrollY = s_listScroll[scrollId];
    float totalContentH = items_count * itemH + padY * 2.0f;
    float maxScroll = (std::max)(totalContentH - frameH, 0.0f);

    // Handle scroll input
    ImGui::SetCursorScreenPos(frameMin);
    ImGui::InvisibleButton("##lbarea", ImVec2(width, frameH));
    if (ImGui::IsItemHovered()) {
        scrollY -= ImGui::GetIO().MouseWheel * itemH * 2.0f;
        scrollY = std::clamp(scrollY, 0.0f, maxScroll);
    }

    // Draw items
    bool changed = false;
    float curY = padY - scrollY;
    for (int i = 0; i < items_count; i++) {
        float iy = frameMin.y + curY;
        if (iy + itemH >= frameMin.y && iy <= frameMax.y) {
            bool sel = (*current_item == i);
            ImVec2 iMin(frameMin.x + S(2), iy);
            ImVec2 iMax(frameMax.x - S(2), iy + itemH);

            // Hit test
            bool itemHovered = ImGui::IsItemHovered() &&
                ImGui::GetIO().MousePos.x >= iMin.x && ImGui::GetIO().MousePos.x <= iMax.x &&
                ImGui::GetIO().MousePos.y >= iMin.y && ImGui::GetIO().MousePos.y <= iMax.y;

            // Skip the row-select click when the mouse is over a per-item
            // cog (its own rect is registered from last frame, so this test
            // works before we submit this frame's cog).
            if (itemHovered && ImGui::IsMouseClicked(0) && !IsMouseOverSettingsCog()) {
                *current_item = i;
                changed = true;
            }

            // Lerped hover
            ImGuiID itemAnimId = ImGui::GetID(label) + i + 500;
            float ihAnim = Animate(itemAnimId, itemHovered ? 1.0f : 0.0f);
            if (ihAnim > 0.01f) {
                ImU32 hoverCol = MenuStyle::Colors::ShadowInk.alpha((ihAnim * 30) / 255.0f).u32();
                dl->AddRectFilled(iMin, iMax, hoverCol);
            }

            // Selected row: bold + accent-colored text (matches combo popup).
            // boldItemIndex still forces bold for keyboard-nav position hints
            // even when that row isn't the selected one.
            ImU32 textCol = sel ? MenuStyle::Colors::Accent.u32() : MenuStyle::Colors::TextBright.alpha(220 / 255.0f).u32();
            bool useBold = (sel || i == boldItemIndex) && MenuStyle::g_fontBold;
            ImVec2 tp(iMin.x + S(8.0f), iMin.y + (itemH - lineH) * 0.5f);
            ImFont* nameFont = useBold ? MenuStyle::g_fontBold : ImGui::GetFont();
            float nameFontSize = ImGui::GetFontSize();
            if (useBold) {
                dl->AddText(MenuStyle::g_fontBold, nameFontSize,
                            tp, textCol, items[i]);
            } else {
                dl->AddText(tp, textCol, items[i]);
            }

            // Per-item cog — sits just past the item name, vertically
            // centered on the row. Skipped when the caller didn't supply
            // an itemCogProvider or the provider returned an empty fn.
            if (itemCogProvider) {
                std::function<void()> itemContent = itemCogProvider(i);
                if (itemContent) {
                    ImVec2 nameSz = nameFont->CalcTextSizeA(nameFontSize, FLT_MAX, 0.0f, items[i]);
                    float cogSz = S(10.0f);
                    ImVec2 cogPos(tp.x + nameSz.x + S(6.0f),
                                  iMin.y + (itemH - cogSz) * 0.5f);
                    // Cap the cog's X so it never spills past the item row's
                    // right edge (long item names).
                    float maxCogX = iMax.x - cogSz - S(4.0f);
                    if (cogPos.x > maxCogX) cogPos.x = maxCogX;
                    char cogKey[96];
                    snprintf(cogKey, sizeof(cogKey), "%s:item:%d", cogScope, i);
                    UI::SettingsCog(cogKey, items[i], cogPos, cogSz,
                                    itemContent, itemHovered);
                }
            }
        }
        curY += itemH;
    }

    dl->PopClipRect();

    // Advance cursor past the listbox
    ImGui::SetCursorScreenPos(ImVec2(frameMin.x, frameMax.y));
    ImGui::Dummy(ImVec2(width, 0));

    ImGui::PopID();
    return changed;
}

// ─── ColorPicker ────────────────────────────────────────────────────────

// ─── Custom Color Picker Components ─────────────────────────────────────

// Preserved hue per picker (grays lose hue in RGB→HSV, so we store it)
static std::unordered_map<ImGuiID, float> s_pickerHue;
static char s_hexBuf[12] = "";

// Forward decl — defined further down with the picker code
static bool ParseHexColor(const char* s, float out[4], bool* hadAlpha);

// ─── Themed color copy/paste popup ──────────────────────────────────────
// Fully custom-drawn popup that matches the keybind context-menu style: fade-in, channel
// split for bg/content, title bar + accent separator, hover-animated menu rows.
//
// Uses a manual ImGui::Begin (NOT BeginPopup) with a per-id open/position state map,
// because BeginPopup forces ImGuiWindowFlags_AlwaysAutoResize which conflicts with our
// drawlist-only rendering and caused flicker on taller (multi-color) popups.

static std::unordered_map<int, float> s_colorCtxHoverAnim;
static std::unordered_map<ImGuiID, float> s_colorCtxFade;

struct ColorCtxState {
    bool   open = false;
    ImVec2 pos  = ImVec2(0, 0);
    int    openFrame = -1;  // frame on which the popup was opened (used to skip the opening click)
};
// Keyed by ImGuiID derived from the caller's current PushID scope, so multiple pickers
// on the same page don't collide.
static std::unordered_map<ImGuiID, ColorCtxState> s_colorCtxState;

// Public helpers so call sites can open/query the popup without touching BeginPopup.
// The uid is derived by the caller via ImGui::GetID(popupId) inside the picker's PushID
// scope so every picker gets a unique ID.
static void OpenColorCopyPastePopup(ImGuiID uid, ImVec2 anchorBL) {
    auto& st = s_colorCtxState[uid];
    st.open = true;
    st.pos  = anchorBL;
    st.openFrame = ImGui::GetFrameCount();
    s_colorCtxFade[uid] = 0.0f; // fresh fade-in
}

static ImU32 FadeColorU32(ImU32 col, float fade) {
    int a = (int)(((col >> 24) & 0xFF) * fade);
    return (col & 0x00FFFFFF) | ((ImU32)a << 24);
}

static bool RenderColorCopyPastePopup(ImGuiID uid,
                                      float cols[][4],
                                      const char* const names[],
                                      int count) {
    auto& st = s_colorCtxState[uid];
    if (!st.open) return false;

    // Layout constants — Panel tokens keep every floating window aligned.
    const float padSide     = S(10.0f);
    const float titleBarH   = S(MenuStyle::Panel::TitleBarH);
    const float bgRound     = S(MenuStyle::Panel::Rounding);
    const float itemH       = ImGui::GetTextLineHeight() + S(10.0f);
    const float sectionGap  = S(6.0f);
    const float headerH     = ImGui::GetTextLineHeight() + S(8.0f);
    const float popupW      = S(200.0f);

    // Single-color: no per-slot header row. Multi-color: keep the name
    // header so slots are distinguishable.
    float headerPerSlot = (count > 1) ? headerH : 0.0f;
    float popupH = titleBarH + S(6.0f) + 1.0f
                 + count * (headerPerSlot + itemH * 2.0f)
                 + (count - 1) * sectionGap
                 + S(6.0f) + 1.0f;

    // Keep the popup on-screen
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 pos = st.pos;
    if (pos.x + popupW > io.DisplaySize.x) pos.x = io.DisplaySize.x - popupW - S(4.0f);
    if (pos.y + popupH > io.DisplaySize.y) pos.y = io.DisplaySize.y - popupH - S(4.0f);
    if (pos.x < S(4.0f)) pos.x = S(4.0f);
    if (pos.y < S(4.0f)) pos.y = S(4.0f);

    // Transparent shell, fixed-size Begin window
    ImGui::PushStyleColor(ImGuiCol_WindowBg,  ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,    ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2(0, 0));

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(popupW, popupH), ImGuiCond_Always);
    ImGui::SetNextWindowFocus();

    // Unique window name per uid so multiple pickers on the same page don't collide
    char winName[64];
    snprintf(winName, sizeof(winName), "##colorctx_%u", (unsigned)uid);

    bool changed = false;
    bool wantClose = false;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav;
    // We pass p_open=nullptr; close is handled manually via click-outside detection below.
    if (ImGui::Begin(winName, nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winMax(winPos.x + popupW, winPos.y + popupH);

        // Fade-in animation
        float& fade = s_colorCtxFade[uid];
        float dt = ImGui::GetIO().DeltaTime;
        float fadeStep = std::min(20.0f * dt, 1.0f);
        fade += (1.0f - fade) * fadeStep;
        if (fade > 0.995f) fade = 1.0f;

        // Channel split: 0 = bg, 1 = content
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);

        ImFont* boldFont = MenuStyle::g_fontBold ? MenuStyle::g_fontBold : ImGui::GetFont();
        float fontSize = ImGui::GetFontSize();
        float lineH = ImGui::GetTextLineHeight();

        // Title bar — "copy color". Bold + TextMuted with a 1 px shadow,
        // same treatment container titles use.
        const char* title = (count > 1) ? "copy colors" : "copy color";
        {
            float titleY = winPos.y + (titleBarH - fontSize) * 0.5f;
            dl->AddText(boldFont, fontSize, ImVec2(winPos.x + padSide + 1, titleY + 1),
                        MenuStyle::Colors::ShadowInk.alpha((90 * fade) / 255.0f).u32(), title);
            dl->AddText(boldFont, fontSize, ImVec2(winPos.x + padSide, titleY),
                        FadeColorU32(MenuStyle::Colors::TextMuted.u32(), fade), title);
        }

        float sepY = winPos.y + titleBarH;
        float curY = sepY + S(6.0f) + 1.0f;

        // Pre-parse clipboard for paste validation
        const char* clip = ImGui::GetClipboardText();
        float pasted[4] = { 0, 0, 0, 0 };
        bool pastedHasAlpha = false;
        bool clipValid = clip && ParseHexColor(clip, pasted, &pastedHasAlpha);

        // Draw one menu row with hover lerp animation. `swatchCol4` (if
        // non-null) draws a color swatch on the left in place of an FA
        // icon — used to show the exact color that would be copied
        // (current) or pasted (clipboard) directly on the button.
        auto DrawMenuRow = [&](int idx, const char* label, bool enabled,
                               const float* swatchCol4) -> bool {
            ImVec2 iMin(winPos.x + S(6.0f), curY);
            ImVec2 iMax(winPos.x + popupW - S(6.0f), curY + itemH);

            ImGui::SetCursorScreenPos(iMin);
            char id[32]; snprintf(id, sizeof(id), "##ccm%d", idx);
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0, 0, 0, 0));
            ImGui::InvisibleButton(id, ImVec2(iMax.x - iMin.x, itemH));
            ImGui::PopStyleColor(3);
            bool hovered = ImGui::IsItemHovered() && enabled;
            bool clicked = ImGui::IsItemClicked() && enabled;

            float& anim = s_colorCtxHoverAnim[idx];
            float target = hovered ? 1.0f : 0.0f;
            float step = std::min(20.0f * dt, 1.0f);
            anim += (target - anim) * step;
            if (std::abs(target - anim) < 0.005f) anim = target;

            // Hover sheen — Border color faded top → bottom at ~20% alpha,
            // same treatment the container title bar and sidebar subtab
            // sheen use so every hover in the menu reads as one system.
            if (anim > 0.01f) {
                ImU32 topSh = MenuStyle::Colors::Border.alpha(0.20f * anim * fade).u32();
                ImU32 botSh = MenuStyle::Colors::Border.alpha(0.00f).u32();
                dl->AddRectFilledMultiColor(iMin, iMax, topSh, topSh, botSh, botSh);
            }

            ImU32 mutedCol = MenuStyle::Colors::TextMuted.u32();
            ImU32 textCol  = enabled
                ? LerpColor(mutedCol, MenuStyle::Colors::Text.u32(), anim)
                : MenuStyle::Colors::IconDisabled.u32();
            textCol = FadeColorU32(textCol, fade);
            float textY = curY + (itemH - lineH) * 0.5f;
            float textX = winPos.x + padSide;

            // Swatch — hard rectangle with WidgetOutline (black) frame,
            // matching every other swatch in the menu. Reserves the swatch
            // slot even when swatchCol4 is null (disabled paste with no
            // clipboard) so the label X stays aligned across rows.
            const float swW = S(20.0f);
            const float swH = S(11.0f);
            float swX = std::floor(textX);
            float swY = std::floor(curY + (itemH - swH) * 0.5f);
            ImVec2 swMin(swX, swY);
            ImVec2 swMax(swX + swW, swY + swH);
            if (swatchCol4) {
                ImU32 swCol = ImGui::GetColorU32(ImVec4(
                    swatchCol4[0], swatchCol4[1], swatchCol4[2], swatchCol4[3]));
                dl->AddRectFilled(swMin, swMax, FadeColorU32(swCol, fade));
                dl->AddRect(swMin, swMax,
                            FadeColorU32(MenuStyle::Colors::WidgetOutline.u32(), fade),
                            0.0f, 0, 1.0f);
            }
            textX = swMax.x + S(8.0f);

            MenuStyle::TextShadow(dl, ImVec2(textX, textY), textCol, label);

            curY += itemH;
            return clicked;
        };

        int rowIdx = 0;
        for (int i = 0; i < count; i++) {
            if (i > 0) curY += sectionGap;

            // Multi-color pickers keep a small name-only header so users
            // can distinguish which slot each copy/paste pair edits. Single
            // color skips the header entirely — the swatch on the copy row
            // is enough context.
            if (count > 1) {
                const char* nm = (names && names[i]) ? names[i] : "color";
                float textY = curY + (headerH - fontSize) * 0.5f;
                dl->AddText(boldFont, fontSize,
                            ImVec2(winPos.x + padSide, textY),
                            FadeColorU32(MenuStyle::Colors::TextMuted.u32(), fade),
                            nm);
                curY += headerH;
            }

            // copy row — swatch shows the CURRENT color (what would be copied)
            if (DrawMenuRow(rowIdx++, "copy", true, cols[i])) {
                char hex[12];
                snprintf(hex, sizeof(hex), "#%02X%02X%02X%02X",
                         (int)(cols[i][0] * 255), (int)(cols[i][1] * 255),
                         (int)(cols[i][2] * 255), (int)(cols[i][3] * 255));
                ImGui::SetClipboardText(hex);
                if (count == 1) wantClose = true; // keep open for multi-color pickers
            }
            // paste row — swatch shows the CLIPBOARD color when valid (what
            // would be pasted), or nothing when the clipboard doesn't parse.
            if (DrawMenuRow(rowIdx++, "paste", clipValid,
                            clipValid ? pasted : nullptr)) {
                cols[i][0] = pasted[0];
                cols[i][1] = pasted[1];
                cols[i][2] = pasted[2];
                if (pastedHasAlpha) cols[i][3] = pasted[3];
                changed = true;
                if (count == 1) wantClose = true; // keep open for multi-color pickers
            }
        }

        // Background on channel 0 — matches the container chrome exactly:
        //   • PanelBg body
        //   • Border@20% → 0 overlay across the title bar (soft strip)
        //   • Border outline
        //   • Outer black Ink outline wrapping the border
        dl->ChannelsSetCurrent(0);
        dl->AddRectFilled(winPos, winMax,
                          FadeColorU32(MenuStyle::Colors::PanelBg.u32(), fade));
        {
            ImU32 topCol = MenuStyle::Colors::Border.alpha(0.20f * fade).u32();
            ImU32 botCol = MenuStyle::Colors::Border.alpha(0.00f).u32();
            dl->AddRectFilledMultiColor(
                winPos, ImVec2(winMax.x, winPos.y + titleBarH),
                topCol, topCol, botCol, botCol);
        }
        dl->AddRect(winPos, winMax,
                    FadeColorU32(MenuStyle::Colors::Border.u32(), fade),
                    0.0f, 0, 1.0f);
        dl->AddRect(ImVec2(winPos.x - 1.0f, winPos.y - 1.0f),
                    ImVec2(winMax.x + 1.0f, winMax.y + 1.0f),
                    FadeColorU32(MenuStyle::Colors::Ink.u32(), fade),
                    0.0f, 0, 1.0f);
        dl->ChannelsMerge();

        // Click-outside detection — skip the frame the popup opened on, otherwise the
        // right-click that opened it would be detected as "outside" and close it instantly.
        if (ImGui::GetFrameCount() > st.openFrame) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                ImVec2 mp = ImGui::GetIO().MousePos;
                bool inside = mp.x >= winPos.x && mp.x <= winMax.x &&
                              mp.y >= winPos.y && mp.y <= winMax.y;
                if (!inside) wantClose = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) wantClose = true;
        }

        ImGui::End();
    } else {
        ImGui::End();
    }

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);

    if (wantClose) {
        st.open = false;
        s_colorCtxFade[uid] = 0.0f;
    }

    return changed;
}

// Parse a hex color string into rgba floats. Accepts "RRGGBB", "#RRGGBB",
// "RRGGBBAA", "#RRGGBBAA". Returns true on success and writes out[4] in [0,1].
// `hadAlpha` is set to true when an 8-char form was provided.
static bool ParseHexColor(const char* s, float out[4], bool* hadAlpha = nullptr) {
    if (!s) return false;
    // Skip leading whitespace
    while (*s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) s++;
    if (*s == '#') s++;
    int len = 0;
    while (s[len] && len < 9) {
        char c = s[len];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) break;
        len++;
    }
    unsigned int rv = 0, gv = 0, bv = 0, av = 255;
    if (len == 6 && sscanf(s, "%02x%02x%02x", &rv, &gv, &bv) == 3) {
        if (hadAlpha) *hadAlpha = false;
    } else if (len == 8 && sscanf(s, "%02x%02x%02x%02x", &rv, &gv, &bv, &av) == 4) {
        if (hadAlpha) *hadAlpha = true;
    } else {
        return false;
    }
    out[0] = rv / 255.0f;
    out[1] = gv / 255.0f;
    out[2] = bv / 255.0f;
    out[3] = av / 255.0f;
    return true;
}

// SV square: saturation left→right, value top→bottom
static bool DrawSVSquare(ImDrawList* dl, ImVec2 pos, float w, float h,
                         float hue, float& sat, float& val) {
    ImVec2 max(pos.x + w, pos.y + h);

    // Hue color at full saturation+value
    float hr, hg, hb;
    ImGui::ColorConvertHSVtoRGB(hue, 1.0f, 1.0f, hr, hg, hb);
    ImU32 hueCol = IM_COL32((int)(hr * 255), (int)(hg * 255), (int)(hb * 255), 255);

    // White → hue horizontally
    dl->AddRectFilledMultiColor(pos, max,
        MenuStyle::Colors::White.u32(), hueCol, hueCol, MenuStyle::Colors::White.u32());
    // Transparent → black vertically
    ImU32 inkClear = MenuStyle::Colors::Ink.alpha(0.0f).u32();
    ImU32 inkFull  = MenuStyle::Colors::Ink.u32();
    dl->AddRectFilledMultiColor(pos, max,
        inkClear, inkClear, inkFull, inkFull);

    dl->AddRect(pos, max, MenuStyle::Colors::Border.u32(), 0.0f, 0, 1.0f);

    // Cursor
    float cx = pos.x + sat * w;
    float cy = pos.y + (1.0f - val) * h;
    dl->AddCircle(ImVec2(cx, cy), S(5.0f), MenuStyle::Colors::White.u32(), 24, S(2.0f));
    dl->AddCircle(ImVec2(cx, cy), S(4.0f), MenuStyle::Colors::Ink.alpha(140 / 255.0f).u32(), 24, 1.0f);

    // Interaction
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("##sv", ImVec2(w, h));
    bool changed = false;
    if (ImGui::IsItemActive()) {
        ImVec2 m = ImGui::GetIO().MousePos;
        sat = std::clamp((m.x - pos.x) / w, 0.0f, 1.0f);
        val = 1.0f - std::clamp((m.y - pos.y) / h, 0.0f, 1.0f);
        changed = true;
    }
    return changed;
}

// Horizontal hue bar
static bool DrawHueBar(ImDrawList* dl, ImVec2 pos, float w, float barH, float& hue) {
    ImVec2 max(pos.x + w, pos.y + barH);

    // 6 rainbow segments
    float segW = w / 6.0f;
    ImU32 colors[7];
    for (int i = 0; i < 7; i++) {
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB(i / 6.0f, 1.0f, 1.0f, r, g, b);
        colors[i] = IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 255);
    }
    for (int i = 0; i < 6; i++) {
        ImVec2 smin(pos.x + i * segW, pos.y);
        ImVec2 smax(pos.x + (i + 1) * segW, pos.y + barH);
        dl->AddRectFilledMultiColor(smin, smax, colors[i], colors[i + 1], colors[i + 1], colors[i]);
    }
    dl->AddRect(pos, max, MenuStyle::Colors::Border.u32(), 0.0f, 0, 1.0f);

    // Cursor — small vertical pill
    float cx = pos.x + hue * w;
    float hcw = S(2.5f);
    dl->AddRectFilled(ImVec2(cx - hcw, pos.y - 1), ImVec2(cx + hcw, pos.y + barH + 1),
                      MenuStyle::Colors::White.u32(), 1.0f);
    dl->AddRect(ImVec2(cx - hcw, pos.y - 1), ImVec2(cx + hcw, pos.y + barH + 1),
                MenuStyle::Colors::Ink.alpha(140 / 255.0f).u32(), 1.0f, 0, 1.0f);

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("##hue", ImVec2(w, barH));
    bool changed = false;
    if (ImGui::IsItemActive()) {
        hue = std::clamp((ImGui::GetIO().MousePos.x - pos.x) / w, 0.0f, 0.9999f);
        changed = true;
    }
    return changed;
}

// Horizontal alpha bar with checkerboard
static bool DrawAlphaBar(ImDrawList* dl, ImVec2 pos, float w, float barH,
                         float r, float g, float b, float& alpha) {
    ImVec2 max(pos.x + w, pos.y + barH);

    // Checkerboard
    float cs = barH * 0.5f;
    dl->PushClipRect(pos, max, true);
    for (float x = 0; x < w; x += cs) {
        for (int row = 0; row < 2; row++) {
            int xi = (int)(x / cs);
            ImU32 c = ((xi + row) % 2 == 0) ? MenuStyle::Colors::CheckerLight.u32() : MenuStyle::Colors::CheckerDark.u32();
            ImVec2 cmin(pos.x + x, pos.y + row * cs);
            ImVec2 cmax(std::min(pos.x + x + cs, max.x), std::min(pos.y + (row + 1) * cs, max.y));
            dl->AddRectFilled(cmin, cmax, c);
        }
    }
    dl->PopClipRect();

    // Color gradient: transparent → opaque
    ImU32 c0 = IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 0);
    ImU32 c1 = IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 255);
    dl->AddRectFilledMultiColor(pos, max, c0, c1, c1, c0);
    dl->AddRect(pos, max, MenuStyle::Colors::Border.u32(), 0.0f, 0, 1.0f);

    // Cursor
    float cx = pos.x + alpha * w;
    float acw = S(2.5f);
    dl->AddRectFilled(ImVec2(cx - acw, pos.y - 1), ImVec2(cx + acw, pos.y + barH + 1),
                      MenuStyle::Colors::White.u32(), 1.0f);
    dl->AddRect(ImVec2(cx - acw, pos.y - 1), ImVec2(cx + acw, pos.y + barH + 1),
                MenuStyle::Colors::Ink.alpha(140 / 255.0f).u32(), 1.0f, 0, 1.0f);

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("##alpha", ImVec2(w, barH));
    bool changed = false;
    if (ImGui::IsItemActive()) {
        alpha = std::clamp((ImGui::GetIO().MousePos.x - pos.x) / w, 0.0f, 1.0f);
        changed = true;
    }
    return changed;
}

// Full custom picker: SV square + hue bar + alpha bar + hex input
// Returns true if color changed
static bool DrawCustomPicker(ImGuiID pickerId, float col[4], float width) {
    bool changed = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Get/store hue
    float h, s, v;
    ImGui::ColorConvertRGBtoHSV(col[0], col[1], col[2], h, s, v);
    auto hueIt = s_pickerHue.find(pickerId);
    if (hueIt == s_pickerHue.end()) {
        s_pickerHue[pickerId] = h;
    } else if (s > 0.01f && v > 0.01f) {
        s_pickerHue[pickerId] = h; // update stored hue from RGB if color isn't gray
    }
    h = s_pickerHue[pickerId];

    const float gap = S(6.0f);
    const float barH = S(12.0f);
    float svH = width * 0.75f; // slightly shorter than square for compact layout
    ImVec2 pos = ImGui::GetCursorScreenPos();

    // SV square
    bool svChanged = DrawSVSquare(dl, pos, width, svH, h, s, v);
    pos.y += svH + gap;

    // Hue bar
    ImGui::SetCursorScreenPos(pos);
    bool hueChanged = DrawHueBar(dl, pos, width, barH, h);
    if (hueChanged) s_pickerHue[pickerId] = h;
    pos.y += barH + gap;

    // Alpha bar
    ImGui::SetCursorScreenPos(pos);
    float pr, pg, pb;
    ImGui::ColorConvertHSVtoRGB(h, s, v, pr, pg, pb);
    bool alphaChanged = DrawAlphaBar(dl, pos, width, barH, pr, pg, pb, col[3]);
    pos.y += barH + gap;

    // Convert HSV back to RGB if SV or hue changed
    if (svChanged || hueChanged) {
        ImGui::ColorConvertHSVtoRGB(h, s, v, col[0], col[1], col[2]);
        changed = true;
    }
    if (alphaChanged) changed = true;

    // Hex input
    ImGui::SetCursorScreenPos(pos);
    float lineH = ImGui::GetTextLineHeight();
    float hexH = lineH + S(8.0f);

    // Format current color as hex
    char currentHex[12];
    snprintf(currentHex, sizeof(currentHex), "#%02X%02X%02X%02X",
             (int)(col[0] * 255), (int)(col[1] * 255),
             (int)(col[2] * 255), (int)(col[3] * 255));

    // Track whether hex field is being edited
    static ImGuiID s_hexActivePickerId = 0;
    bool hexActive = (s_hexActivePickerId == pickerId);
    if (!hexActive) {
        strncpy(s_hexBuf, currentHex, sizeof(s_hexBuf));
    }

    // Styled hex input — reserves room on the right for a copy icon button.
    // Widget-standard visual language: DrawWidgetGradient over WidgetBase +
    // WidgetOutline (pure black) frame, hard rectangle. Same treatment used
    // by sliders, combos, and the value edit badges.
    const float copyBtnW = S(22.0f);
    ImVec2 hexMin = pos;
    ImVec2 hexMax(pos.x + width, pos.y + hexH);
    DrawWidgetGradient(dl, hexMin, hexMax,
                       MenuStyle::Colors::WidgetBase.u32());
    dl->AddRect(hexMin, hexMax,
                MenuStyle::Colors::WidgetOutline.u32(), 0.0f, 0, 1.0f);
    // Vertical separator between input field and copy icon
    dl->AddLine(ImVec2(hexMax.x - copyBtnW, hexMin.y + 1.0f),
                ImVec2(hexMax.x - copyBtnW, hexMax.y - 1.0f),
                MenuStyle::Colors::WidgetOutline.u32(), 1.0f);

    ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,         ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text,           MenuStyle::Colors::Text.v);
    ImVec4 selCol = MenuStyle::Colors::Accent.v; selCol.w = 0.45f;
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, selCol);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(6.0f), (hexH - lineH) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::SetNextItemWidth(width - copyBtnW);
    ImGui::InputText("##hex", s_hexBuf, sizeof(s_hexBuf),
            ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::IsItemActive()) s_hexActivePickerId = pickerId;
    else if (s_hexActivePickerId == pickerId) {
        s_hexActivePickerId = 0;
        // Parse hex on deactivate
        unsigned int rv = 0, gv = 0, bv = 0, av = 255;
        int len = (int)strlen(s_hexBuf);
        const char* hexStr = s_hexBuf;
        if (hexStr[0] == '#') { hexStr++; len--; }
        if (len == 6 && sscanf(hexStr, "%02x%02x%02x", &rv, &gv, &bv) == 3) {
            col[0] = rv / 255.0f; col[1] = gv / 255.0f; col[2] = bv / 255.0f;
            changed = true;
        } else if (len == 8 && sscanf(hexStr, "%02x%02x%02x%02x", &rv, &gv, &bv, &av) == 4) {
            col[0] = rv / 255.0f; col[1] = gv / 255.0f; col[2] = bv / 255.0f; col[3] = av / 255.0f;
            changed = true;
        }
        // Re-sync hue from new RGB
        if (changed) {
            float nh, ns, nv;
            ImGui::ColorConvertRGBtoHSV(col[0], col[1], col[2], nh, ns, nv);
            if (ns > 0.01f && nv > 0.01f) s_pickerHue[pickerId] = nh;
        }
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(6);

    // Copy icon button — copies the live hex to the clipboard. Hover
    // sheen matches the container title bar / sidebar sheen treatment:
    // Border color faded from top to bottom at ~20% alpha.
    {
        ImVec2 copyMin(hexMax.x - copyBtnW, hexMin.y);
        ImVec2 copyMax(hexMax.x, hexMax.y);
        ImGui::SetCursorScreenPos(copyMin);
        ImGui::InvisibleButton("##copyhex", ImVec2(copyBtnW, hexH));
        bool copyHovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) {
            ImGui::SetClipboardText(currentHex);
        }
        // Hover sheen — Border@25% top → transparent bottom, inset 1 px
        // so the input's frame outline stays crisp underneath.
        float hoverAnim = Animate(pickerId + 7100, copyHovered ? 1.0f : 0.0f);
        if (hoverAnim > 0.005f) {
            ImU32 topCol = MenuStyle::Colors::Border.alpha(0.25f * hoverAnim).u32();
            ImU32 botCol = MenuStyle::Colors::Border.alpha(0.00f).u32();
            dl->AddRectFilledMultiColor(
                ImVec2(copyMin.x + 1, copyMin.y + 1),
                ImVec2(copyMax.x - 1, copyMax.y - 1),
                topCol, topCol, botCol, botCol);
        }
        // Icon — TextMuted at rest, lifting to Text on hover (matches the
        // rest of the menu's icon-button convention).
        const char* copyIcon = "\xef\x83\x85"; // FA copy f0c5
        if (MenuStyle::g_fontIcon) {
            float iconSize = S(11.0f);
            ImVec2 isz = MenuStyle::g_fontIcon->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, copyIcon);
            float ix = copyMin.x + (copyBtnW - isz.x) * 0.5f;
            float iy = copyMin.y + (hexH - isz.y) * 0.5f;
            ImU32 iconCol = LerpColor(MenuStyle::Colors::TextMuted.u32(),
                                       MenuStyle::Colors::Text.u32(),
                                       hoverAnim);
            dl->AddText(MenuStyle::g_fontIcon, iconSize, ImVec2(ix, iy), iconCol, copyIcon);
        }
    }

    // ── Preset row ─────────────────────────────────────────────────────
    // 12 RGBA tiles + a trailing "+" tile to capture the current color.
    // Square tiles share the row's height; gap pulled from the same S(6.0f)
    // rhythm used by the SV/Hue/Alpha bars above. Row is always drawn even
    // when empty so the "+" tile is reachable on first open.
    {
        ImVec2 presetPos(pos.x, pos.y + hexH + S(8.0f));
        const float tileGap = S(4.0f);
        const float tileH   = S(16.0f);
        // Compute tile width so kColorPresetMax+1 tiles fill width exactly.
        const int slots = kColorPresetMax + 1;
        float tileW = (width - tileGap * (float)(slots - 1)) / (float)slots;
        if (tileW < S(10.0f)) tileW = S(10.0f);
        const float tileRound = S(2.0f);

        ImVec2 mouse = ImGui::GetIO().MousePos;

        // Existing presets
        for (int i = 0; i < (int)s_colorPresets.size() && i < kColorPresetMax; i++) {
            float tx = presetPos.x + (tileW + tileGap) * (float)i;
            ImVec2 tMin(tx, presetPos.y);
            ImVec2 tMax(tx + tileW, presetPos.y + tileH);
            uint32_t c = s_colorPresets[i];
            RoundRectFilled(dl, tMin, tMax, c, tileRound);

            bool hov = mouse.x >= tMin.x && mouse.x <= tMax.x &&
                       mouse.y >= tMin.y && mouse.y <= tMax.y;
            ImU32 outline = hov ? MenuStyle::Colors::Accent.u32()
                                : MenuStyle::Colors::Border.u32();
            RoundRectStroke(dl, tMin, tMax, outline, tileRound, hov ? 1.5f : 1.0f);

            if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                // Unpack ABGR (IM_COL32 layout) back to normalized floats.
                col[0] = ((c >>  0) & 0xFF) / 255.0f;
                col[1] = ((c >>  8) & 0xFF) / 255.0f;
                col[2] = ((c >> 16) & 0xFF) / 255.0f;
                col[3] = ((c >> 24) & 0xFF) / 255.0f;
                float nh, ns, nv;
                ImGui::ColorConvertRGBtoHSV(col[0], col[1], col[2], nh, ns, nv);
                if (ns > 0.01f && nv > 0.01f) s_pickerHue[pickerId] = nh;
                changed = true;
            }
            if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                s_colorPresets.erase(s_colorPresets.begin() + i);
                // Iteration is done after the click; bail out so we don't
                // index past the new size on a still-pending hov check.
                break;
            }
        }

        // "+" tile — always at slot index = current preset count (or capped).
        int plusIdx = std::min((int)s_colorPresets.size(), kColorPresetMax);
        // When at cap, plusIdx == kColorPresetMax → tile sits in the last
        // slot (overlapping the final preset visually). Skip drawing it in
        // that case; the user can right-click to delete and free a slot.
        if ((int)s_colorPresets.size() < kColorPresetMax) {
            float tx = presetPos.x + (tileW + tileGap) * (float)plusIdx;
            ImVec2 tMin(tx, presetPos.y);
            ImVec2 tMax(tx + tileW, presetPos.y + tileH);
            bool hov = mouse.x >= tMin.x && mouse.x <= tMax.x &&
                       mouse.y >= tMin.y && mouse.y <= tMax.y;
            ImU32 bg = hov ? MenuStyle::Colors::Border.u32() : MenuStyle::Colors::Border.u32();
            RoundRectFilled(dl, tMin, tMax, bg, tileRound);
            RoundRectStroke(dl, tMin, tMax,
                            hov ? MenuStyle::Colors::Accent.u32() : MenuStyle::Colors::Border.u32(),
                            tileRound, hov ? 1.5f : 1.0f);
            // "+" glyph (thin cross)
            float cx = (tMin.x + tMax.x) * 0.5f;
            float cy = (tMin.y + tMax.y) * 0.5f;
            float arm = tileH * 0.25f;
            ImU32 plusCol = hov ? MenuStyle::Colors::IconHover.u32() : MenuStyle::Colors::IconRest.u32();
            dl->AddLine(ImVec2(cx - arm, cy), ImVec2(cx + arm, cy), plusCol, 1.5f);
            dl->AddLine(ImVec2(cx, cy - arm), ImVec2(cx, cy + arm), plusCol, 1.5f);

            if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                uint32_t packed = IM_COL32(
                    (int)(col[0] * 255), (int)(col[1] * 255),
                    (int)(col[2] * 255), (int)(col[3] * 255));
                s_colorPresets.push_back(packed);
            }
        }

        // Advance cursor past the preset row (+ a small bottom gap).
        ImGui::SetCursorScreenPos(ImVec2(pos.x, presetPos.y + tileH));
        return changed;
    }
}

// ─── Shared popup drawing for color pickers ─────────────────────────────

// Draws the picker popup content (tabs for multi, picker, bg)
// popupId: the ImGui popup string ID
// col/cols: color data, names/count: tab info (count=1 for single)
// swatchMax: for positioning, pickerId: for animation/tab state
static bool DrawPickerPopup(const char* popupId, float cols[][4],
                            const char* const names[], int count,
                            ImVec2 swatchMax, ImGuiID pickerId) {
    const float popupW = S(260.0f);
    const float pad = S(8.0f);
    bool changed = false;

    // Check popup state for toggle + animation
    bool popupOpen = ImGui::IsPopupOpen(popupId);
    ImGuiID animId = pickerId + 3000;
    float openAnim = Animate(animId, popupOpen ? 1.0f : 0.0f, 12.0f);

    ImGui::SetNextWindowPos(ImVec2(swatchMax.x - popupW, swatchMax.y + S(4.0f)));
    ImGui::SetNextWindowSizeConstraints(ImVec2(popupW, 0), ImVec2(popupW, 800));
    float bgRoundPicker = S(MenuStyle::Panel::Rounding);
    ImVec4 panelBgVec = MenuStyle::Panel::Bg;      // implicit Color -> ImVec4
    ImVec4 borderVec  = MenuStyle::Panel::Border;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, bgRoundPicker);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, panelBgVec);
    ImGui::PushStyleColor(ImGuiCol_Border, borderVec);

    if (ImGui::BeginPopup(popupId, ImGuiWindowFlags_NoMove)) {
        // Apply open animation as alpha
        float alpha = std::clamp(openAnim, 0.0f, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

        ImVec2 popPos = ImGui::GetWindowPos();
        auto* popDl = ImGui::GetWindowDrawList();
        ImVec2 mousePos = ImGui::GetIO().MousePos;

        float tabRowH = (count > 1) ? S(30.0f) : 0.0f;
        float sepH = (count > 1) ? 1.0f : 0.0f;

        // Split channels: 0 = bg, 1 = content
        popDl->ChannelsSplit(2);
        popDl->ChannelsSetCurrent(1);

        float cursorY = 0.0f;

        // Ensure active tab is valid
        auto tabIt = s_colorPickerTab.find(pickerId);
        if (tabIt == s_colorPickerTab.end()) {
            s_colorPickerTab[pickerId] = 0;
            tabIt = s_colorPickerTab.find(pickerId);
        }
        int& activeTab = tabIt->second;
        if (activeTab >= count) activeTab = 0;

        // Tab row (only when count > 1) — tabs span full popup width equally
        if (count > 1) {
            float tabW = popupW / (float)count; // equal width per tab
            float underlineX = 0.0f;
            float underlineW = 0.0f;
            float underlineAnim = 0.0f; // selAnim of the active tab, for fade-in

            for (int i = 0; i < count; i++) {
                const char* name = names[i];
                ImVec2 textSize = ImGui::CalcTextSize(name);
                ImVec2 tabMin(popPos.x + i * tabW, popPos.y);
                ImVec2 tabMax(tabMin.x + tabW, popPos.y + tabRowH);

                bool tabHovered = mousePos.x >= tabMin.x && mousePos.x <= tabMax.x &&
                                  mousePos.y >= tabMin.y && mousePos.y <= tabMax.y;
                if (tabHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    activeTab = i;

                // Smooth selected fade-in — same Animate() pattern the
                // sidebar tab / subtab use, so the sheen lerps in when a
                // tab becomes selected instead of snapping on.
                float selAnim = Animate(pickerId + 5100 + i,
                                        (i == activeTab) ? 1.0f : 0.0f, 16.0f);

                // Selected sheen — Border color, transparent at TOP fading
                // to ~35% alpha at BOTTOM so it emanates from the underline
                // upward. Scaled by selAnim so the whole sheen fades in.
                if (selAnim > 0.005f) {
                    ImU32 topCol = MenuStyle::Colors::Border.alpha(0.00f).u32();
                    ImU32 botCol = MenuStyle::Colors::Border.alpha(0.35f * selAnim).u32();
                    popDl->AddRectFilledMultiColor(
                        tabMin, tabMax, topCol, topCol, botCol, botCol);
                }

                // Text — center of tab cell. Lerps from TextMuted → Accent
                // as the tab becomes selected, tracking the sheen fade-in.
                ImU32 textCol = LerpColor(MenuStyle::Colors::TextMuted.u32(),
                                          MenuStyle::Colors::Accent.u32(),
                                          selAnim);
                float textX = tabMin.x + (tabW - textSize.x) * 0.5f;
                float textY = popPos.y + (tabRowH - textSize.y) * 0.5f;
                popDl->AddText(ImVec2(textX, textY), textCol, name);

                if (i == activeTab) {
                    underlineX = tabMin.x;
                    underlineW = tabW;
                    underlineAnim = selAnim;
                }
            }

            // Static underline — snaps to the selected tab (no slide),
            // but alpha fades in with the same selAnim as the sheen.
            float ulY = popPos.y + tabRowH - 2.0f;
            ImU32 ulCol = MenuStyle::Colors::Accent.alpha(underlineAnim).u32();
            popDl->AddRectFilled(
                ImVec2(underlineX, ulY),
                ImVec2(underlineX + underlineW, ulY + 2.0f),
                ulCol, 1.0f);

            cursorY = tabRowH;

            popDl->AddLine(ImVec2(popPos.x, popPos.y + cursorY),
                           ImVec2(popPos.x + popupW, popPos.y + cursorY),
                           MenuStyle::Colors::Border.u32(), 1.0f);
            cursorY += sepH;
        }

        // Custom picker
        ImGui::SetCursorPos(ImVec2(pad, cursorY + pad));
        changed = DrawCustomPicker(pickerId, cols[activeTab], popupW - pad * 2.0f);

        // Bottom padding matches sides/top (pad)
        float contentBottom = ImGui::GetCursorPos().y + pad;
        ImGui::SetCursorPos(ImVec2(0, contentBottom));
        ImGui::Dummy(ImVec2(popupW, 0));

        popDl->ChannelsMerge();
        ImGui::PopStyleVar(); // Alpha

        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    return changed;
}

// ─── ColorPicker (standalone with label) ────────────────────────────────

bool ColorPicker(const char* label, float col[4]) {
    ImGui::PushID(label);
    ImGuiID pickerId = ImGui::GetID("##picker");

    NonCheckboxIndentScope _indent;
    float width = GetWidgetWidth() - 2.0f * NonCheckboxIndent();
    ImVec2 base = ImGui::GetCursorScreenPos();
    float lineH = ImGui::GetTextLineHeight();
    const float swatchW = S(24.0f);
    const float swatchH = S(12.0f);
    const float rowH = S(22.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Swatch — hard rectangle (no rounding). Widget-standard gradient
    // painted with the current colour as the base tone, then the pure-
    // black WidgetOutline framing it. Same visual language as every
    // other interactive widget.
    //
    // Floor both X and Y to integer pixels — AddRectFilled and AddRect
    // handle sub-pixel origins differently, so at fractional coords
    // (any non-integer DPI scale) the fill's AA fringe pokes 1 px
    // past the outline. Same fix ColorPickerInline uses on its Y coord.
    float swatchX = std::floor(base.x + width - swatchW - S(2.0f));
    float swatchY = std::floor(base.y + (rowH - swatchH) * 0.5f);
    ImVec2 swatchMin(swatchX, swatchY);
    ImVec2 swatchMax(std::floor(swatchX + swatchW),
                     std::floor(swatchY + swatchH));
    DrawWidgetGradient(dl, swatchMin, swatchMax,
        ImGui::GetColorU32(ImVec4(col[0], col[1], col[2], col[3])));
    dl->AddRect(swatchMin, swatchMax,
                MenuStyle::Colors::WidgetOutline.u32(), 0.0f, 0, 1.0f);

    // Click area — left = open picker, right = open copy popup
    ImGui::SetCursorScreenPos(base);
    bool popupWasOpen = ImGui::IsPopupOpen("##picker");
    if (ImGui::InvisibleButton("##swatch", ImVec2(width, rowH))) {
        if (!popupWasOpen) {
            ImGui::OpenPopup("##picker");
            s_animState[pickerId + 3000] = 0.0f; // reset open anim
        }
    }
    bool hovered = ImGui::IsItemHovered();
    float hoverAnim = Animate(pickerId + 6000, hovered ? 1.0f : 0.0f);

    // Label — a color picker always carries a color, so the label stays
    // at Text (white) regardless of hover. Same rule Combo uses: any
    // widget whose value can't be "off/none" gets a solid white label.
    (void)hoverAnim;
    float cy = base.y + (rowH - lineH) * 0.5f - 1.0f;
    MenuStyle::TextShadow(dl, ImVec2(base.x, cy),
                          MenuStyle::Colors::Text.u32(), label);

    ImGuiID copyUid = ImGui::GetID("##copy_color");
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        OpenColorCopyPastePopup(copyUid,
            ImVec2(swatchMin.x, swatchMax.y + S(4.0f)));
    }

    bool changed = false;

    // Right-click copy / paste popup — themed
    {
        float arr[1][4] = { { col[0], col[1], col[2], col[3] } };
        const char* const noNames[] = { "" };
        if (RenderColorCopyPastePopup(copyUid, arr, noNames, 1)) {
            col[0] = arr[0][0]; col[1] = arr[0][1];
            col[2] = arr[0][2]; col[3] = arr[0][3];
            // Re-sync hue from new RGB
            float nh, ns, nv;
            ImGui::ColorConvertRGBtoHSV(col[0], col[1], col[2], nh, ns, nv);
            if (ns > 0.01f && nv > 0.01f) s_pickerHue[pickerId] = nh;
            changed = true;
        }
    }

    // Draw picker popup
    float arr[1][4] = { { col[0], col[1], col[2], col[3] } };
    const char* const noNames[] = { "" };
    if (DrawPickerPopup("##picker", arr, noNames, 1, swatchMax, pickerId)) {
        col[0] = arr[0][0]; col[1] = arr[0][1];
        col[2] = arr[0][2]; col[3] = arr[0][3];
        changed = true;
    }

    ImGui::PopID();
    return changed;
}

// ─── ColorPickerInline (multi-color) ────────────────────────────────────

bool ColorPickerInline(const char* id, float cols[][4], const char* const names[], int count) {
    ImGui::PushID(id);
    ImGuiID pickerId = ImGui::GetID("##inlinepicker");

    ImVec2 prevMin = ImGui::GetItemRectMin();
    ImVec2 prevMax = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float totalSwatchW = S(24.0f); // same size for single and multi
    const float swatchH = S(12.0f);

    // Position swatch right-aligned inside the widget row. Uses the same
    // right-inset as non-checkbox widget bodies (NonCheckboxIndent) so
    // an inline picker on a checkbox row lines up with the right edges
    // of the sliders / combos / textboxes above and below.
    float widgetRight = prevMin.x + GetWidgetWidth();
    float rowH = prevMax.y - prevMin.y;
    // Floor X and Y — AddRectFilled and AddRect handle sub-pixel origins
    // differently, so at fractional coords (non-integer DPI scale) the
    // fill's AA fringe pokes 1 px past the outline on both edges.
    float swatchX = std::floor(widgetRight - totalSwatchW - NonCheckboxIndent());
    float swatchY = std::floor(prevMin.y + (rowH - swatchH) * 0.5f);
    ImVec2 swatchMin(swatchX, swatchY);
    ImVec2 swatchMax(std::floor(swatchX + totalSwatchW),
                     std::floor(swatchY + swatchH));

    if (count == 1) {
        // Single tile — widget-standard gradient over the swatch colour.
        ImU32 col = ImGui::GetColorU32(ImVec4(cols[0][0], cols[0][1], cols[0][2], cols[0][3]));
        DrawWidgetGradient(dl, swatchMin, swatchMax, col);
    } else {
        // Multi-color: same shape as single-color, split evenly into N
        // tiles. Each tile gets its own DrawWidgetGradient so it reads
        // like a mini single-color swatch. Tile boundaries are floored
        // to integer pixels so adjacent tiles butt up cleanly with no
        // AA seam bleed between them.
        for (int i = 0; i < count; i++) {
            ImU32 c = ImGui::GetColorU32(
                ImVec4(cols[i][0], cols[i][1], cols[i][2], cols[i][3]));
            float tx0 = std::floor(swatchMin.x + (totalSwatchW * i) / (float)count);
            float tx1 = std::floor(swatchMin.x + (totalSwatchW * (i + 1)) / (float)count);
            ImVec2 tMin(tx0, swatchMin.y);
            ImVec2 tMax(tx1, swatchMax.y);
            DrawWidgetGradient(dl, tMin, tMax, c);
        }
    }
    dl->AddRect(swatchMin, swatchMax,
                MenuStyle::Colors::WidgetOutline.u32(), 0.0f, 0, 1.0f);

    // Click detection — toggle popup (respect window z-order)
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool swatchHovered = mousePos.x >= swatchMin.x && mousePos.x <= swatchMax.x &&
                         mousePos.y >= swatchMin.y && mousePos.y <= swatchMax.y &&
                         ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    bool popupWasOpen = ImGui::IsPopupOpen("##inlinecpop");
    if (swatchHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !popupWasOpen) {
        ImGui::OpenPopup("##inlinecpop");
        s_animState[pickerId + 3000] = 0.0f; // reset open anim
    }
    ImGuiID inlineCopyUid = ImGui::GetID("##copy_inline_color");
    if (swatchHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        OpenColorCopyPastePopup(inlineCopyUid,
            ImVec2(swatchMin.x, swatchMax.y + S(4.0f)));
    }

    bool changed = false;

    // Themed right-click copy/paste popup
    if (RenderColorCopyPastePopup(inlineCopyUid, cols, names, count))
        changed = true;

    if (DrawPickerPopup("##inlinecpop", cols, names, count, swatchMax, pickerId))
        changed = true;

    ImGui::PopID();
    return changed;
}

// ─── ColorPickerInline (single-color) ───────────────────────────────────

bool ColorPickerInline(const char* id, float col[4]) {
    float cols[1][4] = { { col[0], col[1], col[2], col[3] } };
    const char* const names[] = { "" };
    bool changed = ColorPickerInline(id, cols, names, 1);
    if (changed) {
        col[0] = cols[0][0]; col[1] = cols[0][1];
        col[2] = cols[0][2]; col[3] = cols[0][3];
    }
    return changed;
}

// ─── Sliding tab bar state ──────────────────────────────────────────────
// Bars slide across tabs. Offsets are relative to first tab's position each frame.

// Sidebar bar (vertical slide)
static float s_stBarOff = -1.0f;      // current lerped Y offset from first tab
static float s_stBarTarget = 0.0f;    // target Y offset
static float s_stBarH = 36.0f;        // bar height (= tab height)
static float s_stBarOriginX = 0.0f;   // first tab screen X (set each frame)
static float s_stBarOriginY = 0.0f;   // first tab screen Y (set each frame)
static float s_stBarW = 0.0f;         // tab width
static int   s_stBarFrame = -1;       // frame counter for origin reset

// Bottom tab bar (horizontal slide)
static float s_btBarOff = -1.0f;      // current lerped X offset from first tab
static float s_btBarTarget = 0.0f;    // target X offset
static float s_btBarTabW = 0.0f;      // bar width (= tab width)
static float s_btBarOriginX = 0.0f;   // first tab screen X
static float s_btBarOriginY = 0.0f;   // first tab screen Y
static float s_btBarH = 65.0f;        // tab height
static int   s_btBarFrame = -1;

// Top subtab bar (horizontal slide) — under title bar
static float s_tsBarOff = -1.0f;      // current lerped X offset from first tab
static float s_tsBarTarget = 0.0f;    // target X offset
static float s_tsBarTabW = 0.0f;      // bar width (= tab width)
static float s_tsBarOriginX = 0.0f;   // first tab screen X
static float s_tsBarOriginY = 0.0f;   // first tab screen Y
static float s_tsBarH = 34.0f;        // tab height
static int   s_tsBarFrame = -1;

// Called AFTER all sidebar tabs — lerps and draws the accent bar on top
void DrawSidebarTabIndicator() {
    if (s_stBarOff < 0) {
        if (s_stBarH <= 0) return;
        s_stBarOff = s_stBarTarget; // snap on first use
    }
    float dt = ImGui::GetIO().DeltaTime;
    s_stBarOff += (s_stBarTarget - s_stBarOff) * std::min(16.0f * dt, 1.0f);
    if (std::abs(s_stBarTarget - s_stBarOff) < 0.5f) s_stBarOff = s_stBarTarget;

    // Draw accent bar at lerped position
    float y = s_stBarOriginY + s_stBarOff;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(s_stBarOriginX + S(2.0f), y),
                      ImVec2(s_stBarOriginX + S(5.0f), y + s_stBarH),
                      MenuStyle::Colors::Accent.u32(), 0.0f);
}

// Called AFTER all top subtabs — lerps and draws the accent bar on top
void DrawTopSubTabIndicator() {
    if (s_tsBarOff < 0) {
        if (s_tsBarTabW <= 0) return;
        s_tsBarOff = s_tsBarTarget; // snap on first use
    }
    float dt = ImGui::GetIO().DeltaTime;
    s_tsBarOff += (s_tsBarTarget - s_tsBarOff) * std::min(28.0f * dt, 1.0f);
    if (std::abs(s_tsBarTarget - s_tsBarOff) < 0.5f) s_tsBarOff = s_tsBarTarget;

    // Draw accent bar at lerped position (bottom edge of subtab row)
    float x = s_tsBarOriginX + s_tsBarOff;
    float y = s_tsBarOriginY;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x, y + s_tsBarH - S(2.0f)),
                      ImVec2(x + s_tsBarTabW, y + s_tsBarH),
                      MenuStyle::Colors::Accent.u32(), 0.0f);
}

// Called AFTER all bottom tabs — lerps and draws the accent bar on top
void DrawBottomTabIndicator() {
    if (s_btBarOff < 0) {
        if (s_btBarTabW <= 0) return;
        s_btBarOff = s_btBarTarget; // snap on first use
    }
    float dt = ImGui::GetIO().DeltaTime;
    s_btBarOff += (s_btBarTarget - s_btBarOff) * std::min(28.0f * dt, 1.0f);
    if (std::abs(s_btBarTarget - s_btBarOff) < 0.5f) s_btBarOff = s_btBarTarget;

    // Draw accent bar at lerped position
    float x = s_btBarOriginX + s_btBarOff;
    float y = s_btBarOriginY;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x, y + s_btBarH - S(5.0f)),
                      ImVec2(x + s_btBarTabW, y + s_btBarH - S(2.0f)),
                      MenuStyle::Colors::Accent.u32(), 0.0f);
}

// ─── SidebarTab ─────────────────────────────────────────────────────────

bool SidebarTab(const char* label, bool selected) {
    float width = ImGui::GetContentRegionAvail().x;
    // Row baked with vertical padding so subtabs can stack with zero
    // ItemSpacing between them and still leave breathing room around the
    // label. Any pitch adjustment happens here — the caller pushes 0
    // ItemSpacing.y to eliminate the between-row gap.
    float height = S(28.0f);
    float hPad = S(12.0f);         // left inset for the label
    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(label);
    bool clicked = ImGui::InvisibleButton("##stab", ImVec2(width, height));
    ImGuiID id = ImGui::GetItemID();
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // First tab each frame sets origin
    int curFrame = ImGui::GetFrameCount();
    if (s_stBarFrame != curFrame) {
        s_stBarOriginX = pos.x;
        s_stBarOriginY = pos.y;
        s_stBarW = width;
        s_stBarFrame = curFrame;
    }
    if (selected) {
        s_stBarTarget = pos.y - s_stBarOriginY;
        s_stBarH = height;
    }

    float selAnim   = Animate(id + 0, selected ? 1.0f : 0.0f, 16.0f);
    float hoverAnim = Animate(id + 1, hovered  ? 1.0f : 0.0f, 20.0f);

    // Background bounds — inset on all sides so the gradient reads as an
    // inner sheen sitting inside the sidebar frame. sideInset controls the
    // left/right distance; vertInset is applied top AND bottom, so the gap
    // between two adjacent rows equals 2 * vertInset. Topmost row gets an
    // additional 2 * vertInset on top so the gap above the first row
    // matches the between-row gap (sidebar border alone left 0 gap).
    bool isTopRow = (pos.y == s_stBarOriginY);
    const float sideInset = S(4.0f);
    const float vertInset = S(1.0f);                          // 2 px gap between adjacent rows
    const float topExtra  = isTopRow ? vertInset * 2.0f : 0.0f;
    ImVec2 bgMin(pos.x + sideInset,         pos.y + vertInset + topExtra);
    ImVec2 bgMax(pos.x + width - sideInset, pos.y + height - vertInset);

    // Hover / selected fills use a left→right gradient of Border color
    // fading from 20% alpha on the left to fully transparent on the right.
    // Overlaid on the sidebar bg so no RGB mixing is needed — same treatment
    // the container title bar uses.
    using MenuStyle::Colors::Border;
    const float kSheenAlpha = 0.25f;

    if (hoverAnim > 0.01f && selAnim < 0.99f) {
        ImU32 left  = Border.alpha(kSheenAlpha * hoverAnim).u32();
        ImU32 right = Border.alpha(0.0f).u32();
        dl->AddRectFilledMultiColor(bgMin, bgMax, left, right, right, left);
    }

    if (selAnim > 0.01f) {
        ImU32 left  = Border.alpha(kSheenAlpha * selAnim).u32();
        ImU32 right = Border.alpha(0.0f).u32();
        dl->AddRectFilledMultiColor(bgMin, bgMax, left, right, right, left);

        // 1 px accent side bar hugging the gradient's left edge — fades in
        // with selection so it reads as part of the same sheen.
        ImU32 accentBar = MenuStyle::Colors::Accent.alpha(selAnim).u32();
        dl->AddRectFilled(bgMin, ImVec2(bgMin.x + 1.0f, bgMax.y), accentBar);
    }

    // Text — muted default, brightens toward Text on hover, and swaps to
    // Accent on selection. Selection wins over hover.
    ImU32 mutedCol  = MenuStyle::Colors::TextMuted.u32();
    ImU32 hoverCol  = LerpColor(mutedCol, MenuStyle::Colors::Text.u32(), hoverAnim);
    ImU32 selCol    = MenuStyle::Colors::Accent.u32();
    ImU32 textCol   = LerpColor(hoverCol, selCol, selAnim);

    // Vertically centre the label — pure math center, rounded to nearest
    // pixel so the shadow stays crisp regardless of DPI.
    ImVec2 textSize = ImGui::CalcTextSize(label);
    ImVec2 textPos(std::floor(pos.x + hPad + 0.5f),
                   std::floor(pos.y + (height - textSize.y) * 0.5f + 0.5f));
    MenuStyle::TextShadow(dl, textPos, textCol, label);

    ImGui::PopID();
    return clicked;
}

// ─── TopSubTab ──────────────────────────────────────────────────────────

bool TopSubTab(const char* label, const char* icon, bool selected, float width) {
    float height = S(34.0f);
    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(label);
    bool clicked = ImGui::InvisibleButton("##tstab", ImVec2(width, height));
    ImGuiID id = ImGui::GetItemID();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // First tab each frame sets origin
    int curFrame = ImGui::GetFrameCount();
    if (s_tsBarFrame != curFrame) {
        s_tsBarOriginX = pos.x;
        s_tsBarOriginY = pos.y;
        s_tsBarH = height;
        s_tsBarFrame = curFrame;
    }
    if (selected) {
        s_tsBarTarget = pos.x - s_tsBarOriginX;
        s_tsBarTabW = width;
    }

    // Animate selection (fade)
    float selAnim = Animate(id, selected ? 1.0f : 0.0f, 16.0f);

    // Background — fades in/out per tab (leave 1px at bottom so the bar's border line stays visible)
    if (selAnim > 0.01f) {
        dl->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + width, pos.y + height - 1.0f),
                          LerpColor(MenuStyle::Colors::Ink.alpha(0.0f).u32(), MenuStyle::Colors::TopSubtabSelectedBg.u32(), selAnim), 0.0f);
    }

    // Colors — icon fades to accent, text fades brighter
    ImU32 mutedCol = MenuStyle::Colors::TextMuted.u32();
    ImU32 iconCol = LerpColor(mutedCol, MenuStyle::Colors::Accent.u32(), selAnim);
    ImU32 textCol = LerpColor(mutedCol, MenuStyle::Colors::White.u32(), selAnim);

    bool hasIcon = (icon && icon[0] != '\0');
    float iconFontSize = S(14.0f);
    float gap = hasIcon ? S(6.0f) : 0.0f;

    // Measure icon + label so the pair can be centered together
    ImVec2 iconDim(0.0f, 0.0f);
    if (hasIcon) {
        if (MenuStyle::g_fontIcon)
            iconDim = MenuStyle::g_fontIcon->CalcTextSizeA(iconFontSize, FLT_MAX, 0.0f, icon);
        else
            iconDim = ImGui::CalcTextSize(icon);
    }
    ImVec2 textSize = ImGui::CalcTextSize(label);
    float totalW = iconDim.x + gap + textSize.x;

    float startX = pos.x + (width - totalW) * 0.5f;
    float cy = pos.y + height * 0.5f;

    // Draw icon (vertically centered)
    if (hasIcon) {
        float iconY = cy - iconDim.y * 0.5f;
        if (MenuStyle::g_fontIcon)
            MenuStyle::TextShadow(dl, MenuStyle::g_fontIcon, iconFontSize,
                                  ImVec2(startX, iconY), iconCol, icon);
        else
            MenuStyle::TextShadow(dl, ImVec2(startX, iconY), iconCol, icon);
    }

    // Draw label after icon
    float textX = startX + iconDim.x + gap;
    float textY = cy - textSize.y * 0.5f;
    MenuStyle::TextShadow(dl, ImVec2(textX, textY), textCol, label);

    ImGui::PopID();
    return clicked;
}

// ─── BottomTab ──────────────────────────────────────────────────────────

bool BottomTab(const char* label, const char* icon, bool selected, float width) {
    float height = S(52.0f);
    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(label);
    bool clicked = ImGui::InvisibleButton("##btab", ImVec2(width, height));
    ImGuiID id = ImGui::GetItemID();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // First tab each frame sets origin
    int curFrame = ImGui::GetFrameCount();
    if (s_btBarFrame != curFrame) {
        s_btBarOriginX = pos.x;
        s_btBarOriginY = pos.y;
        s_btBarH = height;
        s_btBarFrame = curFrame;
    }
    if (selected) {
        s_btBarTarget = pos.x - s_btBarOriginX;
        s_btBarTabW = width;
    }

    // Animate selection (icon color fade + icon size). Faster rate so we spend less
    // time in fractional-size frames where the glyph rasterization shimmers.
    float selAnim = Animate(id, selected ? 1.0f : 0.0f, 30.0f);

    // No per-tab background — the gradient now spans the whole bottom bar (drawn in RenderBottomTabBar).

    // Icon: grows in place from small → normal. Size is rounded to an integer pixel so
    // each frame is a crisp, consistently rasterized glyph. Position is centered on the
    // FULL reference size so the icon doesn't drift as it grows, then rounded to integers.
    float smallSize  = S(20.0f);
    float normalSize = S(26.0f);
    float iconFontSize = std::floor(smallSize + (normalSize - smallSize) * selAnim + 0.5f);

    ImVec2 iconDim;
    if (MenuStyle::g_fontIcon)
        iconDim = MenuStyle::g_fontIcon->CalcTextSizeA(iconFontSize, FLT_MAX, 0.0f, icon);
    else
        iconDim = ImGui::CalcTextSize(icon);

    // Reference dimensions at full size — used so the glyph center stays fixed across the animation.
    ImVec2 refDim;
    if (MenuStyle::g_fontIcon)
        refDim = MenuStyle::g_fontIcon->CalcTextSizeA(normalSize, FLT_MAX, 0.0f, icon);
    else
        refDim = iconDim;
    float refCX = pos.x + width  * 0.5f;
    float refCY = pos.y + height * 0.5f;

    ImU32 mutedCol = MenuStyle::Colors::TextMuted.u32();
    ImU32 iconCol = LerpColor(mutedCol, MenuStyle::Colors::Accent.u32(), selAnim);

    float iconX = std::floor(refCX - iconDim.x * 0.5f + 0.5f);
    float iconY = std::floor(refCY - iconDim.y * 0.5f + 0.5f);
    (void)refDim; // ref only used to anchor, dims not directly needed

    if (MenuStyle::g_fontIcon)
        MenuStyle::TextShadow(dl, MenuStyle::g_fontIcon, iconFontSize, ImVec2(iconX, iconY), iconCol, icon);
    else
        MenuStyle::TextShadow(dl, ImVec2(iconX, iconY), iconCol, icon);

    ImGui::PopID();
    return clicked;
}

// ─── SideTab — vertical rail variant of BottomTab ───────────────────────
//
// Same visual language as BottomTab (icon color fades muted → accent on
// select, icon grows small → large in place). Difference: caller places
// each tab as a fixed-size slot in a vertical column via SetCursorScreenPos
// between calls, rather than SameLine-ing horizontally.

bool SideTab(const char* label, const char* icon, bool selected,
             float width, float height) {
    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(label);
    bool clicked = ImGui::InvisibleButton("##stab", ImVec2(width, height));
    ImGuiID id = ImGui::GetItemID();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float selAnim = Animate(id, selected ? 1.0f : 0.0f, 30.0f);

    // Icon: grows in place from small → normal, same as BottomTab.
    float smallSize  = S(20.0f);
    float normalSize = S(24.0f);
    float iconFontSize = std::floor(smallSize + (normalSize - smallSize) * selAnim + 0.5f);

    ImVec2 iconDim;
    if (MenuStyle::g_fontIcon)
        iconDim = MenuStyle::g_fontIcon->CalcTextSizeA(iconFontSize, FLT_MAX, 0.0f, icon);
    else
        iconDim = ImGui::CalcTextSize(icon);

    float refCX = pos.x + width  * 0.5f;
    float refCY = pos.y + height * 0.5f;

    ImU32 mutedCol = MenuStyle::Colors::TextMuted.u32();
    ImU32 iconCol = LerpColor(mutedCol, MenuStyle::Colors::Accent.u32(), selAnim);

    float iconX = std::floor(refCX - iconDim.x * 0.5f + 0.5f);
    float iconY = std::floor(refCY - iconDim.y * 0.5f + 0.5f);

    if (MenuStyle::g_fontIcon)
        dl->AddText(MenuStyle::g_fontIcon, iconFontSize, ImVec2(iconX, iconY), iconCol, icon);
    else
        dl->AddText(ImVec2(iconX, iconY), iconCol, icon);

    (void)label; // label unused in this icon-only layout; kept for future tooltip

    ImGui::PopID();
    return clicked;
}

// ─── TopTextTab — pandora-style plain-text nav tab ──────────────────────
//
// Rendered in the menu's top strip beside the logo. Selected → bold + full
// text colour. Unselected → regular weight + muted. Hover → fades toward
// selected colour. No underline, no fill — the type change IS the state.

bool TopTextTab(const char* label, bool selected, float width, float height,
                ImDrawFlags cornerFlags, float openAnim) {
    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(label);
    bool clicked = ImGui::InvisibleButton("##ttab", ImVec2(width, height));
    ImGuiID id = ImGui::GetItemID();
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float selAnim   = Animate(id + 100, selected ? 1.0f : 0.0f, 22.0f);
    float hoverAnim = Animate(id + 101, hovered  ? 1.0f : 0.0f, 22.0f);
    float showAnim  = std::max(selAnim, hoverAnim * 0.5f);

    // Selected background — MenuLight fills the full tab slot AND paints
    // one extra pixel past the bottom edge so the strip's underline
    // (drawn by RenderTopStrip 1px above stripH) is covered. Effect: the
    // active tab looks like a continuous piece of the menu window bg
    // underneath. On top of the fill, a thin accent bar hugs the tab's
    // top edge (same corner rounding so it follows the curve) and 1px
    // side outlines run down the left and right edges.
    //
    // openAnim modulates the fill's HEIGHT during the menu open animation:
    // the top edge of the fill starts at the tab's bottom and rises up as
    // the anim lerps 0 → 1, so the selected tab visually "opens up from
    // the bottom".
    if (selAnim > 0.01f) {
        int a = (int)(selAnim * 255.0f);

        // Rounding disabled for now — cornerFlags is ignored below so every
        // tab draws with hard rectangular corners. Bump this back to
        // ~S(3-4) when reintroducing rounded tabs.
        float rounding = 0.0f;
        (void)cornerFlags;
        float fillTop = pos.y + height * (1.0f - openAnim);
        ImVec2 bgMin(pos.x, fillTop);
        ImVec2 bgMax(pos.x + width, pos.y + height + 1.0f);

        // Body fill — TopTabBodyFill from the palette.
        ImU32 bodyBase = MenuStyle::Colors::TopTabBodyFill.u32();
        ImU32 bodyFill = (bodyBase & 0x00FFFFFFu) | ((ImU32)a << 24);
        dl->AddRectFilled(bgMin, bgMax, bodyFill, rounding, cornerFlags);

        // Top-edge glow — TopTabGlowTop at partial alpha at the top vertices,
        // transparent at the bottom vertices, so the tone fades cleanly into
        // the body over roughly the top half of the tab. kGlowTopAlpha tunes
        // glow intensity.
        float glowH = height * 0.55f;
        const int kGlowTopAlpha = 90;              // ~35% opacity at full select
        int topA = (int)(selAnim * (float)kGlowTopAlpha);
        ImU32 glowBase = MenuStyle::Colors::TopTabGlowTop.u32();
        ImU32 topGlow  = (glowBase & 0x00FFFFFFu) | ((ImU32)topA << 24);
        ImU32 botGlow  = glowBase & 0x00FFFFFFu;   // alpha 0
        dl->AddRectFilledMultiColor(bgMin,
                                    ImVec2(bgMax.x, bgMin.y + glowH),
                                    topGlow, topGlow, botGlow, botGlow);

        // Side outlines — vertical 1px lines matching the outer menu
        // border colour (Colors::Border()) so every outline in the menu
        // reads as one system. Left edge is offset one pixel further left
        // so it aligns with the tab's own left boundary instead of half-
        // overlapping the body fill.
        ImU32 outlineBase = MenuStyle::Colors::Border.u32();
        ImU32 outlineCol = (outlineBase & 0x00FFFFFFu) | ((ImU32)a << 24);
        float outlineTop    = bgMin.y + rounding - 1.0f;
        // Stop 1px short of the tab's bottom — AddLine's AA endpoint extends
        // one row past p2.y, which was poking out below the tab body.
        float outlineBottom = pos.y + height - 1.0f;
        dl->AddLine(ImVec2(bgMin.x - 0.5f, outlineTop),
                    ImVec2(bgMin.x - 0.5f, outlineBottom),
                    outlineCol, 1.0f);
        dl->AddLine(ImVec2(bgMax.x - 0.5f, outlineTop),
                    ImVec2(bgMax.x - 0.5f, outlineBottom),
                    outlineCol, 1.0f);
    }

    ImU32 textCol = LerpColor(MenuStyle::Colors::TextMuted.u32(),
                              MenuStyle::Colors::Text.u32(), showAnim);

    ImFont* font = (selected && MenuStyle::g_fontBold) ? MenuStyle::g_fontBold
                                                       : ImGui::GetFont();
    float fontSize = ImGui::GetFontSize();
    ImVec2 sz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
    float tx = std::floor(pos.x + (width  - sz.x) * 0.5f);
    float ty = std::floor(pos.y + (height - sz.y) * 0.5f);
    MenuStyle::TextShadow(dl, font, fontSize, ImVec2(tx, ty), textCol, label);

    ImGui::PopID();
    return clicked;
}

// ─── Settings cog icon + popup ──────────────────────────────────────────

// Deferred state — the cog click sets these, RenderSettingsCogPopup opens/draws.
static bool s_cogPopupPending = false;
static bool s_cogClosePending = false;
static ImVec2 s_cogPopupAnchor;
static std::function<void()> s_cogPopupContentFn;
static std::string s_cogPopupLabel;
static float s_cogPopupFadeAnim = 0.0f;
// Extra width (beyond the popup's base width) reserved for keybind chips
// on widgets inside the popup. Set by the SettingsCog call site; consumed
// in RenderSettingsCogPopup below.
static float s_cogPopupExtraW = 0.0f;

bool SettingsCog(const char* key, const char* label, ImVec2 pos, float size,
                 const std::function<void()>& contentFn,
                 bool rowHovered,
                 float extraPopupWidth) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Hit area is larger than the icon for easier clicking
    float hitPad = S(3.0f);
    ImVec2 hitMin(pos.x - hitPad, pos.y - hitPad);
    ImVec2 hitMax(pos.x + size + hitPad, pos.y + size + hitPad);

    RegisterCogRect(hitMin, hitMax);

    bool isOpen = (s_settingsCogOpen == key);
    bool anyCogPopupOpen = !s_settingsCogOpen.empty();

    // Raw mouse detection — suppress when another window/popup is on top of
    // the cog. IsWindowHovered(ChildWindows) without AllowWhenBlockedByPopup
    // returns false when any popup OR other window covers the mouse, which
    // is what we want. We DO need AllowWhenBlockedByActiveItem though: the
    // cog sits inside a slider/checkbox row whose InvisibleButton becomes
    // ActiveId on mouse-down, and without this flag IsWindowHovered goes
    // false for the rest of the click → the cog reads its own click as
    // "blocked" and the popup never opens.
    ImVec2 mp = ImGui::GetIO().MousePos;
    bool inRect = mp.x >= hitMin.x && mp.x <= hitMax.x &&
                  mp.y >= hitMin.y && mp.y <= hitMax.y;
    bool windowHovered = ImGui::IsWindowHovered(
        ImGuiHoveredFlags_ChildWindows |
        ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    // Any ImGui popup owns mouse input above the underlying menu rows.  In
    // particular, combo/multicombo dropdowns are rendered as popups and must
    // not let a click fall through to a cog positioned on the next row.
    const bool anyPopupOpen = ImGui::IsPopupOpen(
        "", ImGuiPopupFlags_AnyPopupId);
    const bool anotherPopupOpen = anyPopupOpen && !isOpen;
    const bool popupClickConsumed =
        s_popupClickConsumedFrame == ImGui::GetFrameCount();
    bool blocked = anyCogPopupOpen || anotherPopupOpen || popupClickConsumed || !windowHovered;
    bool hovered = inRect && !blocked;

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (isOpen) {
            s_cogClosePending = true;
        } else {
            s_settingsCogOpen = key;
            s_cogPopupPending = true;
            s_cogPopupAnchor = ImVec2(pos.x - S(8.0f), hitMax.y + S(2.0f));
            s_cogPopupContentFn = contentFn;
            s_cogPopupLabel = label;
            s_cogPopupFadeAnim = 0.0f;
            s_cogPopupExtraW = extraPopupWidth;
        }
    }

    // Keep the popup's stored state fresh every frame while THIS cog is
    // the open one. Without this refresh, adding a keybind chip while the
    // popup is open leaves s_cogPopupExtraW stuck at whatever it was on
    // click — chips then render OUTSIDE the popup's rect, and clicking
    // them lands outside the popup so ImGui closes it.
    if (isOpen) {
        s_cogPopupExtraW    = extraPopupWidth;
        s_cogPopupContentFn = contentFn;
        s_cogPopupLabel     = label;
    }

    // Animate hover brightness — brightens when hovering the cog OR the parent row
    // Suppress when any settings popup is open
    ImGuiID animHoverId = ImGui::GetID(key);
    bool showHover = !blocked && (hovered || rowHovered);
    float hoverAnim = Animate(animHoverId, showHover ? 1.0f : 0.0f);

    // Spin animation when hovered or open
    ImGuiID animSpinId = animHoverId + 1;
    float spinTarget = (hovered || isOpen) ? 1.0f : 0.0f;
    float spinAnim = Animate(animSpinId, spinTarget, 6.0f);

    // Draw the cog icon (centered in the original pos/size, not the hit area)
    const char* icon = "\xef\x80\x93"; // FA gear f013
    ImU32 col = isOpen ? MenuStyle::Colors::Accent.u32()
                       : LerpColor(MenuStyle::Colors::IconCogRest.u32(),
                                   MenuStyle::Colors::Text.u32(), hoverAnim);

    float cx = pos.x + size * 0.5f;
    float cy = pos.y + size * 0.5f;

    if (MenuStyle::g_fontIcon) {
        ImVec2 isz = MenuStyle::g_fontIcon->CalcTextSizeA(size, FLT_MAX, 0.0f, icon);
        float angle = spinAnim * 3.14159265f * 0.5f; // 90 degree rotation

        if (angle > 0.001f) {
            // Rotate by building glyphs into a rotated draw list region
            float cosA = cosf(angle), sinA = sinf(angle);
            // Save/restore: render icon at origin, then rotate around center
            ImVec2 ip(-isz.x * 0.5f, -isz.y * 0.5f);

            // We'll draw each vertex rotated. Use AddText then rotate the last vertices.
            int vtxStart = dl->VtxBuffer.Size;
            dl->AddText(MenuStyle::g_fontIcon, size,
                ImVec2(cx - isz.x * 0.5f, cy - isz.y * 0.5f), col, icon);
            int vtxEnd = dl->VtxBuffer.Size;

            for (int i = vtxStart; i < vtxEnd; i++) {
                ImVec2& v = dl->VtxBuffer[i].pos;
                float dx = v.x - cx, dy = v.y - cy;
                v.x = cx + dx * cosA - dy * sinA;
                v.y = cy + dx * sinA + dy * cosA;
            }
        } else {
            ImVec2 ip(pos.x + (size - isz.x) * 0.5f, pos.y + (size - isz.y) * 0.5f);
            dl->AddText(MenuStyle::g_fontIcon, size, ip, col, icon);
        }
    } else {
        const char* alt = "*";
        ImVec2 isz = ImGui::CalcTextSize(alt);
        ImVec2 ip(pos.x + (size - isz.x) * 0.5f, pos.y + (size - isz.y) * 0.5f);
        dl->AddText(ip, col, alt);
    }

    return hovered;
}

void RenderSettingsCogPopup() {
    // Close request from cog toggle
    if (s_cogClosePending) {
        ImGui::CloseCurrentPopup();
        s_cogClosePending = false;
        s_settingsCogOpen.clear();
        s_cogPopupContentFn = nullptr;
        s_cogPopupFadeAnim = 0.0f;
    }

    if (s_cogPopupPending) {
        ImGui::OpenPopup("##settings_cog");
        s_cogPopupPending = false;
    }

    const float basePopupW = S(200.0f);
    // Widen the popup when any widget inside has keybind chips that spill
    // past their natural NonCheckboxIndent margin. s_cogPopupExtraW is
    // computed by the caller (menu_builder.cpp at the SettingsCog call
    // site) as the max chip-block overflow among the popup's widgets.
    const float popupW = basePopupW + s_cogPopupExtraW;

    bool popupOpen = ImGui::IsPopupOpen("##settings_cog");
    float dt = ImGui::GetIO().DeltaTime;
    s_cogPopupFadeAnim += ((popupOpen ? 1.0f : 0.0f) - s_cogPopupFadeAnim) * std::min(20.0f * dt, 1.0f);
    if (!popupOpen && s_cogPopupFadeAnim < 0.01f) s_cogPopupFadeAnim = 0.0f;

    ImGui::SetNextWindowPos(s_cogPopupAnchor);
    ImGui::SetNextWindowSizeConstraints(ImVec2(popupW, 0), ImVec2(popupW, S(400.0f)));

    // Transparent ImGui shell — BeginContainer draws the whole card/title/
    // border on its own drawlist channel so we don't want ImGui's popup
    // chrome fighting for the same real estate.
    ImGui::PushStyleColor(ImGuiCol_PopupBg,      ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,       ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_NavHighlight, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,  0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);

    if (ImGui::BeginPopup("##settings_cog")) {
        float fade = std::clamp(s_cogPopupFadeAnim, 0.0f, 1.0f);

        // Render the popup body as a proper container — title is the
        // element's own label (e.g. the widget the cog belongs to), full
        // popup width. Same visual language as menu containers.
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade);
        const char* title = s_cogPopupLabel.empty() ? "settings"
                                                    : s_cogPopupLabel.c_str();
        UI::BeginContainer(title, popupW, nullptr, /*collapsable=*/false);

        // Reserve the extra popup width as a chip gutter so widgets inside
        // don't grow into it — same trick RenderContainer uses in
        // menu_builder.cpp. Widgets get the pre-chip visible width; chips
        // land in the reserved gutter to the right.
        if (s_cogPopupExtraW > 0.0f) {
            UI::PushWidgetWidth(UI::GetWidgetWidth() - s_cogPopupExtraW);
        }
        if (s_cogPopupContentFn) s_cogPopupContentFn();
        if (s_cogPopupExtraW > 0.0f) UI::PopWidgetWidth();

        UI::EndContainer();
        ImGui::PopStyleVar(); // Alpha

        // Nested keybind context menu — see the long comment on the old
        // implementation; must be rendered inside this popup's scope so
        // OpenPopup stacks correctly instead of closing us.
        KeybindSystem::RenderContextMenu();
        // Chip's right-click context — must also live inside this popup's
        // scope. The chip's InvisibleButton calls ImGui::OpenPopup at its
        // OWN scope (which is this popup, when the chip lives inside a
        // settings cog), so BeginPopup at this scope finds it.
        UI::RenderKeybindChipContextMenu();

        ImGui::EndPopup();
    } else {
        if (!s_settingsCogOpen.empty()) {
            s_settingsCogOpen.clear();
            s_cogPopupContentFn = nullptr;
        }
    }

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
}

// ─── Tooltip marker (question-mark icon) ────────────────────────────────

// Per-marker hover-delay timers, keyed by string id
static std::unordered_map<std::string, float> s_tooltipHoverTimers;
static const float kTooltipDelay = 0.35f;

bool TooltipMarker(const char* key, ImVec2 pos, float size, const char* tooltipText) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 rmin = pos;
    ImVec2 rmax(pos.x + size, pos.y + size);

    // Hover test against just the icon's rect (no input consumption)
    ImVec2 mp = ImGui::GetIO().MousePos;
    bool hovered = mp.x >= rmin.x && mp.x <= rmax.x && mp.y >= rmin.y && mp.y <= rmax.y &&
                   ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                          ImGuiHoveredFlags_AllowWhenBlockedByPopup);

    // Draw the icon — FA question (f128), a bare '?' glyph with no circle
    // background. Fallback to ImGui's default font "?" when the icon font
    // isn't loaded.
    const char* icon = "\xef\x84\xa8"; // FA question f128
    ImU32 col = hovered ? MenuStyle::Colors::Accent.u32() : MenuStyle::Colors::IconMuted.u32();
    if (MenuStyle::g_fontIcon) {
        ImVec2 isz = MenuStyle::g_fontIcon->CalcTextSizeA(size, FLT_MAX, 0.0f, icon);
        ImVec2 ip(rmin.x + (size - isz.x) * 0.5f, rmin.y + (size - isz.y) * 0.5f);
        dl->AddText(MenuStyle::g_fontIcon, size, ip, col, icon);
    } else {
        const char* alt = "?";
        ImVec2 isz = ImGui::CalcTextSize(alt);
        ImVec2 ip(rmin.x + (size - isz.x) * 0.5f, rmin.y + (size - isz.y) * 0.5f);
        dl->AddText(ip, col, alt);
    }

    // Hover-delay timer
    float dt = ImGui::GetIO().DeltaTime;
    float& t = s_tooltipHoverTimers[key];
    if (hovered) t += dt;
    else         t = 0.0f;

    // Show themed tooltip when hover threshold reached
    if (t >= kTooltipDelay && tooltipText && tooltipText[0]) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(8.0f), S(6.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, MenuStyle::Colors::WidgetBase.v);
        ImGui::PushStyleColor(ImGuiCol_Border,  MenuStyle::Colors::Ink.v);
        ImGui::PushStyleColor(ImGuiCol_Text,    MenuStyle::Colors::Text.v);

        // Position tooltip just below + right of the marker
        ImGui::SetNextWindowPos(ImVec2(rmax.x + S(6.0f), rmin.y));
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(S(260.0f));
        ImGui::TextUnformatted(tooltipText);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);
    }

    return hovered;
}

// ─── Tooltip ────────────────────────────────────────────────────────────

void Tooltip(const char* text) {
    if (!text || !text[0]) return;
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal |
                              ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                              ImGuiHoveredFlags_AllowWhenDisabled))
        return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(8.0f), S(6.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, MenuStyle::Colors::WidgetBase.v);
    ImGui::PushStyleColor(ImGuiCol_Border,  MenuStyle::Colors::Ink.v);
    ImGui::PushStyleColor(ImGuiCol_Text,    MenuStyle::Colors::Text.v);

    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(S(260.0f));
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
}

// ─── KeybindChip ────────────────────────────────────────────────────────
//
// Pill-shaped chip drawn to the right of a widget label showing one keybind's
// state. Reads bind data + edit state from KeybindSystem.

// Chip context-menu state — one menu can be open at a time, opened by the
// chip's right-click handler.
static bool  s_chipCtxOpen = false;
static int   s_chipCtxBindIdx = -1;
static ImVec2 s_chipCtxPos = ImVec2(0, 0);
static bool  s_chipCtxPending = false;

// Font-size multiplier for keybind chip text. Slightly smaller than the
// menu's body text so chips read as compact secondary metadata rather
// than fighting with widget labels for attention. Must match the value
// used by menu_builder's chip-block width reserve and by KeybindSystem's
// ChipBlockWidthFor / ChipBlockOverflowFor width measurements.
static float ChipFontSize() { return ImGui::GetFontSize() * 0.85f; }

// Compact key-name formatter for the chip. Falls back to the numeric VK
// when no friendly name exists.
static std::string FormatChipKey(int vk) {
    if (vk == 0) return "";
    const char* n = KeybindSystem::GetKeyName(vk);
    if (n && n[0]) return n;
    char buf[16]; snprintf(buf, sizeof(buf), "%d", vk);
    return buf;
}

static const char* ChipModeShort(KeybindSystem::BindMode m) {
    using KeybindSystem::BindMode;
    switch (m) {
    case BindMode::Toggle: return "toggle";
    case BindMode::Hold:   return "hold";
    case BindMode::OffKey: return "off-key";
    }
    return "";
}

float KeybindChip(const char* elementId, int localBindIdx, ImVec2 pos, float rowH) {
    int globalIdx = KeybindSystem::IndexOfBind(elementId, localBindIdx);
    if (globalIdx < 0) return 0.0f;
    const auto& all = KeybindSystem::GetAllKeybinds();
    const auto& kb  = all[globalIdx];

    // Chip label — text-only:
    //   editing this bind's key → [...]
    //   bound                   → [K]
    //   unbound                 → []
    KeybindSystem::ChipPhase phase = KeybindSystem::GetEditPhase();
    bool isEditingThis = (KeybindSystem::GetEditingBindIndex() == globalIdx);

    std::string inside;
    if (isEditingThis && phase == KeybindSystem::ChipPhase::WaitingForKey) {
        inside = "...";
    } else if (kb.key > 0) {
        inside = FormatChipKey(kb.key);
    } else {
        inside = "-"; // unbound chip reads "[-]" (was "[]")
    }

    char chipText[48];
    snprintf(chipText, sizeof(chipText), "[%s]", inside.c_str());

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Chip text is drawn at a slightly smaller font than body text so
    // chips read as compact metadata. Width measurement uses the same
    // font+size to stay pixel-accurate with the render.
    ImFont* chipFont = ImGui::GetFont();
    float chipFs = ChipFontSize();
    ImVec2 textSize = chipFont->CalcTextSizeA(chipFs, FLT_MAX, 0.0f, chipText);
    float chipW = textSize.x;
    float chipH = textSize.y;

    // Center vertically on the label row so the chip sits on the label
    // baseline rather than the row's optical top.
    ImVec2 chipMin(pos.x, pos.y + std::floor((rowH - chipH) * 0.5f));
    ImVec2 chipMax(chipMin.x + chipW, chipMin.y + chipH);

    // Hit test — manual mouse-rect check (matches how this used to work
    // before the InvisibleButton attempt). The InvisibleButton path had
    // IsItemClicked(Right) returning false in nested-popup contexts for
    // reasons I can't fully nail down; manual works everywhere.
    ImVec2 mp = ImGui::GetIO().MousePos;
    bool hovered = mp.x >= chipMin.x && mp.x <= chipMax.x &&
                   mp.y >= chipMin.y && mp.y <= chipMax.y &&
                   ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                          ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        KeybindSystem::BeginKeyEdit(globalIdx);
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        // Defer OpenPopup — the chip is drawn inside a container (which
        // does PushID(containerLabel)), so calling OpenPopup here would
        // hash the popup id against that container's IDStack seed. But
        // BeginPopup at the top level (or inside a settings-cog popup)
        // has a different IDStack seed, and the ids wouldn't match, so
        // the popup would never open. Deferring to RenderKeybindChip-
        // ContextMenu — which is called at both the top level AND inside
        // the settings-cog popup, with IDStack at those scopes' baseline
        // — makes the OpenPopup / BeginPopup ids consistent.
        s_chipCtxPending = true;
        s_chipCtxBindIdx = globalIdx;
        s_chipCtxPos     = ImVec2(chipMin.x, chipMax.y + S(4.0f));
    }

    // Text-only rendering. An active runtime bind stays accent-colored while
    // the configured checkbox remains visually unchanged.
    using MenuStyle::Colors::Accent;
    using MenuStyle::Colors::Text;
    using MenuStyle::Colors::TextNormal;
    using MenuStyle::Colors::TextMuted;

    ImU32 textCol;
    if (isEditingThis && phase == KeybindSystem::ChipPhase::WaitingForKey) {
        textCol = Accent.u32();
    } else if (kb.active) {
        textCol = Accent.u32();
    } else if (hovered) {
        textCol = Text.u32();     // pure white on hover
    } else if (kb.key > 0) {
        textCol = TextNormal.u32();
    } else {
        textCol = TextMuted.u32();
    }

    ImVec2 textPos(std::floor(chipMin.x), std::floor(chipMin.y));
    MenuStyle::TextShadow(dl, chipFont, chipFs, textPos, textCol, chipText);
    return chipW;
}

void RenderKeybindChipContextMenu() {
    // Consume the pending flag set by KeybindChip on right-click. OpenPopup
    // is called HERE (rather than at the chip) so the popup id is hashed
    // against THIS scope's IDStack — matching whichever BeginPopup below
    // will run. Called from both the top-level render path AND from inside
    // the settings-cog popup: the first one to see the pending flag opens
    // the popup at its scope, which is where BeginPopup finds it too.
    if (s_chipCtxPending) {
        s_chipCtxPending = false;
        ImGui::OpenPopup("##kbchipctx");
    }
    KeybindSystem::Keybind* kb = KeybindSystem::GetKeybindAt(s_chipCtxBindIdx);
    if (!kb) return;

    // Popup layout — mode combo at top, inline value widget beneath (typed
    // per elemType, showing/editing the value the bind applies), delete
    // button at the bottom. Popup width fits the widget content with a
    // small side padding so a Checkbox (which draws at cursor with no
    // indent) doesn't sit flush against the popup's left border.
    const float popupW  = S(200.0f);
    const float sidePad = S(8.0f);

    ImGui::SetNextWindowPos(s_chipCtxPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(popupW, 0), ImVec2(popupW, FLT_MAX));
    // Consistent dark popup styling — matches the widget's right-click
    // "add bind" menu (RenderContextMenu in keybind_system.cpp).
    using MenuStyle::Colors::MenuDark;
    using MenuStyle::Colors::Border;
    using MenuStyle::Colors::WidgetBase;
    using MenuStyle::Colors::Text;
    using MenuStyle::Colors::TextMuted;
    using MenuStyle::Colors::Accent;

    ImGui::PushStyleColor(ImGuiCol_PopupBg,       MenuDark);
    ImGui::PushStyleColor(ImGuiCol_Border,        Border);
    ImGui::PushStyleColor(ImGuiCol_Header,        WidgetBase.alpha(0.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, WidgetBase);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  Accent.alpha(0.35f));
    ImGui::PushStyleColor(ImGuiCol_Text,          Text);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled,  TextMuted);
    ImGui::PushStyleColor(ImGuiCol_Separator,     Border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(sidePad, S(8.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,     ImVec2(S(6.0f), S(6.0f)));

    if (ImGui::BeginPopup("##kbchipctx",
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        // Pin widget width to the content area (popup width minus the two
        // WindowPadding insets) so widgets fill the popup evenly.
        PushWidgetWidth(popupW - sidePad * 2.0f);

        // ── Mode combo ──────────────────────────────────────────────
        static const char* const kModeItems[] = { "toggle", "hold", "off-key" };
        int modeIdx = (int)kb->mode;
        if (UI::Combo("mode", &modeIdx, kModeItems, 3)) {
            kb->mode = (KeybindSystem::BindMode)modeIdx;
        }

        // ── Value widget (element-typed) ────────────────────────────
        using KeybindSystem::ElementType;
        switch (kb->elemType) {
        case ElementType::Checkbox:
            UI::Checkbox("value", &kb->boolValue);
            break;
        case ElementType::SliderFloat:
            UI::SliderFloat("value", &kb->floatValue,
                            kb->floatMin, kb->floatMax,
                            kb->decimalPlaces, nullptr);
            break;
        case ElementType::SliderInt:
            UI::SliderInt("value", &kb->intValue,
                          kb->intMin, kb->intMax, nullptr);
            break;
        case ElementType::Combo: {
            std::vector<const char*> ptrs;
            ptrs.reserve(kb->comboItems.size());
            for (auto& s : kb->comboItems) ptrs.push_back(s.c_str());
            if (!ptrs.empty())
                UI::Combo("value", &kb->intValue, ptrs.data(), (int)ptrs.size());
            break;
        }
        case ElementType::MultiCombo: {
            // Compact — one checkbox per item, in-place.
            for (int i = 0; i < (int)kb->comboItems.size() &&
                            i < (int)kb->multiValues.size(); i++) {
                bool v = kb->multiValues[i];
                if (UI::Checkbox(kb->comboItems[i].c_str(), &v))
                    kb->multiValues[i] = v;
            }
            break;
        }
        }

        // ── Delete row ──────────────────────────────────────────────
        ImGui::Dummy(ImVec2(0, S(2.0f)));
        if (UI::Button("delete bind", 0.0f)) {
            KeybindSystem::RemoveKeybindAt(s_chipCtxBindIdx);
            s_chipCtxBindIdx = -1;
            ImGui::CloseCurrentPopup();
        }

        PopWidgetWidth();
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(8);
}

// ─── Standalone keybind chip ───────────────────────────────────────────
//
// Label on the left, chip on the right. Chip = "[K]" (bound) or "[…]"
// (listening) or "[]" (unbound). Left-click enters key-listen via
// KeybindSystem::BeginStandaloneKeyEdit; right-click does nothing when
// locked (used by the menu-toggle key so it can't be deleted).

void StandaloneKeybindChip(const char* label, int* vkPtr, bool locked) {
    if (!vkPtr) return;
    ImGui::PushID(label);

    // Match the same rightside indent used by every non-checkbox widget so
    // the label / chip align with sliders, combos, inputs, etc. inside the
    // same container.
    NonCheckboxIndentScope _indent;
    ImVec2 base = ImGui::GetCursorScreenPos();
    float width = GetWidgetWidth() - 2.0f * NonCheckboxIndent();
    float lineH = ImGui::GetTextLineHeight();
    float rowH  = lineH + S(6.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Label — same tone as slider / combo labels, drawn at the indented
    // cursor position so its left edge matches those widgets' label X.
    ImVec2 labelPos(base.x, std::floor(base.y + (rowH - lineH) * 0.5f - 1.0f));
    MenuStyle::TextShadow(dl, labelPos,
                          MenuStyle::Colors::TextNormal.u32(), label);

    // Chip — right-aligned at the same X where a non-checkbox widget's
    // frame right edge sits (base.x + width). This is inside the widget
    // area, not past it.
    bool listening = KeybindSystem::IsListeningForStandalone(vkPtr);
    std::string inside;
    if (listening)         inside = "...";
    else if (*vkPtr > 0)   inside = KeybindSystem::GetKeyName(*vkPtr);
    else                   inside = "-";
    char chipText[48];
    snprintf(chipText, sizeof(chipText), "[%s]", inside.c_str());
    // Match the widget-level KeybindChip's slightly-smaller font so both
    // chip variants read at the same visual weight.
    ImFont* chipFont = ImGui::GetFont();
    float   chipFs   = ChipFontSize();
    ImVec2 chipSize = chipFont->CalcTextSizeA(chipFs, FLT_MAX, 0.0f, chipText);
    ImVec2 chipMin(std::floor(base.x + width - chipSize.x),
                   std::floor(base.y + (rowH - chipSize.y) * 0.5f));
    ImVec2 chipMax(chipMin.x + chipSize.x, chipMin.y + chipSize.y);

    // Manual hit-test — no ImGui item submitted so we don't disturb the
    // popup / cursor layout.
    ImVec2 mp = ImGui::GetIO().MousePos;
    bool hovered = mp.x >= chipMin.x && mp.x <= chipMax.x &&
                   mp.y >= chipMin.y && mp.y <= chipMax.y &&
                   ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                          ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        KeybindSystem::BeginStandaloneKeyEdit(vkPtr);
    }
    // Right-click intentionally does nothing while locked. If unlocked
    // and we later want a chip context menu here, wire it via the same
    // s_chipCtxPending path.
    (void)locked;

    ImU32 textCol = listening
        ? MenuStyle::Colors::Accent.u32()
        : (hovered ? MenuStyle::Colors::Text.u32()     // pure white on hover
                   : MenuStyle::Colors::TextNormal.u32());
    MenuStyle::TextShadow(dl, chipFont, chipFs, chipMin, textCol, chipText);

    // Advance layout — reserve the row's height so the next widget sits
    // directly below this one.
    ImGui::Dummy(ImVec2(width, rowH));

    ImGui::PopID();
}

// ─── KeybindLabel ───────────────────────────────────────────────────────

void KeybindLabel(const char* text, float rightEdge) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 textSize = ImGui::CalcTextSize(text);
    float x = rightEdge - textSize.x;
    MenuStyle::TextShadow(ImGui::GetWindowDrawList(), ImVec2(x, pos.y), MenuStyle::Colors::TextMuted.u32(), text);
}

void ResetAnimations() {
    // Only reset slider fill animations (sweep-in effect), leave everything else
    for (ImGuiID id : s_sliderFillIds)
        s_animState.erase(id);
    s_sliderFillIds.clear();
}

void FrameTick() {
    GCAnimStateIfDue();
}

} // namespace UI
