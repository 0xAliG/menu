#pragma once
#include "imgui.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace KeybindSystem {

enum class BindMode {
    Toggle,
    Hold,
    OffKey   // Always-on EXCEPT when key is held (inverse of Hold)
};

enum class ElementType {
    Checkbox,
    SliderFloat,
    SliderInt,
    Combo,
    MultiCombo
};

struct Keybind {
    std::string elementId;
    std::string label;
    int key = 0;           // Virtual key code (0 = unbound)
    BindMode mode = BindMode::Toggle;
    float floatValue = 0;  // Active value for sliders
    int intValue = 0;      // Active value for int sliders / combo index
    bool boolValue = true;  // Active value for checkboxes
    std::vector<bool> multiValues; // Active values for MultiCombo (one bool per item)
    bool active = false;    // Currently active (toggled on / held)

    // Element type metadata
    ElementType elemType = ElementType::Checkbox;
    float floatMin = 0, floatMax = 1;
    int intMin = 0, intMax = 100;
    int decimalPlaces = 0;
    std::vector<std::string> comboItems;

    // Live variable pointer (registered by widget on first render)
    void* varPtr = nullptr;
};

// Register a widget's variable pointer for direct keybind application
void RegisterVar(const std::string& elementId, void* ptr);

// Override the label shown in the keybind list / overlay for this element.
// Pass an empty string to clear the override (falls back to the widget label).
void SetDisplayLabel(const std::string& elementId, const std::string& displayName);

// Resolve the label to show for a given keybind (override if set, otherwise kb.label).
const std::string& GetDisplayLabel(const Keybind& kb);

// Initialize the keybind system
void Init();

// Process all keybinds each frame (check key states, apply values)
void ProcessKeybinds();

// Keybind management
void AddKeybind(const std::string& elementId, const std::string& label,
                ElementType elemType, float fMin = 0, float fMax = 1,
                int iMin = 0, int iMax = 100, int decimalPlaces = 0,
                const std::vector<std::string>& comboItems = {});
void RemoveKeybindAt(int index);
void RemoveAllKeybinds(const std::string& elementId);
Keybind* GetKeybind(const std::string& elementId); // first match
Keybind* GetKeybindAt(int index);                  // mutable accessor by global index
bool IsAnyActive(const std::string& elementId);    // any keybind active for element
bool HasBoundKey(const std::string& elementId);    // any keybind exists with key != 0
// Resolve a checkbox's runtime value without mutating its configured/menu
// value. The last active bind wins; with no active bind, configuredValue is
// returned. Feature code should use this while the menu continues to render
// and gate child options from the configured bool.
bool GetEffectiveCheckboxValue(const std::string& elementId, bool configuredValue);
const std::vector<Keybind>& GetAllKeybinds();

// Replace the entire keybind list. Used by ConfigSystem::Load — overwrites
// existing binds (does NOT merge). Variable pointers / base values are
// preserved through the existing RegisterVar map.
void ReplaceAll(const std::vector<Keybind>& kbs);

// Context menu (opened on right-click of a widget).
// New flow: this menu just offers "Add bind" (disabled if the element
// already has kMaxBindsPerElement binds). Everything else lives on the
// inline `[…]` chip's right-click menu (see ChipContext* below).
void OpenContextMenu(const char* elementId, const char* label,
                     ElementType elemType, float fMin = 0, float fMax = 1,
                     int iMin = 0, int iMax = 100, int decimalPlaces = 0,
                     const std::vector<std::string>& comboItems = {});
void RenderContextMenu();

// ─── New inline-chip editing flow ─────────────────────────────────────
//
// Each element gets up to kMaxBindsPerElement inline chips drawn to the
// right of its label. A chip goes through a small state machine:
//   Idle             — solid pill, shows `[K]` (or `[]` if unbound)
//   WaitingForValue  — every other menu element is disabled; the user
//                      changes THIS widget's value to set the "apply on
//                      key press" value. Any click outside the target
//                      widget cancels.
//   WaitingForKey    — chip shows `[...]`; the next non-Escape key press
//                      captures into kb.key.
//
// Only ONE bind can be in edit mode at a time (any phase).
constexpr int kMaxBindsPerElement = 2;

enum class ChipPhase {
    Idle,
    WaitingForValue,
    WaitingForKey,
};

int  CountBindsFor(const std::string& elementId);
int  IndexOfBind(const std::string& elementId, int localBindIndex); // global s_keybinds index or -1

// Compute how much EXTRA rightside gutter the widest chip block in the
// menu needs BEYOND each widget's natural right margin. For non-checkbox
// widgets there's already `naturalNonCheckboxMargin` px of empty space
// between the frame's right edge and the container's inner right edge;
// chips smaller than that fit in the existing margin and this returns 0.
// For checkbox binds the natural margin is 0 (their InvisibleButton spans
// full width). Returns the maximum excess across all elements.
float MaxChipBlockOverflow(float naturalNonCheckboxMargin);

// Overflow just for the binds on `elementId`. Used per-container to see
// if a specific container needs the extra rightside gutter.
float ChipBlockOverflowFor(const std::string& elementId,
                           float naturalNonCheckboxMargin);

// Raw chip block width for `elementId` (sum of chip widths + gaps),
// ignoring natural margin. Used to shrink checkbox click areas so a chip
// click doesn't fall through to toggle the checkbox.
float ChipBlockWidthFor(const std::string& elementId);

// Enter WaitingForValue for the given bind. Snapshots the target widget's
// current value so we can detect when the user changes it. If another
// bind was already in edit mode, cancel that one first.
void BeginValueEdit(int keybindIdx);

// Enter WaitingForKey directly (skip value edit — used by "Change key").
void BeginKeyEdit(int keybindIdx);

// Enter key-listen mode that writes the captured VK straight into an
// externally-owned int*. Used by standalone chips (e.g. the "menu key"
// toggle) that aren't part of the widget-level keybind list.
void BeginStandaloneKeyEdit(int* targetVk);

// True while a standalone key-listen is targeting this exact int*.
bool IsListeningForStandalone(int* targetVk);

// Cycle mode (or set a specific one).
void SetBindMode(int keybindIdx, BindMode mode);

// Cancel any in-progress edit, revert phase to Idle.
void CancelEdit();

// Called once per frame from the main render pass — handles value-change
// detection, key capture, and click-outside cancellation.
void TickEdit();

// Is any bind currently in edit mode?
bool IsAnyEditing();

// Is THIS element the current edit target (so it should stay interactive)?
bool IsEditingTarget(const std::string& elementId);

// Should THIS element render as disabled (any bind is editing AND this
// element isn't the target)?
bool IsElementDisabledForEdit(const std::string& elementId);

// Current phase + which bind is being edited (index into GetAllKeybinds()).
ChipPhase GetEditPhase();
int       GetEditingBindIndex();

// Spotlight overlay support — the editing target widget calls
// SetEditTargetRect(min, max) while rendering so the top-level render path
// knows where NOT to dim. DrawSpotlightOverlay draws four dark rects that
// tile the given content area with a hole around the target rect.
void SetEditTargetRect(ImVec2 rmin, ImVec2 rmax);
bool GetEditTargetRect(ImVec2* rmin, ImVec2* rmax);
void DrawSpotlightOverlay(ImDrawList* dl, ImVec2 contentMin, ImVec2 contentMax);

// ── legacy config popup — DEPRECATED ──────────────────────────────────
// The old floating config popup is being replaced by the inline chip
// flow above. RenderKeybindConfigPopup is a no-op stub kept only so
// callers still compile until the sweep completes; do NOT add new callers.
void RenderKeybindConfigPopup();

// Keybind list panel (embedded, shows all binds, click to edit, right-click to delete)
void RenderKeybindList();

// Floating keybind list window (separate draggable overlay showing active binds)
void RenderKeybindListWindow(bool menuVisible = false);

// Key name helper
const char* GetKeyName(int vk);

// Listening state
bool IsListening();

// Called by Builder::ApplyConfig after writing snapshot values into widget
// pointers. Drops the wasOverridden flag on every cached base value so the
// next ProcessKeybinds tick re-captures fresh bases — otherwise a keybind
// that was active during ApplyConfig would restore the pre-config value on
// release instead of the newly-loaded one.
void OnConfigApplied();

} // namespace KeybindSystem
