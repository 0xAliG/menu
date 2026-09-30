#include "keybind_system.h"
#include "ui_components.h"
#include "menu_style.h"
#include "input.h"
#include "config_system.h"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cmath>

// Shorthand for DPI-scaled pixel values
#define S(x) (MenuStyle::S(x))

namespace KeybindSystem {

static ImU32 LerpColor(ImU32 a, ImU32 b, float t) {
    int ra = (a >> 0) & 0xFF, ga = (a >> 8) & 0xFF, ba = (a >> 16) & 0xFF, aa = (a >> 24) & 0xFF;
    int rb = (b >> 0) & 0xFF, gb = (b >> 8) & 0xFF, bb = (b >> 16) & 0xFF, ab = (b >> 24) & 0xFF;
    return IM_COL32(
        (int)(ra + (rb - ra) * t),
        (int)(ga + (gb - ga) * t),
        (int)(ba + (bb - ba) * t),
        (int)(aa + (ab - aa) * t));
}

static std::vector<Keybind> s_keybinds;
static std::unordered_map<std::string, void*> s_varPtrs;
static std::unordered_map<std::string, std::string> s_displayLabels; // elementId → keybind list display label override

// Base value store — the user's manual value, updated only when no keybind is overriding
struct BaseValue {
    float f = 0; int i = 0; bool b = false;
    std::vector<bool> multi; // for MultiCombo
    bool initialized = false;
    bool wasOverridden = false; // were we overriding last frame?
};
static std::unordered_map<std::string, BaseValue> s_baseValues;
static bool s_contextMenuOpen = false;
static bool s_contextMenuPending = false;
static std::string s_contextElementId;
static std::string s_contextLabel;
static ElementType s_contextElemType = ElementType::Checkbox;
static float s_contextFloatMin = 0, s_contextFloatMax = 1;
static int s_contextIntMin = 0, s_contextIntMax = 100;
static int s_contextDecimalPlaces = 0;
static std::vector<std::string> s_contextComboItems;

// Config popup state
static bool s_configPopupOpen = false;
static bool s_configPopupPending = false;
static std::string s_configElementId;
static std::string s_configLabel;
static int s_configKey = 0;
static BindMode s_configMode = BindMode::Toggle;
static float s_configFloatVal = 0;
static int s_configIntVal = 0;
static bool s_configBoolVal = true;
static std::vector<bool> s_configMultiVal; // for MultiCombo
static bool s_listeningForKey = false;
static bool s_editingExisting = false;
static int s_editingIndex = -1; // index into s_keybinds when editing
static ElementType s_configElemType = ElementType::Checkbox;
static float s_configFloatMin = 0, s_configFloatMax = 1;
static int s_configIntMin = 0, s_configIntMax = 100;
static int s_configDecimalPlaces = 0;
static std::vector<std::string> s_configComboItems;

// Track previous key state for reliable edge detection
// Prev-state for the ACTIVE-BIND poller (ProcessKeybinds). Overwritten
// every frame with the whole 256-VK state, feeds "justPressed" edge
// detection for user-configured hotkeys.
static bool s_prevKeyState[256] = {};

// Separate prev-state for the CAPTURE poller (TickEdit / WaitingForKey).
// Kept independent because ProcessKeybinds' memcpy above used to trash
// this state every frame — so any key held when the user clicked "Bind
// key" (e.g. movement WASD) would already read prev=1 on the first
// capture-loop frame, killing the edge, and no key press ever registered.
// Only BeginKeyEdit / BeginStandaloneKeyEdit resets it (to zero) and only
// the capture loop writes to it.
static bool s_capturePrev[256] = {};
// Skip first frame of listening so stale key bits (from the click that
// opened the listen dialog itself) clear before edge detection kicks in.
static int s_listenSkipFrames = 0;

// ─── New inline-chip editing state ─────────────────────────────────────
// Only ONE bind can be in edit mode at a time. Everything else in the
// menu is disabled while phase != Idle so the user can freely change the
// editing target's value without accidentally triggering another widget.
static ChipPhase   s_editPhase   = ChipPhase::Idle;
static int         s_editBindIdx = -1;      // index into s_keybinds

// Snapshot of the target's value at BeginValueEdit — compared against
// the live value each TickEdit; when they differ, we capture the new
// value into the Keybind and advance to WaitingForKey.
static float       s_editSnapFloat = 0.0f;
static int         s_editSnapInt   = 0;
static bool        s_editSnapBool  = false;
static std::vector<bool> s_editSnapMulti;

// A frame counter set on BeginValueEdit / BeginKeyEdit so the same click
// that opens the phase doesn't immediately count as "click outside → cancel".
static int         s_editEnterFrame = -1;
// A frame counter incremented every time the target widget consumes
// input this frame — set from IsEditingTarget checks so click-outside
// cancellation can tell "no editable widget saw the click".
static int         s_editTargetSawInputFrame = -1;

int CountBindsFor(const std::string& elementId) {
    int n = 0;
    for (auto& kb : s_keybinds) if (kb.elementId == elementId) ++n;
    return n;
}

int IndexOfBind(const std::string& elementId, int localBindIndex) {
    int seen = 0;
    for (int i = 0; i < (int)s_keybinds.size(); i++) {
        if (s_keybinds[i].elementId == elementId) {
            if (seen == localBindIndex) return i;
            ++seen;
        }
    }
    return -1;
}

// Internal: measure chip block width for a specific element, factoring in
// the widget's natural rightside margin. Returns 0 if the chip fits.
static float ChipBlockOverflowForImpl(const std::string& elementId,
                                      float naturalNonCheckboxMargin,
                                      ElementType* outElemType = nullptr) {
    if (s_keybinds.empty()) return 0.0f;
    const float chipGap = S(6.0f); // must match menu_builder chip loop
    float blockW = 0.0f;
    int count = 0;
    ElementType et = ElementType::Checkbox;
    for (int i = 0; i < (int)s_keybinds.size(); i++) {
        const Keybind& kb = s_keybinds[i];
        if (kb.elementId != elementId) continue;
        et = kb.elemType;
        bool editingKey = (s_editBindIdx == i && s_editPhase == ChipPhase::WaitingForKey);
        std::string inside;
        if (editingKey)      inside = "...";
        else if (kb.key > 0) inside = GetKeyName(kb.key);
        else                 inside = "-";
        char text[48];
        snprintf(text, sizeof(text), "[%s]", inside.c_str());
        // Match the chip render's 85%-of-body-text font size so overflow
        // math lines up with what actually appears on screen.
        ImFont* cf  = ImGui::GetFont();
        float   cfs = ImGui::GetFontSize() * 0.85f;
        float w = cf->CalcTextSizeA(cfs, FLT_MAX, 0.0f, text).x;
        blockW = (count == 0) ? w : (blockW + chipGap + w);
        count++;
    }
    if (count == 0) return 0.0f;
    blockW += chipGap; // leading gap between widget's frame and first chip
    if (outElemType) *outElemType = et;

    // Every widget type gets `naturalNonCheckboxMargin` of "free" chip space
    // for menu-growth purposes — even checkboxes, whose row has whitespace
    // to the right of the label where a chip fits visually. Click-bleed
    // prevention for checkbox is handled separately in menu_builder via
    // reserveRight (the checkbox's InvisibleButton is shrunk by the full
    // chip block width regardless).
    return (std::max)(0.0f, blockW - naturalNonCheckboxMargin);
}

float MaxChipBlockOverflow(float naturalNonCheckboxMargin) {
    if (s_keybinds.empty()) return 0.0f;
    std::unordered_map<std::string, char> seen;
    float maxOv = 0.0f;
    for (auto& kb : s_keybinds) {
        if (seen.count(kb.elementId)) continue;
        seen[kb.elementId] = 1;
        float ov = ChipBlockOverflowForImpl(kb.elementId, naturalNonCheckboxMargin);
        if (ov > maxOv) maxOv = ov;
    }
    return maxOv;
}

float ChipBlockOverflowFor(const std::string& elementId,
                           float naturalNonCheckboxMargin) {
    return ChipBlockOverflowForImpl(elementId, naturalNonCheckboxMargin);
}

float ChipBlockWidthFor(const std::string& elementId) {
    if (s_keybinds.empty()) return 0.0f;
    const float chipGap = S(6.0f);
    float blockW = 0.0f;
    int count = 0;
    for (int i = 0; i < (int)s_keybinds.size(); i++) {
        const Keybind& kb = s_keybinds[i];
        if (kb.elementId != elementId) continue;
        bool editingKey = (s_editBindIdx == i && s_editPhase == ChipPhase::WaitingForKey);
        std::string inside;
        if (editingKey)      inside = "...";
        else if (kb.key > 0) inside = GetKeyName(kb.key);
        else                 inside = "-";
        char text[48];
        snprintf(text, sizeof(text), "[%s]", inside.c_str());
        // Match the chip render's 85%-of-body-text font size so overflow
        // math lines up with what actually appears on screen.
        ImFont* cf  = ImGui::GetFont();
        float   cfs = ImGui::GetFontSize() * 0.85f;
        float w = cf->CalcTextSizeA(cfs, FLT_MAX, 0.0f, text).x;
        blockW = (count == 0) ? w : (blockW + chipGap + w);
        count++;
    }
    if (count > 0) blockW += chipGap; // leading gap
    return blockW;
}

static void SnapshotTargetValue(const Keybind& kb) {
    s_editSnapFloat = 0.0f;
    s_editSnapInt   = 0;
    s_editSnapBool  = false;
    s_editSnapMulti.clear();
    if (!kb.varPtr) return;
    switch (kb.elemType) {
    case ElementType::Checkbox:
        s_editSnapBool = *(bool*)kb.varPtr;
        break;
    case ElementType::SliderFloat:
        s_editSnapFloat = *(float*)kb.varPtr;
        break;
    case ElementType::SliderInt:
    case ElementType::Combo:
        s_editSnapInt = *(int*)kb.varPtr;
        break;
    case ElementType::MultiCombo: {
        bool* base = (bool*)kb.varPtr;
        s_editSnapMulti.assign(base, base + kb.comboItems.size());
        break;
    }
    }
}

static bool CaptureIfChanged(Keybind& kb) {
    if (!kb.varPtr) return false;
    switch (kb.elemType) {
    case ElementType::Checkbox: {
        bool cur = *(bool*)kb.varPtr;
        if (cur != s_editSnapBool) { kb.boolValue = cur; return true; }
        return false;
    }
    case ElementType::SliderFloat: {
        float cur = *(float*)kb.varPtr;
        if (cur != s_editSnapFloat) { kb.floatValue = cur; return true; }
        return false;
    }
    case ElementType::SliderInt:
    case ElementType::Combo: {
        int cur = *(int*)kb.varPtr;
        if (cur != s_editSnapInt) { kb.intValue = cur; return true; }
        return false;
    }
    case ElementType::MultiCombo: {
        bool* base = (bool*)kb.varPtr;
        for (int i = 0; i < (int)s_editSnapMulti.size(); i++) {
            if (base[i] != s_editSnapMulti[i]) {
                kb.multiValues.assign(base, base + s_editSnapMulti.size());
                return true;
            }
        }
        return false;
    }
    }
    return false;
}

void BeginValueEdit(int keybindIdx) {
    if (keybindIdx < 0 || keybindIdx >= (int)s_keybinds.size()) return;
    s_editPhase   = ChipPhase::WaitingForValue;
    s_editBindIdx = keybindIdx;
    s_editEnterFrame = ImGui::GetFrameCount();
    SnapshotTargetValue(s_keybinds[keybindIdx]);
}

void BeginKeyEdit(int keybindIdx) {
    if (keybindIdx < 0 || keybindIdx >= (int)s_keybinds.size()) return;
    s_editPhase   = ChipPhase::WaitingForKey;
    s_editBindIdx = keybindIdx;
    s_editEnterFrame = ImGui::GetFrameCount();
    s_listenSkipFrames = 2;
    memset(s_capturePrev, 0, sizeof(s_capturePrev));
}

// Standalone key-listen target — set by BeginStandaloneKeyEdit. When
// non-null, TickEdit writes the captured VK to *s_standaloneTarget
// instead of a Keybind entry, and s_editBindIdx stays -1.
static int* s_standaloneTarget = nullptr;

void BeginStandaloneKeyEdit(int* targetVk) {
    if (!targetVk) return;
    s_editPhase        = ChipPhase::WaitingForKey;
    s_editBindIdx      = -1;
    s_standaloneTarget = targetVk;
    s_editEnterFrame   = ImGui::GetFrameCount();
    s_listenSkipFrames = 2;
    memset(s_capturePrev, 0, sizeof(s_capturePrev));
}

bool IsListeningForStandalone(int* targetVk) {
    return s_editPhase == ChipPhase::WaitingForKey &&
           s_standaloneTarget == targetVk && targetVk != nullptr;
}

void SetBindMode(int keybindIdx, BindMode mode) {
    if (keybindIdx < 0 || keybindIdx >= (int)s_keybinds.size()) return;
    s_keybinds[keybindIdx].mode = mode;
}

void CancelEdit() {
    s_editPhase   = ChipPhase::Idle;
    s_editBindIdx = -1;
    s_standaloneTarget = nullptr;
}

bool IsAnyEditing() { return s_editPhase != ChipPhase::Idle; }

bool IsEditingTarget(const std::string& elementId) {
    if (s_editPhase == ChipPhase::Idle) return false;
    if (s_editBindIdx < 0 || s_editBindIdx >= (int)s_keybinds.size()) return false;
    if (s_keybinds[s_editBindIdx].elementId != elementId) return false;
    // Only the value-edit phase leaves the target interactive — key-listen
    // freezes the whole UI (except for the chip / Escape).
    return s_editPhase == ChipPhase::WaitingForValue;
}

bool IsElementDisabledForEdit(const std::string& elementId) {
    if (s_editPhase == ChipPhase::Idle) return false;
    if (s_editPhase == ChipPhase::WaitingForKey) return true; // everything locked
    // WaitingForValue → everything except the target is disabled
    if (s_editBindIdx < 0 || s_editBindIdx >= (int)s_keybinds.size()) return false;
    return s_keybinds[s_editBindIdx].elementId != elementId;
}

ChipPhase GetEditPhase()        { return s_editPhase; }
int       GetEditingBindIndex() { return s_editBindIdx; }

// Spotlight target rect — captured this frame by the target widget's
// renderer, consumed at end-of-frame by DrawSpotlightOverlay. Frame-scoped
// so a stale value from a previous edit target doesn't linger.
static ImVec2 s_editTargetRectMin = ImVec2(0, 0);
static ImVec2 s_editTargetRectMax = ImVec2(0, 0);
static int    s_editTargetRectFrame = -1;

void SetEditTargetRect(ImVec2 rmin, ImVec2 rmax) {
    s_editTargetRectMin = rmin;
    s_editTargetRectMax = rmax;
    s_editTargetRectFrame = ImGui::GetFrameCount();
}

bool GetEditTargetRect(ImVec2* rmin, ImVec2* rmax) {
    if (s_editTargetRectFrame != ImGui::GetFrameCount()) return false;
    if (rmin) *rmin = s_editTargetRectMin;
    if (rmax) *rmax = s_editTargetRectMax;
    return true;
}

void DrawSpotlightOverlay(ImDrawList* dl, ImVec2 contentMin, ImVec2 contentMax) {
    if (s_editPhase == ChipPhase::Idle) return;

    // Ink at ~50% alpha for the dim; tuned so text is still faintly readable
    // through the veil so the user can see WHAT they're not interacting with.
    ImU32 dim = MenuStyle::Colors::ShadowInk.alpha(0.50f).u32();

    ImVec2 tmin, tmax;
    // WaitingForKey freezes everything, including the target — dim the full
    // area. WaitingForValue leaves the target lit if we captured its rect.
    bool haveHole = (s_editPhase == ChipPhase::WaitingForValue) &&
                    GetEditTargetRect(&tmin, &tmax);
    if (!haveHole) {
        dl->AddRectFilled(contentMin, contentMax, dim);
        return;
    }

    // Clamp hole to content bounds so the outer rects always make sense.
    if (tmin.x < contentMin.x) tmin.x = contentMin.x;
    if (tmin.y < contentMin.y) tmin.y = contentMin.y;
    if (tmax.x > contentMax.x) tmax.x = contentMax.x;
    if (tmax.y > contentMax.y) tmax.y = contentMax.y;

    // Nudge the hole outwards a couple of pixels so the "spotlight" reads
    // as breathing room around the widget rather than a tight cutout.
    const float pad = S(4.0f);
    tmin.x -= pad; tmin.y -= pad;
    tmax.x += pad; tmax.y += pad;
    if (tmin.x < contentMin.x) tmin.x = contentMin.x;
    if (tmin.y < contentMin.y) tmin.y = contentMin.y;
    if (tmax.x > contentMax.x) tmax.x = contentMax.x;
    if (tmax.y > contentMax.y) tmax.y = contentMax.y;

    // Four tile rects: top strip, bottom strip, left of hole, right of hole.
    // Top strip:
    dl->AddRectFilled(contentMin, ImVec2(contentMax.x, tmin.y), dim);
    // Bottom strip:
    dl->AddRectFilled(ImVec2(contentMin.x, tmax.y), contentMax, dim);
    // Left column (between top & bottom strips):
    dl->AddRectFilled(ImVec2(contentMin.x, tmin.y), ImVec2(tmin.x, tmax.y), dim);
    // Right column:
    dl->AddRectFilled(ImVec2(tmax.x, tmin.y), ImVec2(contentMax.x, tmax.y), dim);
}

void TickEdit() {
    if (s_editPhase == ChipPhase::Idle) return;
    // A standalone key-listen has s_editBindIdx == -1; a bind-targeted
    // edit has a valid index. Bail only when neither is set.
    bool standalone = (s_standaloneTarget != nullptr);
    if (!standalone &&
        (s_editBindIdx < 0 || s_editBindIdx >= (int)s_keybinds.size())) {
        s_editPhase = ChipPhase::Idle;
        return;
    }

    // Escape always cancels.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        CancelEdit();
        return;
    }

    // Cancel on click outside — skip the frame we entered the phase on so
    // the click that entered doesn't immediately cancel.
    if (ImGui::GetFrameCount() > s_editEnterFrame) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            if (s_editPhase == ChipPhase::WaitingForKey) {
                // Key phase — a click anywhere cancels. Even on the target
                // widget (we're not accepting value changes here).
                CancelEdit();
                return;
            }
            // Value phase — if the target widget didn't capture the click,
            // cancel. IsAnyItemActive/Hovered would be true if a widget
            // consumed it.
            if (!ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive()) {
                CancelEdit();
                return;
            }
        }
    }

    if (s_editPhase == ChipPhase::WaitingForValue) {
        // Value-edit is only meaningful for a real Keybind entry.
        if (standalone) { CancelEdit(); return; }
        Keybind& kb = s_keybinds[s_editBindIdx];
        if (CaptureIfChanged(kb)) {
            // Captured — advance to key listen. Reset capture-prev so the
            // click that caused the value change doesn't leak in.
            s_editPhase = ChipPhase::WaitingForKey;
            s_editEnterFrame = ImGui::GetFrameCount();
            s_listenSkipFrames = 2;
            memset(s_capturePrev, 0, sizeof(s_capturePrev));
        }
        return;
    }

    if (s_editPhase == ChipPhase::WaitingForKey) {
        if (s_listenSkipFrames > 0) { s_listenSkipFrames--; return; }
        // Capture uses its OWN prev-state (s_capturePrev), NOT the
        // ProcessKeybinds-owned s_prevKeyState. Sharing them was the
        // long-standing bug — ProcessKeybinds memcpys the whole
        // curKeyState into s_prevKeyState every frame for its
        // "justPressed" edge detection on active binds, which happens
        // BEFORE TickEdit runs. Any key held when the user clicked
        // "Bind key" (movement WASD in-game, most commonly) would
        // already read prev=1 by the time capture polled, so the edge
        // never fired and no keypress ever registered. Two arrays now,
        // one per subsystem — reset only in BeginKeyEdit / Begin-
        // StandaloneKeyEdit, no cross-contamination.
        for (int vk = 0x08; vk <= 0xFE; vk++) {
            bool down = Input::IsHotkeyDown(vk);
            bool prev = s_capturePrev[vk];
            s_capturePrev[vk] = down;
            if (down && !prev) {
                if (standalone) *s_standaloneTarget = vk;
                else            s_keybinds[s_editBindIdx].key = vk;
                CancelEdit();
                return;
            }
        }
        return;
    }
}

void Init() {
    s_keybinds.clear();
    s_varPtrs.clear();
    s_baseValues.clear();
    memset(s_prevKeyState, 0, sizeof(s_prevKeyState));
}

void OnConfigApplied() {
    // Don't blow away the whole table — just flag every entry as needing
    // recapture. Next ProcessKeybinds tick will sync bases from the live
    // variables (which now hold the freshly-loaded config values).
    for (auto& kv : s_baseValues) {
        kv.second.wasOverridden = false;
        kv.second.initialized   = false;
    }
}

void RegisterVar(const std::string& elementId, void* ptr) {
    s_varPtrs[elementId] = ptr;
}

void SetDisplayLabel(const std::string& elementId, const std::string& displayName) {
    if (displayName.empty())
        s_displayLabels.erase(elementId);
    else
        s_displayLabels[elementId] = displayName;
}

const std::string& GetDisplayLabel(const Keybind& kb) {
    auto it = s_displayLabels.find(kb.elementId);
    if (it != s_displayLabels.end() && !it->second.empty())
        return it->second;
    return kb.label;
}

void ProcessKeybinds() {
    if (s_listeningForKey) return;

    // Keystates already refreshed by Input::Update() at frame start.
    // Hotkey polling reads the HV-backed mirror (Input::IsHotkeyDown)
    // — never the general Win32/HV blend that ImGui uses — so the
    // runtime cheat's stealth invariant holds (see input.h).
    bool curKeyState[256];
    for (int i = 0; i < 256; i++)
        curKeyState[i] = Input::IsHotkeyDown(i);

    // Phase 1: Update active states for all keybinds
    for (auto& kb : s_keybinds) {
        if (kb.key == 0 || kb.key >= 256) continue;
        bool down = curKeyState[kb.key];
        bool justPressed = down && !s_prevKeyState[kb.key];

        switch (kb.mode) {
        case BindMode::Toggle: if (justPressed) kb.active = !kb.active; break;
        case BindMode::Hold:   kb.active = down;  break;
        case BindMode::OffKey: kb.active = !down; break; // inverse of Hold — value applied EXCEPT while key is held
        }
    }

    // Phase 2: For each element, capture base value or apply override
    // Collect unique element IDs that have keybinds
    std::unordered_map<std::string, bool> processed;
    for (auto& kb : s_keybinds) {
        if (processed.count(kb.elementId)) continue;
        processed[kb.elementId] = true;

        auto varIt = s_varPtrs.find(kb.elementId);
        if (varIt == s_varPtrs.end() || !varIt->second) continue;
        void* ptr = varIt->second;

        // Checkbox binds are runtime overrides, not configuration writes.
        // Keeping the configured bool untouched prevents a parent checkbox
        // from visually switching off and collapsing its visibleWhen children
        // when a bind becomes active. Runtime feature code resolves the bind
        // through GetEffectiveCheckboxValue().
        if (kb.elemType == ElementType::Checkbox) {
            BaseValue& base = s_baseValues[kb.elementId];
            base.b = *(bool*)ptr;
            base.initialized = true;
            base.wasOverridden = false;
            continue;
        }

        // Check if any keybind for this element is active
        const Keybind* activeKb = nullptr;
        bool anyActive = false;
        for (auto& k : s_keybinds) {
            if (k.elementId == kb.elementId && k.active) {
                activeKb = &k; // last active wins
                anyActive = true;
            }
        }

        BaseValue& base = s_baseValues[kb.elementId];

        if (!anyActive) {
            if (base.wasOverridden) {
                // Just deactivated. For *checkboxes*, applying the inverse of
                // the bind's target value gives the natural two-state toggle
                // semantic — bind active = boolValue, bind inactive =
                // !boolValue. Restoring the base instead caused a "stuck on"
                // bug when the user had manually toggled the checkbox on
                // before binding (base captured as on, deactivation restored
                // to on, bind never seemed to turn the feature off).
                //
                // Sliders / combos / multicombos keep the base-restore
                // semantic — those have a meaningful manual value the user
                // expects back after the bind releases.
                if (base.initialized) {
                    switch (kb.elemType) {
                    case ElementType::Checkbox:    *(bool*)ptr  = !kb.boolValue; break;
                    case ElementType::SliderFloat: *(float*)ptr = base.f; break;
                    case ElementType::SliderInt:
                    case ElementType::Combo:       *(int*)ptr   = base.i; break;
                    case ElementType::MultiCombo: {
                        bool* dst = (bool*)ptr;
                        for (int i = 0; i < (int)base.multi.size(); i++) dst[i] = base.multi[i];
                        break;
                    }
                    }
                }
                base.wasOverridden = false;
            } else {
                // No keybind active, not just deactivated — sync base from live variable
                // (captures manual UI changes)
                switch (kb.elemType) {
                case ElementType::Checkbox:    base.b = *(bool*)ptr;  break;
                case ElementType::SliderFloat: base.f = *(float*)ptr; break;
                case ElementType::SliderInt:
                case ElementType::Combo:       base.i = *(int*)ptr;   break;
                case ElementType::MultiCombo: {
                    bool* src = (bool*)ptr;
                    int n = (int)kb.comboItems.size();
                    base.multi.resize(n);
                    for (int i = 0; i < n; i++) base.multi[i] = src[i];
                    break;
                }
                }
                base.initialized = true;
            }
        } else {
            // Capture base value before first override
            if (!base.wasOverridden && base.initialized) {
                // Was not overriding last frame — base is already synced
            } else if (!base.initialized) {
                // First time — capture current value as base
                switch (kb.elemType) {
                case ElementType::Checkbox:    base.b = *(bool*)ptr;  break;
                case ElementType::SliderFloat: base.f = *(float*)ptr; break;
                case ElementType::SliderInt:
                case ElementType::Combo:       base.i = *(int*)ptr;   break;
                case ElementType::MultiCombo: {
                    bool* src = (bool*)ptr;
                    int n = (int)kb.comboItems.size();
                    base.multi.resize(n);
                    for (int i = 0; i < n; i++) base.multi[i] = src[i];
                    break;
                }
                }
                base.initialized = true;
            }

            // Apply the last active keybind's value
            switch (activeKb->elemType) {
            case ElementType::Checkbox:    *(bool*)ptr  = activeKb->boolValue;  break;
            case ElementType::SliderFloat: *(float*)ptr = activeKb->floatValue; break;
            case ElementType::SliderInt:
            case ElementType::Combo:       *(int*)ptr   = activeKb->intValue;   break;
            case ElementType::MultiCombo: {
                bool* dst = (bool*)ptr;
                int n = (int)activeKb->multiValues.size();
                for (int i = 0; i < n; i++) dst[i] = activeKb->multiValues[i];
                break;
            }
            }
            base.wasOverridden = true;
        }
    }

    memcpy(s_prevKeyState, curKeyState, sizeof(s_prevKeyState));
}

void AddKeybind(const std::string& elementId, const std::string& label,
                ElementType elemType, float fMin, float fMax,
                int iMin, int iMax, int decimalPlaces,
                const std::vector<std::string>& comboItems) {
    // Cap at kMaxBindsPerElement (2) per element — matches the inline-chip
    // UX which reserves room for exactly two chips per widget row.
    if (CountBindsFor(elementId) >= kMaxBindsPerElement) return;

    Keybind kb;
    kb.elementId = elementId;
    kb.label = label;
    kb.elemType = elemType;
    kb.floatMin = fMin;
    kb.floatMax = fMax;
    kb.intMin = iMin;
    kb.intMax = iMax;
    kb.decimalPlaces = decimalPlaces;
    kb.comboItems = comboItems;
    if (elemType == ElementType::MultiCombo)
        kb.multiValues.assign(comboItems.size(), false);
    s_keybinds.push_back(kb);
}

void RemoveKeybindAt(int index) {
    if (index >= 0 && index < (int)s_keybinds.size())
        s_keybinds.erase(s_keybinds.begin() + index);
}

void RemoveAllKeybinds(const std::string& elementId) {
    s_keybinds.erase(
        std::remove_if(s_keybinds.begin(), s_keybinds.end(),
            [&](const Keybind& kb) { return kb.elementId == elementId; }),
        s_keybinds.end());
}

Keybind* GetKeybind(const std::string& elementId) {
    for (auto& kb : s_keybinds) {
        if (kb.elementId == elementId) return &kb;
    }
    return nullptr;
}

Keybind* GetKeybindAt(int index) {
    if (index < 0 || index >= (int)s_keybinds.size()) return nullptr;
    return &s_keybinds[index];
}

bool IsAnyActive(const std::string& elementId) {
    for (auto& kb : s_keybinds) {
        if (kb.elementId == elementId && kb.active) return true;
    }
    return false;
}

bool HasBoundKey(const std::string& elementId) {
    for (auto& kb : s_keybinds) {
        if (kb.elementId == elementId && kb.key != 0) return true;
    }
    return false;
}

bool GetEffectiveCheckboxValue(const std::string& elementId, bool configuredValue) {
    const Keybind* activeKb = nullptr;
    for (const auto& kb : s_keybinds) {
        if (kb.elementId == elementId &&
            kb.elemType == ElementType::Checkbox && kb.active) {
            activeKb = &kb; // same last-active-wins rule as other elements
        }
    }
    return activeKb ? activeKb->boolValue : configuredValue;
}

const std::vector<Keybind>& GetAllKeybinds() {
    return s_keybinds;
}

void ReplaceAll(const std::vector<Keybind>& kbs) {
    // Keep registered variable pointers and base values intact — they're keyed
    // on elementId and remain valid across config loads. The keybinds vector
    // itself is fully swapped.
    s_keybinds = kbs;
    // varPtr in the incoming keybinds is from whatever serializer wrote them
    // (may be null after deserialization). Re-bind from the registry so newly
    // loaded binds apply to live variables on the next ProcessKeybinds tick.
    for (auto& kb : s_keybinds) {
        auto it = s_varPtrs.find(kb.elementId);
        kb.varPtr = (it != s_varPtrs.end()) ? it->second : nullptr;
        kb.active = false; // any prior 'held' state from the source is bogus
    }
}

static const char* BindModeName(BindMode m) {
    switch (m) {
    case BindMode::Toggle: return "toggle";
    case BindMode::Hold:   return "hold";
    case BindMode::OffKey: return "off key";
    }
    return "toggle";
}

// Format the value a keybind applies as a display string
static const char* FormatKeybindValue(const Keybind& kb, char* buf, int bufSize) {
    switch (kb.elemType) {
    case ElementType::Checkbox:
        return kb.boolValue ? "on" : "off";
    case ElementType::SliderFloat: {
        char fmt[16];
        snprintf(fmt, sizeof(fmt), "%%.%df", kb.decimalPlaces);
        snprintf(buf, bufSize, fmt, kb.floatValue);
        return buf;
    }
    case ElementType::SliderInt:
        snprintf(buf, bufSize, "%d", kb.intValue);
        return buf;
    case ElementType::Combo:
        if (kb.intValue >= 0 && kb.intValue < (int)kb.comboItems.size())
            return kb.comboItems[kb.intValue].c_str();
        snprintf(buf, bufSize, "%d", kb.intValue);
        return buf;
    case ElementType::MultiCombo: {
        // Show count of selected items: e.g. "2/3"
        int sel = 0;
        for (bool v : kb.multiValues) if (v) sel++;
        snprintf(buf, bufSize, "%d/%d", sel, (int)kb.multiValues.size());
        return buf;
    }
    }
    return "";
}

void OpenContextMenu(const char* elementId, const char* label,
                     ElementType elemType, float fMin, float fMax,
                     int iMin, int iMax, int decimalPlaces,
                     const std::vector<std::string>& comboItems) {
    s_contextElementId = elementId;
    s_contextLabel = label;
    s_contextElemType = elemType;
    s_contextFloatMin = fMin;
    s_contextFloatMax = fMax;
    s_contextIntMin = iMin;
    s_contextIntMax = iMax;
    s_contextDecimalPlaces = decimalPlaces;
    s_contextComboItems = comboItems;
    s_contextMenuPending = true; // defer OpenPopup to RenderContextMenu's scope
}

static void OpenConfigPopup(const std::string& elementId, const std::string& label, bool editing,
                            ElementType elemType, float fMin, float fMax,
                            int iMin, int iMax, int decimalPlaces,
                            const std::vector<std::string>& comboItems,
                            int editIndex = -1) {
    s_configElementId = elementId;
    s_configLabel = label;
    s_editingExisting = editing;
    s_editingIndex = editIndex;
    s_configElemType = elemType;
    s_configFloatMin = fMin;
    s_configFloatMax = fMax;
    s_configIntMin = iMin;
    s_configIntMax = iMax;
    s_configDecimalPlaces = decimalPlaces;
    s_configComboItems = comboItems;

    if (editing && editIndex >= 0 && editIndex < (int)s_keybinds.size()) {
        Keybind& kb = s_keybinds[editIndex];
        s_configKey = kb.key;
        s_configMode = kb.mode;
        s_configFloatVal = kb.floatValue;
        s_configIntVal = kb.intValue;
        s_configBoolVal = kb.boolValue;
        s_configMultiVal = kb.multiValues;
        s_configElemType = kb.elemType;
        s_configFloatMin = kb.floatMin;
        s_configFloatMax = kb.floatMax;
        s_configIntMin = kb.intMin;
        s_configIntMax = kb.intMax;
        s_configDecimalPlaces = kb.decimalPlaces;
        s_configComboItems = kb.comboItems;
        if (s_configElemType == ElementType::MultiCombo)
            s_configMultiVal.resize(s_configComboItems.size(), false);
    } else {
        s_configKey = 0;
        s_configMode = BindMode::Toggle;
        s_configFloatVal = fMin;
        s_configIntVal = iMin;
        s_configBoolVal = true;
        if (elemType == ElementType::MultiCombo)
            s_configMultiVal.assign(comboItems.size(), false);
        else
            s_configMultiVal.clear();
    }
    s_listeningForKey = false;
    s_configPopupOpen = true;
    s_configPopupPending = true;
}

// Forward declaration
static bool UIComboFromVec(const char* label, int* current, const std::vector<std::string>& items);

// Animated hover state for context menu items
static std::unordered_map<int, float> s_ctxHoverAnim;
static float s_ctxFadeAnim = 0.0f;
static ImVec2 s_ctxMenuPos = ImVec2(0, 0);
static float s_ctxMenuW = 200.0f;
static float s_ctxTargetW = 140.0f;
static float s_ctxAnimW = 140.0f;
static int   s_ctxEditIdx = -1;   // keybind index being edited inline, -1 = none
static float s_ctxCfgFade = 0.0f;

// Apply fade alpha to a color
static ImU32 FadeColor(ImU32 col, float fade) {
    int a = (int)(((col >> 24) & 0xFF) * fade);
    return (col & 0x00FFFFFF) | ((ImU32)a << 24);
}

// New RenderContextMenu — right-click on a widget opens THIS tiny popup.
// The only action is "add bind" which appends a fresh Keybind for the
// element and immediately drops the user into WaitingForValue on the new
// chip. Everything else (change key/value/mode/delete) lives on the chip's
// own right-click menu.
void RenderContextMenu() {
    if (s_contextMenuPending) {
        ImGui::OpenPopup("##keybind_ctx");
        s_contextMenuPending = false;
    }

    // Custom-drawn popup — matches the combo dropdown style (see
    // ui_components.cpp Combo() dropdown): transparent ImGui shell, then
    // hand-drawn WidgetBase fill + WidgetOutline (Ink) border + custom
    // rows with InvisibleButton hit-test and Accent hover.
    using MenuStyle::Colors::WidgetBase;
    using MenuStyle::Colors::WidgetOutline;
    using MenuStyle::Colors::TextBright;
    using MenuStyle::Colors::TextMuted;
    using MenuStyle::Colors::Accent;
    using MenuStyle::Colors::ShadowInk;

    int nBinds = CountBindsFor(s_contextElementId);
    bool full = nBinds >= kMaxBindsPerElement;

    // Rows: "add bind" always; "remove all binds" only when at least one
    // bind exists. A 1 px separator line sits between rows.
    struct Row {
        const char* label;
        bool        disabled;
        std::function<void()> onClick;
    };
    std::vector<Row> rows;
    rows.push_back({ "add bind", full, [&]() {
        AddKeybind(s_contextElementId, s_contextLabel, s_contextElemType,
                   s_contextFloatMin, s_contextFloatMax,
                   s_contextIntMin, s_contextIntMax,
                   s_contextDecimalPlaces, s_contextComboItems);
        int localIdx = CountBindsFor(s_contextElementId) - 1;
        int globalIdx = IndexOfBind(s_contextElementId, localIdx);
        if (globalIdx >= 0) BeginValueEdit(globalIdx);
    }});
    if (nBinds > 0) {
        rows.push_back({ "remove all binds", false, [&]() {
            RemoveAllKeybinds(s_contextElementId);
        }});
    }

    float lineH = ImGui::GetTextLineHeight();
    float itemH = lineH + S(8.0f);
    float popupW = S(140.0f);
    float popupH = itemH * (float)rows.size(); // no top/bottom padding, no separators

    // Transparent ImGui shell (draw our own fill/border below).
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,    0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize,  0.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,  ImVec4(0, 0, 0, 0));

    ImGui::SetNextWindowSizeConstraints(ImVec2(popupW, popupH), ImVec2(popupW, popupH));

    if (ImGui::BeginPopup("##keybind_ctx",
                          ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoSavedSettings)) {
        ImVec2 popMin = ImGui::GetWindowPos();
        ImVec2 popMax(popMin.x + popupW, popMin.y + popupH);
        ImDrawList* popDl = ImGui::GetWindowDrawList();

        // Widget-language top-down gradient (WidgetBase → MenuLight fade)
        // — same treatment every other widget frame uses, matches the
        // combo dropdown's popup surface.
        popDl->AddRectFilled(popMin, popMax, WidgetBase.u32());
        {
            ImU32 topOv = MenuStyle::Colors::MenuLight.alpha(0.0f).u32();
            ImU32 botOv = MenuStyle::Colors::MenuLight.alpha(160.0f / 255.0f).u32();
            popDl->AddRectFilledMultiColor(popMin, popMax,
                                            topOv, topOv, botOv, botOv);
        }

        ImGui::PushClipRect(popMin, popMax, true);

        // Custom-drawn rows — flush stacked, no separators, no top/bottom
        // padding. Rows span the full popup width (no side inset either)
        // so hover fills the whole row edge-to-edge.
        float curY = 0.0f;
        for (int i = 0; i < (int)rows.size(); i++) {
            const Row& row = rows[i];
            ImVec2 iMin(popMin.x, popMin.y + curY);
            ImVec2 iMax(popMax.x, popMin.y + curY + itemH);

            ImGui::SetCursorScreenPos(iMin);
            char itemId[32];
            snprintf(itemId, sizeof(itemId), "##kbctxr%d", i);
            ImGui::InvisibleButton(itemId, ImVec2(iMax.x - iMin.x, itemH));
            bool hovered = ImGui::IsItemHovered() && !row.disabled;
            bool clicked = ImGui::IsItemClicked() && !row.disabled;
            if (clicked) {
                row.onClick();
                ImGui::CloseCurrentPopup();
            }

            // Hover ink veil — same tint the combo dropdown uses on hover.
            if (hovered) {
                popDl->AddRectFilled(iMin, iMax,
                    ShadowInk.alpha(30 / 255.0f).u32());
            }

            ImU32 textCol = row.disabled
                ? TextMuted.u32()
                : (hovered
                    ? Accent.u32()
                    : TextBright.alpha(220 / 255.0f).u32());
            ImVec2 tp(iMin.x + S(8.0f),
                      iMin.y + (itemH - lineH) * 0.5f);
            popDl->AddText(tp, textCol, row.label);

            curY += itemH;
        }

        ImGui::PopClipRect();

        // Border drawn to the foreground so it sits on top of the fill
        // (same trick the combo dropdown uses).
        auto* fgDl = ImGui::GetForegroundDrawList();
        fgDl->AddRect(popMin, popMax, WidgetOutline.u32(), 0.0f, 0, 1.0f);

        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(5);
}

const char* GetKeyName(int vk) {
    static char buf[32];
    if (vk == 0) return "None";

    // Common keys
    switch (vk) {
    case VK_LBUTTON: return "M1";
    case VK_RBUTTON: return "M2";
    case VK_MBUTTON: return "M3";
    case VK_XBUTTON1: return "M4";
    case VK_XBUTTON2: return "M5";
    case VK_BACK: return "Backspace";
    case VK_TAB: return "Tab";
    case VK_RETURN: return "Enter";
    case VK_SHIFT: return "Shift";
    case VK_CONTROL: return "Ctrl";
    case VK_MENU: return "Alt";
    case VK_CAPITAL: return "Caps";
    case VK_ESCAPE: return "Esc";
    case VK_SPACE: return "Space";
    case VK_INSERT: return "Ins";
    case VK_DELETE: return "Del";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "PgUp";
    case VK_NEXT: return "PgDn";
    case VK_LEFT: return "Left";
    case VK_RIGHT: return "Right";
    case VK_UP: return "Up";
    case VK_DOWN: return "Down";
    case VK_F1: return "F1"; case VK_F2: return "F2"; case VK_F3: return "F3";
    case VK_F4: return "F4"; case VK_F5: return "F5"; case VK_F6: return "F6";
    case VK_F7: return "F7"; case VK_F8: return "F8"; case VK_F9: return "F9";
    case VK_F10: return "F10"; case VK_F11: return "F11"; case VK_F12: return "F12";
    case VK_NUMPAD0: return "Num 0"; case VK_NUMPAD1: return "Num 1";
    case VK_NUMPAD2: return "Num 2"; case VK_NUMPAD3: return "Num 3";
    case VK_NUMPAD4: return "Num 4"; case VK_NUMPAD5: return "Num 5";
    case VK_NUMPAD6: return "Num 6"; case VK_NUMPAD7: return "Num 7";
    case VK_NUMPAD8: return "Num 8"; case VK_NUMPAD9: return "Num 9";
    case VK_MULTIPLY: return "Num *"; case VK_ADD: return "Num +";
    case VK_SUBTRACT: return "Num -"; case VK_DECIMAL: return "Num .";
    case VK_DIVIDE: return "Num /";
    case VK_NUMLOCK: return "NumLock"; case VK_SCROLL: return "ScrLock";
    case VK_SNAPSHOT: return "PrtSc"; case VK_PAUSE: return "Pause";
    case VK_LSHIFT: return "LShift"; case VK_RSHIFT: return "RShift";
    case VK_LCONTROL: return "LCtrl"; case VK_RCONTROL: return "RCtrl";
    case VK_LMENU: return "LAlt"; case VK_RMENU: return "RAlt";
    case VK_OEM_1: return ";";
    case VK_OEM_PLUS: return "=";
    case VK_OEM_COMMA: return ",";
    case VK_OEM_MINUS: return "-";
    case VK_OEM_PERIOD: return ".";
    case VK_OEM_2: return "/";
    case VK_OEM_3: return "`";
    case VK_OEM_4: return "[";
    case VK_OEM_5: return "\\";
    case VK_OEM_6: return "]";
    case VK_OEM_7: return "'";
    default:
        if (vk >= 'A' && vk <= 'Z') {
            buf[0] = (char)vk;
            buf[1] = 0;
            return buf;
        }
        if (vk >= '0' && vk <= '9') {
            buf[0] = (char)vk;
            buf[1] = 0;
            return buf;
        }
        snprintf(buf, sizeof(buf), "0x%02X", vk);
        return buf;
    }
}

// Helper: convert vector<string> to const char*[] for UI::Combo
static bool UIComboFromVec(const char* label, int* current, const std::vector<std::string>& items) {
    if (items.empty()) return false;
    std::vector<const char*> ptrs(items.size());
    for (int i = 0; i < (int)items.size(); i++) ptrs[i] = items[i].c_str();
    return UI::Combo(label, current, ptrs.data(), (int)ptrs.size());
}

static float s_cfgFadeAnim = 0.0f;

// Legacy floating config popup — replaced by the inline chip flow. Kept as
// a no-op stub so existing call sites still compile until they're swept.
void RenderKeybindConfigPopup() {
    (void)s_cfgFadeAnim;
    (void)s_configPopupOpen;
    (void)s_configPopupPending;
    (void)s_configElementId;
    (void)s_configLabel;
    (void)s_configKey;
    (void)s_configMode;
    (void)s_configFloatVal;
    (void)s_configIntVal;
    (void)s_configBoolVal;
    (void)s_configMultiVal;
    (void)s_listeningForKey;
    (void)s_editingExisting;
    (void)s_editingIndex;
    (void)s_configElemType;
    (void)s_configFloatMin;
    (void)s_configFloatMax;
    (void)s_configIntMin;
    (void)s_configIntMax;
    (void)s_configDecimalPlaces;
    (void)s_configComboItems;
}

void RenderKeybindList() {
    auto& binds = s_keybinds;

    if (binds.empty()) {
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.149f, 0.149f, 0.161f, 1.0f)); // = MenuStyle Border
    ImGui::BeginChild("##keybindlist", ImVec2(0, 0), ImGuiChildFlags_None);

    float lineH = ImGui::GetTextLineHeight();
    for (int i = 0; i < (int)binds.size(); i++) {
        auto& kb = binds[i];
        ImGui::PushID(i);

        float width = ImGui::GetContentRegionAvail().x;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float rowH = lineH * 2 + S(4.0f);

        ImGui::InvisibleButton("##row", ImVec2(width, rowH));
        bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        if (hovered) {
            dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + rowH),
                MenuStyle::Colors::White.alpha(10 / 255.0f).u32(), S(4.0f));
        }

        if (kb.active) {
            dl->AddRectFilled(pos, ImVec2(pos.x + S(3.0f), pos.y + rowH), MenuStyle::Colors::Accent.u32());
        }

        const std::string& displayLbl = GetDisplayLabel(kb);
        MenuStyle::TextShadow(dl, ImVec2(pos.x + S(8.0f), pos.y + S(2.0f)), MenuStyle::Colors::Text.u32(), displayLbl.c_str());

        // Key + mode + value
        char valBuf[32];
        const char* valStr = FormatKeybindValue(kb, valBuf, sizeof(valBuf));
        char info[128];
        snprintf(info, sizeof(info), "[%s] %s", GetKeyName(kb.key), BindModeName(kb.mode));
        MenuStyle::TextShadow(dl, ImVec2(pos.x + S(8.0f), pos.y + lineH + S(4.0f)), MenuStyle::Colors::TextMuted.u32(), info);

        ImVec2 valSize = ImGui::CalcTextSize(valStr);
        MenuStyle::TextShadow(dl, ImVec2(pos.x + width - valSize.x - S(8.0f), pos.y + lineH + S(4.0f)),
            MenuStyle::Colors::AccentDim().u32(), valStr);

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            OpenConfigPopup(kb.elementId, displayLbl, true,
                            kb.elemType, kb.floatMin, kb.floatMax,
                            kb.intMin, kb.intMax, kb.decimalPlaces,
                            kb.comboItems, i);
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            RemoveKeybindAt(i);
            ImGui::PopID();
            break;
        }

        dl->AddLine(ImVec2(pos.x, pos.y + rowH), ImVec2(pos.x + width, pos.y + rowH),
                    MenuStyle::Colors::Border.alpha(80 / 255.0f).u32());

        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(3);
}


void RenderKeybindListWindow(bool menuVisible) {
    // Lerp state — reset when the popup hides so each show plays its
    // expand animation cleanly.
    static float s_animHeight = 0.0f;
    static float s_targetHeight = 60.0f;

    // Empty (title-bar-only) height — baseline when no active binds.
    const float titleOnlyH = S(MenuStyle::Panel::TitleBarH)
                           + S(5.0f) + S(4.0f) + S(6.0f);

    // Collect the winning keybind per element (last active wins — matches
    // ProcessKeybinds' resolution order).
    std::vector<const Keybind*> active;
    {
        std::unordered_map<std::string, const Keybind*> winners;
        for (auto& kb : s_keybinds) {
            if (kb.active && kb.key != 0)
                winners[kb.elementId] = &kb;
        }
        for (auto& [id, kb] : winners) active.push_back(kb);
    }

    // Show when: menu open (so the user can reposition it) OR any bind is
    // active. Reset baseline when hidden so the next show animates from
    // the title bar rather than from 0.
    if (!menuVisible && active.empty()) {
        s_animHeight = titleOnlyH;
        s_targetHeight = titleOnlyH;
        return;
    }

    // Width auto-fits the widest row's (label + value) content, bounded
    // below by a minimum so the title bar / accent cap always look right.
    const float minWidth  = S(180.0f);
    const float sidePad   = S(8.0f);   // matches the S(8) padding used by rows / title
    const float rowGap    = S(12.0f);  // gap between label and value on same row
    ImVec2 titleSize = ImGui::CalcTextSize("keybinds");
    // Reserve room for the keyboard glyph on the right of the title bar.
    float titleReserve = titleSize.x + sidePad * 2.0f + S(20.0f);
    float widestRow = titleReserve;
    for (auto* kb : active) {
        const std::string& lbl = GetDisplayLabel(*kb);
        char valBuf[32];
        const char* valStr = FormatKeybindValue(*kb, valBuf, sizeof(valBuf));
        float lblW = ImGui::CalcTextSize(lbl.c_str()).x;
        float valW = ImGui::CalcTextSize(valStr).x;
        float rowW = sidePad * 2.0f + lblW + rowGap + valW;
        if (rowW > widestRow) widestRow = rowW;
    }
    const float width = (widestRow > minWidth) ? widestRow : minWidth;

    const float lineH = ImGui::GetTextLineHeight();
    const float rowH = lineH + S(6.0f);
    const float titleBarH = S(MenuStyle::Panel::TitleBarH);
    const float sepGap = S(5.0f);

    // Lerp toward target height
    float dt = ImGui::GetIO().DeltaTime;
    s_animHeight += (s_targetHeight - s_animHeight) * (std::min)(22.0f * dt, 1.0f);
    if (std::abs(s_targetHeight - s_animHeight) < 0.5f) s_animHeight = s_targetHeight;

    ImGui::SetNextWindowSize(ImVec2(width, s_animHeight), ImGuiCond_Always);

    // Persistent position via ConfigSystem
    static ConfigSystem::WindowState* s_winState =
        ConfigSystem::RegisterWindow("keybinds", false);
    if (s_winState && s_winState->pos.x >= 0.0f) {
        if (s_winState->pendingApply) {
            ImGui::SetNextWindowPos(s_winState->pos, ImGuiCond_Always);
            s_winState->pendingApply = false;
        } else {
            ImGui::SetNextWindowPos(s_winState->pos, ImGuiCond_FirstUseEver);
        }
    } else {
        ImGui::SetNextWindowPos(ImVec2(S(20.0f), S(20.0f)), ImGuiCond_FirstUseEver);
    }

    // Transparent window shell — we draw the whole card / title / border
    // ourselves using the shared Panel palette so the overlay reads as
    // the same design system as every other floating window.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(S(8.0f), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##keybind_overlay", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        if (s_winState) {
            s_winState->pos  = winPos;
            s_winState->size = ImGui::GetWindowSize();
        }

        // Card fills — split into two flat strips using palette tokens:
        //   Title bar (top titleBarH px) = MenuLight
        //   Body      (rest of card)     = MenuDark
        // Panel::Border outlines the whole card.
        ImVec2 bgMin = winPos;
        ImVec2 bgMax(winPos.x + width, winPos.y + s_animHeight);
        ImVec2 titleStripMax(bgMax.x, winPos.y + titleBarH);
        dl->AddRectFilled(bgMin,          titleStripMax,
                          MenuStyle::Colors::MenuLight.u32());
        dl->AddRectFilled(ImVec2(bgMin.x, titleStripMax.y), bgMax,
                          MenuStyle::Colors::MenuDark.u32());
        dl->AddRect(bgMin, bgMax,
                    MenuStyle::Panel::Border.u32(), 0.0f, 0, 1.0f);

        // Clip to window bounds so the row draw doesn't spill during lerp.
        dl->PushClipRect(winPos, bgMax, true);

        // No separate title-bar strip color — the title text sits directly
        // on the same body surface, matching how UI::BeginContainer draws
        // menu containers (single bg color, no second strip tone).
        float sepY = winPos.y + titleBarH;

        // Accent bar — 1 px tall inside the border, right under the 1 px
        // gray outline. Inset by 1 px on the left and 2 px on the right.
        // Left-to-right transparent→Accent→transparent gradient, matching
        // the main menu / debug windows / watermark.
        {
            const ImU32 accent = MenuStyle::Colors::Accent.u32();
            const ImU32 clear  = MenuStyle::Colors::Accent.alpha(0.0f).u32();
            float x0 = winPos.x + 1.0f;
            float x1 = winPos.x + width - 2.0f;
            float y0 = winPos.y + 1.0f;
            float y1 = y0 + 1.0f;
            dl->AddRectFilledMultiColor(
                ImVec2(x0, y0), ImVec2(x1, y1),
                accent, clear, clear, accent);
        }

        // Title text — "keybinds", vertically centered, bold.
        float fontSize = ImGui::GetFontSize();
        float titleY = winPos.y + (titleBarH - fontSize) * 0.5f;
        ImFont* titleFont = MenuStyle::g_fontBold ? MenuStyle::g_fontBold : ImGui::GetFont();
        MenuStyle::TextShadow(dl, titleFont, fontSize,
                              ImVec2(winPos.x + S(8.0f), titleY),
                              MenuStyle::Colors::Text.u32(), "keybinds");

        // Keyboard icon at the right end of the title bar row.
        {
            const char* kbIcon = "\xef\x84\x9c"; // FA keyboard f11c
            if (MenuStyle::g_fontIcon) {
                ImVec2 iconSize = MenuStyle::g_fontIcon->CalcTextSizeA(
                    S(14.0f), FLT_MAX, 0.0f, kbIcon);
                float iconX = winPos.x + width - S(8.0f) - iconSize.x;
                float iconY = winPos.y + (titleBarH - iconSize.y) * 0.5f;
                MenuStyle::TextShadow(dl, MenuStyle::g_fontIcon, S(14.0f),
                                       ImVec2(iconX, iconY),
                                       MenuStyle::Colors::TextMuted.u32(), kbIcon);
            }
        }

        // Rows — label on the left, formatted value on the right.
        float curY = sepY + sepGap + S(4.0f);
        for (auto* kb : active) {
            const std::string& lbl = GetDisplayLabel(*kb);
            MenuStyle::TextShadow(dl, ImVec2(winPos.x + S(8.0f), curY),
                                   MenuStyle::Colors::Text.u32(), lbl.c_str());

            char valBuf[32];
            const char* valStr = FormatKeybindValue(*kb, valBuf, sizeof(valBuf));
            ImVec2 valSize = ImGui::CalcTextSize(valStr);
            MenuStyle::TextShadow(dl,
                ImVec2(winPos.x + width - S(8.0f) - valSize.x, curY),
                MenuStyle::Colors::Accent.u32(), valStr);

            curY += rowH;
        }

        // Bottom padding + target-height sync
        curY += S(6.0f);
        s_targetHeight = curY - winPos.y;

        dl->PopClipRect();

        // Reserve content extent so the window is draggable at its full size.
        ImGui::SetCursorScreenPos(ImVec2(winPos.x, curY));
        ImGui::Dummy(ImVec2(0, 0));
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

bool IsListening() {
    return s_listeningForKey;
}

} // namespace KeybindSystem
