#pragma once
#include "imgui.h"
#include "notifications.h"   // re-exported so a single include gets you toast notifications too
#include "debug_window.h"    // re-exported so a single include gets you the debug-window API too
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace MenuBuilder {

// ─── DebugWindow fluent handle ─────────────────────────────────────────
//
// Thin wrapper around DebugWindow's key-based API so callers can chain
// per-window entry pushes:
//
//     auto perf = MenuBuilder::Debug("perf", "Performance");
//     perf.Set("fps", (int)io.Framerate);
//     perf.Set("frame_ms", io.DeltaTime * 1000, 2);
//
// The window is auto-created on first call (or the first Debug() call
// for that key). Handle is trivial to copy / stash — it just carries
// the key + title strings.
class DebugRef {
public:
    DebugRef(const char* key, const char* title)
        : m_key(key ? key : ""), m_title(title ? title : (key ? key : "debug")) {
        ::DebugWindow::EnsureCreated(m_key.c_str(), m_title.c_str());
    }

    DebugRef& Set(const char* name, const char* value)
        { ::DebugWindow::Set(m_key.c_str(), m_title.c_str(), name, value); return *this; }
    DebugRef& Set(const char* name, const std::string& value)
        { ::DebugWindow::Set(m_key.c_str(), m_title.c_str(), name, value); return *this; }
    DebugRef& Set(const char* name, int value)
        { ::DebugWindow::Set(m_key.c_str(), m_title.c_str(), name, value); return *this; }
    DebugRef& Set(const char* name, float value, int decimals = 2)
        { ::DebugWindow::Set(m_key.c_str(), m_title.c_str(), name, value, decimals); return *this; }
    DebugRef& Set(const char* name, bool value)
        { ::DebugWindow::Set(m_key.c_str(), m_title.c_str(), name, value); return *this; }

    DebugRef& Remove(const char* name) { ::DebugWindow::Remove(m_key.c_str(), name); return *this; }
    DebugRef& Clear()                  { ::DebugWindow::Clear(m_key.c_str());        return *this; }

    DebugRef& Show()          { ::DebugWindow::SetVisible(m_key.c_str(), true);  return *this; }
    DebugRef& Hide()          { ::DebugWindow::SetVisible(m_key.c_str(), false); return *this; }
    DebugRef& Toggle()        { ::DebugWindow::Toggle(m_key.c_str());            return *this; }
    bool*     VisiblePtr()    { return ::DebugWindow::GetVisiblePtr(m_key.c_str(), m_title.c_str()); }

    const std::string& Key()   const { return m_key; }
    const std::string& Title() const { return m_title; }

private:
    std::string m_key;
    std::string m_title;
};

// Get or create a fluent handle for a debug window. Safe to call every
// frame; auto-creates on first call with `key`.
inline DebugRef Debug(const char* key, const char* title = nullptr) {
    return DebugRef(key, (title && title[0]) ? title : key);
}

enum class WidgetType {
    Checkbox, SliderFloat, SliderInt, Combo, MultiCombo,
    Button, InputText, ListBox, SearchableListBox, ColorPicker, Separator,
    StatusText, RenderCallback
};

struct InlineColorDef {
    std::string id;
    float (*singleColor)[4] = nullptr;
    float (*multiColors)[4] = nullptr;
    const char* const* colorNames = nullptr;
    int colorCount = 0; // 0 = single, >0 = multi
};

struct WidgetDef {
    WidgetType type = WidgetType::Checkbox;
    std::string label;
    std::string elementId;

    // Value pointers (one used per type)
    bool* boolPtr = nullptr;
    float* floatPtr = nullptr;
    int* intPtr = nullptr;
    char* textBuf = nullptr;
    size_t textBufSize = 0;
    bool* multiSelected = nullptr;

    // Config
    float floatMin = 0, floatMax = 1;
    int intMin = 0, intMax = 100;
    int decimalPlaces = 0;
    const char* unit = nullptr;
    const char* const* items = nullptr;
    int itemsCount = 0;
    int listBoxHeightItems = -1;
    float buttonWidth = -1.0f;

    // ListBox only: pointer to an int that names the index of a "highlighted"
    // (active) row, rendered in bold. nullptr or *ptr == -1 = no bold row.
    int* boldIndexPtr = nullptr;

    // Excludes this widget from MenuBuilder::Builder::CaptureConfig /
    // ApplyConfig. Use for UI-only state that shouldn't follow a config switch
    // (e.g. the configs-manager listbox, scratch inputs).
    bool transient = false;

    // Inline color attachment
    InlineColorDef inlineColor;
    bool hasInlineColor = false;

    // Optional override for the label that appears in the keybind list / overlay.
    // Empty = use the widget's display label.
    std::string keybindLabel;

    // SearchableListBox only — per-widget filter state. `searchBuf` is the
    // live text in the search field (owned by the widget so it persists
    // across frames without the caller managing storage). `searchPlaceholder`
    // is the label string shown on the search field. mutable so the
    // renderer can write to it via const WidgetDef& w.
    mutable char searchBuf[128] = {};
    std::string  searchPlaceholder;

    // Button callback (only used by WidgetType::Button)
    std::function<void()> onClick;

    // StatusText only — prefix label drawn in default text colour, followed
    // by a provider-driven state string in its own colour. Provider is
    // invoked every frame; return {nullptr, _} to skip the suffix.
    std::string statusPrefix;
    std::function<std::pair<const char*, ImU32>()> statusProvider;

    // RenderCallback only — raw ImGui escape hatch. Caller owns layout,
    // tooltips, and any config round-trip. Bypasses CaptureConfig/ApplyConfig.
    std::function<void()> renderCallback;

    // Conditional visibility — when set, widget is only rendered if the predicate returns true.
    // Use a bool pointer for the simple "show if other checkbox is on" pattern, or a lambda
    // for arbitrary conditions.
    std::function<bool()> visibleWhen;

    // Cog-only conditional visibility. When set, the settings cog icon next
    // to this widget renders only if the predicate returns true. Lets us
    // keep the parent "enabled" checkbox visible while collapsing its cog +
    // popup the moment the feature is off.
    std::function<bool()> settingsVisibleWhen;

    // Optional tooltip text — shown after a brief hover delay over the widget row.
    std::string tooltip;

    // Settings cog — when non-empty, a small gear icon appears after the widget label.
    // Clicking it opens a popup that renders these widgets with proper builder layout.
    std::vector<WidgetDef> settingsWidgets;

    // Per-item settings cogs (ListBox / SearchableListBox only) — one widget
    // vector per source-array index. When an index is present, the listbox
    // draws a cog to the right of that item's name and clicking it opens a
    // popup that renders the item's widget list with the standard layout.
    std::unordered_map<int, std::vector<WidgetDef>> itemSettings;

    // InputText only — placeholder hint shown inside the input frame when
    // the buffer is empty AND the widget's label is empty. Set via
    // .WithPlaceholder("…") so a labelless config-name field reads as
    // "config name" until the user types something.
    std::string placeholder;
};

struct ContainerDef {
    std::string name;
    int column = -1; // -1 = auto, 0 = left, 1 = right
    // Set via ContainerRef::ForSelector. Each entry is an icon-selector
    // slot index this container belongs to. Empty (the default) = always
    // visible regardless of selector state. Non-empty = visible only when
    // the parent subtab's selector has one of these slots active.
    // Containers in a subtab with no selector ignore this field.
    std::vector<int> selectorSlots;
    // Conditional visibility — when set, container is only rendered if the
    // predicate returns true. Mirrors WidgetDef::visibleWhen but at the
    // container level. Container occupies zero layout space when hidden.
    std::function<bool()> visibleWhen;
    std::vector<WidgetDef> widgets;
};

struct IconSelectorOption {
    std::string label;
    // Two mutually-exclusive icon paths:
    //   (a) `icon` non-empty + `image` null  → draw as a Font Awesome glyph
    //   (b) `image` non-null                 → draw via ImDrawList::AddImage
    //                                          (icon string ignored)
    // Use (b) for embedded spritesheets — e.g. apex's weapon-icon atlas
    // exposes one `ImTextureID` for the whole atlas and a per-icon UV
    // rect. Single full-image textures: leave uv0/uv1 at their defaults.
    std::string icon;
    ImTextureID image = (ImTextureID)0;
    ImVec2 uv0 = ImVec2(0.0f, 0.0f);
    ImVec2 uv1 = ImVec2(1.0f, 1.0f);
    // Optional tint applied as a multiplier on AddImage. Defaults to white
    // (no tint). The selected/hover lerp still drives the FA-glyph color
    // path; for images the tint stays constant — most game-asset icons
    // come pre-colored and look wrong if multiplied.
    ImU32 imageTint = IM_COL32(255, 255, 255, 255);
};

// Full-width icon-tab strip rendered ABOVE the subtab's container columns.
// Drives an external `int*` so the selection survives config snapshots and
// keybinds can target it (future). Containers tagged ForSelector(N) below
// the strip show/hide based on `*selectedIndex == N`.
struct IconSelectorDef {
    int* selectedIndex = nullptr;
    std::vector<IconSelectorOption> options;
};

struct SubTabDef {
    std::string name;
    std::string icon;
    // Optional — set via SubTabRef::AddIconSelector. When non-null, drawn
    // above the columns as a full-width tab strip.
    IconSelectorDef selector;
    bool hasSelector = false;
    std::vector<ContainerDef> containers;
};

struct TabDef {
    std::string name;
    std::string icon;
    std::vector<SubTabDef> subtabs;
};

// Forward declarations
class SubTabRef;
class ContainerRef;

class ContainerRef {
public:
    ContainerRef(ContainerDef* c, TabDef* t, SubTabDef* s)
        : m_cont(c), m_tab(t), m_sub(s) {}

    ContainerRef& AddCheckbox(const char* label, bool* v, const char* elemId = nullptr);
    ContainerRef& AddSliderFloat(const char* label, float* v, float vmin, float vmax,
                                  int decimals = 0, const char* unit = nullptr,
                                  const char* elemId = nullptr);
    ContainerRef& AddSliderInt(const char* label, int* v, int vmin, int vmax,
                                const char* unit = nullptr, const char* elemId = nullptr);
    ContainerRef& AddCombo(const char* label, int* v, const char* const items[], int count,
                            const char* elemId = nullptr);
    ContainerRef& AddMultiCombo(const char* label, bool sel[], const char* const items[], int count,
                                 const char* elemId = nullptr);
    ContainerRef& AddButton(const char* label, std::function<void()> onClick = {}, float width = -1.0f);
    ContainerRef& AddInputText(const char* label, char* buf, size_t size,
                                const char* elemId = nullptr);
    ContainerRef& AddListBox(const char* label, int* v, const char* const items[], int count,
                              int height = -1, const char* elemId = nullptr);
    // Same as AddListBox but with a search textbox rendered below the list.
    // Filter is a case-insensitive substring match. `*v` always reflects the
    // ORIGINAL item index (not the filtered position) so the selection
    // survives the user typing / clearing the search.
    // `placeholder` is OPTIONAL — pass nullptr (the default) or an empty
    // string and the search field starts with no hint text. Pass any
    // non-empty string ("search", "type a name…", etc.) to display it
    // inside the field while the buffer is empty.
    ContainerRef& AddSearchableListBox(const char* label, int* v,
                                        const char* const items[], int count,
                                        int height = -1,
                                        const char* placeholder = nullptr,
                                        const char* elemId = nullptr);
    ContainerRef& AddColorPicker(const char* label, float col[4], const char* elemId = nullptr);

    // Live status line — prefix label (default text colour) followed by a
    // provider-driven state string in its own colour. Provider returns
    // {text, ImU32 colour} fresh every frame. No config capture / no keybind.
    ContainerRef& AddStatusText(const char* prefix,
                                std::function<std::pair<const char*, ImU32>()> provider,
                                const char* elemId = nullptr);

    // Escape hatch for dynamic content that can't be expressed as a static
    // widget. Lambda calls raw ImGui — caller owns layout, tooltips, and
    // any config round-trip.
    ContainerRef& AddRenderCallback(std::function<void()> fn);

    // Visual-only horizontal separator. No element ID, no value, no keybind.
    ContainerRef& AddSeparator();

    // Attach inline color to the last widget
    ContainerRef& WithInlineColor(const char* id, float col[4]);
    ContainerRef& WithInlineColor(const char* id, float cols[][4], const char* const names[], int count);

    // Override the label shown for this widget's keybinds in the keybind list/overlay.
    ContainerRef& WithKeybindName(const char* name);

    // Wire a callback to fire when the user clicks a row in the most-recently
    // added SearchableListBox / ListBox — fires on EVERY click, including
    // when the row that's already selected gets re-clicked. Lets callers
    // refresh dependent widgets (e.g. mirror the selected name into a text
    // input) without watching index-change semantics that don't trigger on
    // a same-row reclick (the single-config case).
    ContainerRef& OnSelect(std::function<void()> cb);

    // Conditional visibility — widget renders only when the predicate is true.
    // Two convenience overloads: a bool pointer, or any callable returning bool.
    ContainerRef& VisibleWhen(bool* cond);
    ContainerRef& VisibleWhen(std::function<bool()> pred);

    // Cog-only visibility — see WidgetDef::settingsVisibleWhen. Attaches to
    // the most-recently-added widget. Use for "hide the cog + popup when
    // the parent enabled checkbox is off" pattern.
    ContainerRef& SettingsVisibleWhen(bool* cond);
    ContainerRef& SettingsVisibleWhen(std::function<bool()> pred);

    // Tooltip shown on hover.
    ContainerRef& WithTooltip(const char* text);

    // InputText only: placeholder hint rendered inside the empty input frame.
    // Pair with an empty label string ("") to render a labelless single-row
    // input (no extra "config name" label row above it) — the placeholder
    // text serves as the visual hint until the user types something.
    ContainerRef& WithPlaceholder(const char* text);

    // ListBox only: bind a live "active" index that renders bold. Caller
    // owns the int and is responsible for keeping it in sync each frame.
    ContainerRef& WithBoldIndex(int* ptr);

    // Mark the last-added widget as transient — its value is not captured
    // into nor applied from a config snapshot. Use for UI-only state.
    ContainerRef& Transient();

    // Settings cog — subsequent Add* calls go into the last widget's settings popup
    // until EndSettings() is called. The popup renders these with proper layout.
    ContainerRef& BeginSettings();
    ContainerRef& EndSettings();

    // Per-item settings cog (ListBox / SearchableListBox only). Subsequent
    // Add* calls target the last-added listbox widget's settings for item
    // `itemIdx` (the ORIGINAL source-array index, not a filtered position).
    // Pair with EndItemSettings() to return to top-level widgets. Multiple
    // BeginItemSettings blocks can be issued in sequence to fan out cogs
    // to several items on the same listbox.
    ContainerRef& BeginItemSettings(int itemIdx);
    ContainerRef& EndItemSettings();

    // Set column for subsequent widgets: 0=left, 1=right, -1=auto
    ContainerRef& Column(int col);

    // Tie this container to a slot of the parent subtab's icon selector.
    // Pass -1 (the default at construction time) to make it always visible.
    // Has no effect if the parent subtab has no selector.
    // Tag this container with one or more icon-selector slots. The container
    // is visible iff the selector's current index matches any tagged slot.
    // Calling ForSelector multiple times APPENDS — chaining
    //   .ForSelector(0).ForSelector(2)
    // is equivalent to
    //   .ForSelector({0, 2})
    // Pass -1 to clear all slots and revert to "always visible".
    ContainerRef& ForSelector(int slot);
    ContainerRef& ForSelector(std::initializer_list<int> slots);

private:
    std::vector<WidgetDef>& AddTarget() { return m_settingsTarget ? *m_settingsTarget : m_cont->widgets; }
    std::string AutoElementId(const char* label) const;
    ContainerDef* m_cont;
    TabDef* m_tab;
    SubTabDef* m_sub;
    std::vector<WidgetDef>* m_settingsTarget = nullptr; // non-null when inside BeginSettings
};

class IconSelectorRef {
public:
    IconSelectorRef(IconSelectorDef* d) : m_def(d) {}
    // Each call appends an option; the option's index = call order, 0-based.

    // (a) Font Awesome string — drawn with the current FA icon font.
    IconSelectorRef& AddOption(const char* label, const char* icon);

    // (b) Image — drawn via ImDrawList::AddImage. Pass uv bounds for an
    // atlas / sprite sheet, or leave defaults for a full single-image
    // texture. Optional tint multiplies the texture color (defaults to
    // pure white = no tint).
    IconSelectorRef& AddOption(const char* label, ImTextureID image,
                                ImVec2 uv0 = ImVec2(0.0f, 0.0f),
                                ImVec2 uv1 = ImVec2(1.0f, 1.0f),
                                ImU32 tint = IM_COL32(255, 255, 255, 255));
private:
    IconSelectorDef* m_def;
};

class SubTabRef {
public:
    SubTabRef(SubTabDef* s, TabDef* t) : m_sub(s), m_tab(t) {}
    ContainerRef AddContainer(const char* name, int column = -1);
    // Conditional-visibility container overload. The container is registered
    // unconditionally but its render block (header + widgets) is skipped when
    // the predicate returns false. Selector-slot tagging still applies on top.
    ContainerRef AddContainer(const char* name, int column,
                              std::function<bool()> predicate);
    // Adds a full-width icon-tab strip above this subtab's container
    // columns. The strip drives `*selectedIndex` and only one selector is
    // supported per subtab (a second call overwrites the first). Add
    // option entries on the returned ref; tag containers with
    // ContainerRef::ForSelector(N) to gate them on a slot.
    IconSelectorRef AddIconSelector(int* selectedIndex);
private:
    SubTabDef* m_sub;
    TabDef* m_tab;
};

class TabRef {
public:
    TabRef(TabDef* t) : m_tab(t) {}
    SubTabRef AddSubTab(const char* name, const char* icon = "");
private:
    TabDef* m_tab;
};

// ─── Config snapshot ─────────────────────────────────────────────────────
// In-memory representation of every widget's current value, keyed by elementId.
// Use Builder::CaptureConfig / ApplyConfig to round-trip values without file I/O.
// Serialization (to JSON, binary, etc.) is left to the caller — iterate `values`.

struct ConfigValue {
    enum class Type { None, Bool, Float, Int, MultiBool, Color, Text };
    Type type = Type::None;
    bool                b = false;
    float               f = 0.0f;
    int                 i = 0;
    std::vector<bool>   multi;
    float               color[4] = { 0, 0, 0, 0 };
    std::string         text;
};

struct Config {
    std::unordered_map<std::string, ConfigValue> values;
    // Per-container collapsed flag, keyed by the same stable "tab.sub.container"
    // slug Builder passes to BeginContainer. Captured into the snapshot so
    // collapsed/expanded state follows a config switch like any other value.
    std::unordered_map<std::string, bool> collapsedContainers;
};

// Singleton builder
class Builder {
public:
    static Builder& Get();

    void Clear();
    TabRef AddTab(const char* name, const char* icon);

    // Rendering
    void Render(float contentW);
    void RenderTopSubTabs(float menuW, float barH);
    void RenderBottomTabs(float menuW, float tabBarH);
    // Vertical rail variant — lays each tab as a fixed-size slot in a
    // column, top-anchored. Caller places the cursor at the rail origin
    // before invoking. tabH is the per-slot height.
    void RenderSideTabs(float railW, float tabH);
    // Pandora-style horizontal text tabs. Caller places the cursor at the
    // strip's left edge (e.g. right of the logo); this walks the tab list
    // and emits a TopTextTab per entry, each padded proportional to its
    // label width. Returns the total pixel width consumed so the caller
    // can position anything after (e.g. the top-right cog).
    float RenderTopTabs(float stripH, float labelPad);
    // Vertical text list of the current tab's subtabs — the left sidebar
    // in the pandora layout. Caller opens a child of the desired width
    // first (SidebarTab fills the content region horizontally). Handles
    // selection state; when only one subtab exists it still renders it
    // so the frame stays stable (Q: "always show the sidebar").
    void RenderSubtabSidebar();

    // Navigation
    int GetCurrentTab() const { return m_currentTab; }
    void SetCurrentTab(int idx);
    int GetCurrentSubTab() const;
    void SetCurrentSubTab(int idx);

    const std::vector<TabDef>& GetTabs() const { return m_tabs; }

    // ── Config snapshot ──────────────────────────────────────────────────
    // CaptureConfig walks every registered widget and copies its CURRENT live value
    // (read via the stored variable pointer) into a Config snapshot.
    // ApplyConfig walks the same widgets and writes any matching Config values back.
    // Both ignore widgets without a stored elementId or whose pointers are null.
    Config CaptureConfig() const;
    void   ApplyConfig(const Config& cfg);

    // Replace the items array + count on an existing listbox/combo/multicombo
    // widget. Used to drive dynamic lists (e.g. a "saved configs" listbox that
    // refreshes when configs are added/removed). The caller owns the items
    // pointer and must keep it alive until the next call (or shutdown).
    void UpdateListItems(const char* elementId, const char* const* items, int count);

    // ── Widget state lookup ──────────────────────────────────────────────
    //
    // Peek at the live value of a widget by elementId without holding the
    // variable pointer in scope. Useful for `.VisibleWhen(...)` lambdas that
    // gate one widget on another, or for ad-hoc "if X is on" checks in
    // higher-level code. Returns the fallback when the elementId is unknown
    // or the type mismatches what the widget actually stores.
    //
    // These walk the widget tree linearly. Cheap for typical menu sizes
    // (a few hundred widgets), but DON'T put them in a per-frame hot path
    // that iterates every widget — cache the result if you do.
    bool  GetBool (const char* elementId, bool  fallback = false) const;
    int   GetInt  (const char* elementId, int   fallback = 0)     const;
    float GetFloat(const char* elementId, float fallback = 0.0f)  const;

private:
    Builder() = default;

    void RenderSubTab(const SubTabDef& sub, float contentW,
                      const std::string& parentScope);
    void RenderContainer(const ContainerDef& container, float colW,
                         const std::string& parentScope);
    void RenderWidget(const WidgetDef& w);

public:
    // Render a list of widgets (used by settings cog popup)
    void RenderWidgetList(const std::vector<WidgetDef>& widgets);
private:
    float EstimateContainerHeight(const ContainerDef& container) const;

    std::vector<TabDef> m_tabs;
    int m_currentTab = 0;
    std::vector<int> m_sidebarIndex; // per-tab subtab selection
};

} // namespace MenuBuilder
