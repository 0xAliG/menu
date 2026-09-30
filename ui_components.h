#pragma once
#include "imgui.h"
#include "keybind_system.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace UI {

// Right-click keybind integration
bool HandleRightClickKeybind(const char* elementId, const char* label,
                             KeybindSystem::ElementType elemType,
                             float fMin = 0, float fMax = 1,
                             int iMin = 0, int iMax = 100,
                             int decimalPlaces = 0,
                             const std::vector<std::string>& comboItems = {});

// ─── Container (card) ───────────────────────────────────────────────────
// Draws a rounded panel with title. Auto-height based on content.
// Pass explicit width for column layouts, or 0 to use full available width.
//
// persistKey: when non-null, the collapsed/expanded toggle is stored in a
// stable-string-keyed table that survives label collisions across tabs and
// can be round-tripped through GetCollapseStates/ApplyCollapseStates for
// config persistence. nullptr → ephemeral per-ImGuiID storage (resets on
// label change, never persisted).
//
// collapsable: when false, the title-bar click handler + chevron are
// suppressed and the body always renders open. Used by the settings-cog
// popups (top-right menu cog + per-widget cog), which reuse the container
// look-and-feel but shouldn't behave like collapsable content cards.
bool BeginContainer(const char* label, float width = 0.0f,
                    const char* persistKey = nullptr,
                    bool collapsable = true);
void EndContainer();

// Snapshot of every stable-keyed container's current collapsed state.
// Pair with ApplyCollapseStates to restore from a saved snapshot.
const std::unordered_map<std::string, bool>& GetCollapseStates();
void ApplyCollapseStates(const std::unordered_map<std::string, bool>& states);

// ─── Widgets ────────────────────────────────────────────────────────────

// Square checkbox with accent fill + white checkmark
// reserveRight: shrink click area by this amount on the right (for inline color swatches)
bool Checkbox(const char* label, bool* v, const char* elementId = nullptr,
              float reserveRight = 0.0f);

// Thin slider: label+value row, accent fill, circular knob
bool SliderFloat(const char* label, float* v, float v_min, float v_max,
                 int decimalPlaces = 0, const char* unit = nullptr,
                 const char* elementId = nullptr);

bool SliderInt(const char* label, int* v, int v_min, int v_max,
               const char* unit = nullptr, const char* elementId = nullptr);

// Labeled dropdown combo
bool Combo(const char* label, int* current_item, const char* const items[],
           int items_count, const char* elementId = nullptr);

// Multi-select dropdown
bool MultiCombo(const char* label, bool selected[], const char* const items[],
                int items_count, const char* elementId = nullptr);

// Dark rounded button
bool Button(const char* label, float width = -1.0f);

// Labeled text input
bool InputText(const char* label, char* buf, size_t buf_size);

// Compact input variant with NO label row — `placeholder` text appears
// inside the field when the buffer is empty. Use for search boxes / inline
// inputs where a labelled row would waste vertical space.
// `id` scopes the ImGui ID stack so two placeholder inputs in the same
// container don't collide.
// noTopBorder / noBottomBorder: when set, omits that border edge of the
// frame AND squares the matching corners. Counterparts to ListBox's
// noBottomBorder so a search-input + listbox pair can sit flush with
// only one of the two adjacent borders rendering — the result reads as
// a single 1 px divider line instead of two stacked borders.
bool InputTextPlaceholder(const char* id, const char* placeholder,
                          char* buf, size_t buf_size,
                          bool noTopBorder = false,
                          bool noBottomBorder = false);

// Per-item cog callback: given an item index, return the popup content
// function for that item's settings — or an empty std::function to indicate
// "no cog for this item". The cog draws right-aligned inside the item row,
// just past the item name.
using ListBoxItemCogProvider = std::function<std::function<void()>(int itemIdx)>;

// Scrollable list with single selection.
// boldItemIndex (-1 = none): renders that row's text in the bold font, layered
// on top of the regular selection/hover styling. Useful for marking an "active"
// item that's independent of the user's current selection.
// noBottomBorder: when true, omits the bottom border edge of the frame AND
// squares the bottom corners (no rounding) so the listbox can sit flush
// against another widget below it without a visible seam. Used by the
// SearchableListBox composite to make the listbox + search-input pair read
// as one continuous outlined frame.
// itemCogProvider (optional): when supplied, each item is asked whether it
// should show a settings cog to the right of its name. Clicking that cog
// opens a popup rendered by the provider's returned content function.
// scopeKey (optional): unique string used to namespace per-item cog popup
// state so two listboxes with identical labels don't collide. Defaults to
// the label.
bool ListBox(const char* label, int* current_item, const char* const items[],
             int items_count, int height_in_items = -1, int boldItemIndex = -1,
             bool noBottomBorder = false,
             const ListBoxItemCogProvider& itemCogProvider = {},
             const char* scopeKey = nullptr);

// Color swatch + picker popup
bool ColorPicker(const char* label, float col[4]);

// Inline color picker — attaches swatch right-aligned to previous widget's row
// Single color variant
bool ColorPickerInline(const char* id, float col[4]);
// Multi-color variant with tabs
bool ColorPickerInline(const char* id, float cols[][4], const char* const names[], int count);

// ─── Color picker presets ───────────────────────────────────────────────
//
// User-saved RGBA tiles shown at the bottom of every color picker popup.
// Stored as packed 0xAABBGGRR (Imgui IM_COL32 layout) for compact JSON.
// Capped at kColorPresetMax to keep the row a single line.
//
// Left-click a tile → loads it into the active picker.
// Right-click a tile → removes it.
// "+" tile at the end → appends the current picker color (no-op if already at cap).
//
// Round-trip through ConfigSystem (settings.json) — they're a workflow
// preference, NOT a per-config gameplay value, so they survive config swaps.
constexpr int kColorPresetMax = 12;
const std::vector<uint32_t>& GetColorPresets();
void SetColorPresets(const std::vector<uint32_t>& presets);

// ─── Navigation ─────────────────────────────────────────────────────────

// Left sidebar text tab
bool SidebarTab(const char* label, bool selected);

// Top subtab (under title bar) — horizontal, icon + label centered, fixed width
bool TopSubTab(const char* label, const char* icon, bool selected, float width);

// Bottom tab: icon above label
bool BottomTab(const char* label, const char* icon, bool selected, float width);

// Side-rail tab: icon centered in a slot of the given width×height. Same
// visual language as BottomTab (accent-filled icon on active, muted otherwise,
// icon-scale animation) but laid out for a vertical column instead of a row.
bool SideTab(const char* label, const char* icon, bool selected,
             float width, float height);

// Top-strip text tab. Renders `label` as plain text, bright-white when
// selected, muted otherwise; no underline, no background, no icon.
// Returns TRUE on click. Layout is caller-driven — the button occupies
// `width × height` at the current cursor.
// `cornerFlags` selects which corners of the selected-tab background get
// rounded — pass e.g. `ImDrawFlags_RoundCornersTopLeft` for the first tab
// in the strip and `ImDrawFlags_RoundCornersTopRight` for the last.
// `openAnim` participates in the menu's open-in animation: when < 1 the
// selected tab's fill grows from the bottom edge upward (openAnim = 0 →
// no fill, 1 → full fill). Callers not wired to the open animation can
// leave it at 1 for the original always-full behavior.
bool TopTextTab(const char* label, bool selected, float width, float height,
                ImDrawFlags cornerFlags = ImDrawFlags_RoundCornersNone,
                float openAnim = 1.0f);

// ─── Helpers ────────────────────────────────────────────────────────────

// Themed tooltip — call after a widget; shown on hover with a short delay.
// Uses the previous item's hover state via ImGui::IsItemHovered.
void Tooltip(const char* text);

// Draws a question-mark icon and shows a themed tooltip when the icon is hovered.
// Pass an ImGuiID-stable key (e.g. the widget elementId) so each marker has its own
// hover-delay timer. Returns true if currently hovered.
bool TooltipMarker(const char* key, ImVec2 pos, float size, const char* tooltipText);

// Settings cog icon — draws a small gear icon at `pos`. Clicking it opens a popup
// whose body is rendered by `contentFn`. The popup auto-sizes vertically.
// `key` must be a stable unique string (e.g. the widget elementId).
// `extraPopupWidth` widens the popup beyond its base width so chips attached
// to widgets inside the popup have room to render without clipping.
bool SettingsCog(const char* key, const char* label, ImVec2 pos, float size,
                 const std::function<void()>& contentFn,
                 bool rowHovered = false,
                 float extraPopupWidth = 0.0f);

// Renders the currently-open settings cog popup. Call once per frame from the
// top-level render path (after Builder::Render, outside any container/group).
void RenderSettingsCogPopup();

void KeybindLabel(const char* text, float rightEdge);

// ─── Keybind chip ──────────────────────────────────────────────────────
//
// Small inline pill drawn right of a widget's label showing the state of
// one keybind attached to that element. Draws one of:
//   `[]`   — unbound (freshly added)
//   `[…]`  — waiting for a key press (accent-tinted background)
//   `[K]`  — bound to key K (e.g. `[E]`, `[F1]`)
//
// Left-click: enters KeybindSystem::BeginValueEdit for the bind, then
// automatically advances to WaitingForKey once the target widget's value
// changes. Right-click: opens a mini popup with Change value / Change key /
// Change mode / Delete.
//
// Returns the pixel width drawn (0 if the bind doesn't exist). The chip is
// drawn with its top-left at `pos` and its height auto-fits the row.
float KeybindChip(const char* elementId, int localBindIdx, ImVec2 pos,
                  float rowH);

// Renders the currently-open chip context menu. Call once per frame from the
// top-level render path (same spot as RenderSettingsCogPopup).
void RenderKeybindChipContextMenu();

// ─── Standalone keybind chip ───────────────────────────────────────────
//
// Row layout: `label` on the left, a keybind chip anchored right, showing
// the current VK stored in *vkPtr. Left-clicking the chip enters key-
// listen mode via KeybindSystem::BeginStandaloneKeyEdit, which captures
// the next key press straight into *vkPtr. Used for one-off keybinds
// that aren't part of the widget-level keybind list (e.g. the menu-
// toggle key). If `locked` is true, right-clicking the chip does nothing
// (no delete option), so the bind can't be removed — only rebound.
void StandaloneKeybindChip(const char* label, int* vkPtr, bool locked = false);

// Get current widget width (respects container context)
float GetWidgetWidth();

// Horizontal indent applied to non-checkbox widgets (sliders, combos,
// listbox, input, etc.) so their frame starts to the right of where
// checkbox labels sit. Compose renderers should use this to align any
// external label (drawn OUTSIDE the widget itself) with the widget frame.
float NonCheckboxIndent();

// Temporarily override widget width (for popup content). Pass 0 to restore.
void PushWidgetWidth(float w);
void PopWidgetWidth();

// Reset animations (call on tab switch to replay slide-in effects)
void ResetAnimations();

// Per-frame tick — call once at the top of Menu::Render. Currently runs the
// periodic GC over the internal animation-state map so untouched entries
// (widgets that stopped rendering) don't accumulate across the session.
void FrameTick();

// Update sliding tab indicators (call each frame before rendering tabs)
void DrawSidebarTabIndicator();
void DrawTopSubTabIndicator();
void DrawBottomTabIndicator();

} // namespace UI
