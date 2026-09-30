#include "menu_builder.h"
#include "ui_components.h"
#include "menu_style.h"
#include "keybind_system.h"
#include "menu.h" // Menu::GetOpenAnim() for the top-tab slide animation
#include <algorithm>
#include <cctype>
#include <cmath>

// Shorthand for DPI-scaled pixel values
#define S(x) (MenuStyle::S(x))

namespace MenuBuilder {

// ─── Auto element ID generation ─────────────────────────────────────────────

std::string ContainerRef::AutoElementId(const char* label) const {
    // Path-based namespacing: tab.subtab.container.widget. Without the
    // container segment, two containers in the same subtab with widgets
    // sharing the same label (e.g., per-weapon "enabled" checkboxes inside
    // separate "aim" containers tagged ForSelector(N)) would collapse to
    // the same element ID and stomp each other's config snapshots. The
    // container name added below disambiguates them automatically; widgets
    // passing an explicit `elemId` bypass this entirely.
    std::string tabName = m_tab->name;
    std::string subName = m_sub->name;
    std::string contName = m_cont ? m_cont->name : "";
    std::string lbl = label;

    auto toLower = [](std::string& s) {
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    };
    auto spaceToDot = [](std::string& s) {
        for (auto& c : s) if (c == ' ') c = '.';
    };

    toLower(tabName);  spaceToDot(tabName);
    toLower(subName);  spaceToDot(subName);
    toLower(contName); spaceToDot(contName);
    toLower(lbl);      spaceToDot(lbl);

    std::string id = tabName + "." + subName;
    if (!contName.empty()) id += "." + contName;
    id += "." + lbl;
    return id;
}

// ─── ContainerRef methods ───────────────────────────────────────────────────

ContainerRef& ContainerRef::AddCheckbox(const char* label, bool* v, const char* elemId) {
    WidgetDef w;
    w.type = WidgetType::Checkbox;
    w.label = label;
    w.elementId = elemId ? elemId : AutoElementId(label);
    w.boolPtr = v;
    AddTarget().push_back(std::move(w));
    return *this;
}

ContainerRef& ContainerRef::AddSliderFloat(const char* label, float* v, float vmin, float vmax,
                                            int decimals, const char* unit, const char* elemId) {
    WidgetDef w;
    w.type = WidgetType::SliderFloat;
    w.label = label;
    w.elementId = elemId ? elemId : AutoElementId(label);
    w.floatPtr = v;
    w.floatMin = vmin;
    w.floatMax = vmax;
    w.decimalPlaces = decimals;
    w.unit = unit;
    AddTarget().push_back(std::move(w));
    return *this;
}

ContainerRef& ContainerRef::AddSliderInt(const char* label, int* v, int vmin, int vmax,
                                          const char* unit, const char* elemId) {
    WidgetDef w;
    w.type = WidgetType::SliderInt;
    w.label = label;
    w.elementId = elemId ? elemId : AutoElementId(label);
    w.intPtr = v;
    w.intMin = vmin;
    w.intMax = vmax;
    w.unit = unit;
    AddTarget().push_back(std::move(w));
    return *this;
}

ContainerRef& ContainerRef::AddCombo(const char* label, int* v, const char* const items[], int count,
                                      const char* elemId) {
    WidgetDef w;
    w.type = WidgetType::Combo;
    w.label = label;
    w.elementId = elemId ? elemId : AutoElementId(label);
    w.intPtr = v;
    w.items = items;
    w.itemsCount = count;
    AddTarget().push_back(std::move(w));
    return *this;
}

ContainerRef& ContainerRef::AddMultiCombo(const char* label, bool sel[], const char* const items[], int count,
                                           const char* elemId) {
    WidgetDef w;
    w.type = WidgetType::MultiCombo;
    w.label = label;
    w.elementId = elemId ? elemId : AutoElementId(label);
    w.multiSelected = sel;
    w.items = items;
    w.itemsCount = count;
    AddTarget().push_back(std::move(w));
    return *this;
}

ContainerRef& ContainerRef::AddButton(const char* label, std::function<void()> onClick, float width) {
    WidgetDef w;
    w.type = WidgetType::Button;
    w.label = label;
    w.buttonWidth = width;
    w.onClick = std::move(onClick);
    AddTarget().push_back(std::move(w));
    return *this;
}

ContainerRef& ContainerRef::AddInputText(const char* label, char* buf, size_t size,
                                          const char* elemId) {
    WidgetDef w;
    w.type = WidgetType::InputText;
    w.label = label;
    w.elementId = elemId ? elemId : AutoElementId(label);
    w.textBuf = buf;
    w.textBufSize = size;
    AddTarget().push_back(std::move(w));
    return *this;
}

ContainerRef& ContainerRef::AddListBox(const char* label, int* v, const char* const items[], int count,
                                        int height, const char* elemId) {
    WidgetDef w;
    w.type = WidgetType::ListBox;
    w.label = label;
    w.elementId = elemId ? elemId : AutoElementId(label);
    w.intPtr = v;
    w.items = items;
    w.itemsCount = count;
    w.listBoxHeightItems = height;
    AddTarget().push_back(std::move(w));
    return *this;
}

ContainerRef& ContainerRef::AddSearchableListBox(const char* label, int* v,
                                                 const char* const items[], int count,
                                                 int height,
                                                 const char* placeholder,
                                                 const char* elemId) {
    WidgetDef w;
    w.type = WidgetType::SearchableListBox;
    w.label = label;
    w.elementId = elemId ? elemId : AutoElementId(label);
    w.intPtr = v;
    w.items = items;
    w.itemsCount = count;
    w.listBoxHeightItems = height;
    w.searchPlaceholder = placeholder ? placeholder : "";
    // searchBuf zero-initialised by WidgetDef's default member init.
    AddTarget().push_back(std::move(w));
    return *this;
}

ContainerRef& ContainerRef::AddColorPicker(const char* label, float col[4], const char* elemId) {
    WidgetDef w;
    w.type = WidgetType::ColorPicker;
    w.label = label;
    w.elementId = elemId ? elemId : AutoElementId(label);
    w.floatPtr = col;
    AddTarget().push_back(std::move(w));
    return *this;
}

ContainerRef& ContainerRef::AddSeparator() {
    WidgetDef w;
    w.type = WidgetType::Separator;
    // Visual-only — no label, no elementId, not captured by config snapshot.
    AddTarget().push_back(std::move(w));
    return *this;
}

ContainerRef& ContainerRef::AddStatusText(const char* prefix,
                                          std::function<std::pair<const char*, ImU32>()> provider,
                                          const char* elemId) {
    WidgetDef w;
    w.type           = WidgetType::StatusText;
    w.label          = prefix ? prefix : "";
    w.statusPrefix   = prefix ? prefix : "";
    w.statusProvider = std::move(provider);
    w.elementId      = elemId ? elemId : AutoElementId(prefix ? prefix : "status");
    AddTarget().push_back(std::move(w));
    return *this;
}

ContainerRef& ContainerRef::AddRenderCallback(std::function<void()> fn) {
    WidgetDef w;
    w.type           = WidgetType::RenderCallback;
    w.renderCallback = std::move(fn);
    // No elementId — render callbacks own their own state and bypass
    // CaptureConfig/ApplyConfig entirely.
    AddTarget().push_back(std::move(w));
    return *this;
}

ContainerRef& ContainerRef::WithInlineColor(const char* id, float col[4]) {
    auto& vec = AddTarget();
    if (!vec.empty()) {
        auto& last = vec.back();
        last.hasInlineColor = true;
        last.inlineColor.id = id;
        last.inlineColor.singleColor = reinterpret_cast<float(*)[4]>(col);
        last.inlineColor.colorCount = 0;
    }
    return *this;
}

ContainerRef& ContainerRef::WithInlineColor(const char* id, float cols[][4], const char* const names[], int count) {
    auto& vec = AddTarget();
    if (!vec.empty()) {
        auto& last = vec.back();
        last.hasInlineColor = true;
        last.inlineColor.id = id;
        last.inlineColor.multiColors = cols;
        last.inlineColor.colorNames = names;
        last.inlineColor.colorCount = count;
    }
    return *this;
}

ContainerRef& ContainerRef::OnSelect(std::function<void()> cb) {
    auto& vec = AddTarget();
    if (!vec.empty()) vec.back().onClick = std::move(cb);
    return *this;
}

ContainerRef& ContainerRef::WithKeybindName(const char* name) {
    auto& vec = AddTarget();
    if (!vec.empty()) {
        auto& last = vec.back();
        last.keybindLabel = name ? name : "";
        // Register immediately so the override is in place even before the first render.
        if (!last.elementId.empty())
            KeybindSystem::SetDisplayLabel(last.elementId, last.keybindLabel);
    }
    return *this;
}

ContainerRef& ContainerRef::VisibleWhen(bool* cond) {
    auto& vec = AddTarget();
    if (!vec.empty() && cond) {
        vec.back().visibleWhen = [cond]() { return *cond; };
    }
    return *this;
}

ContainerRef& ContainerRef::VisibleWhen(std::function<bool()> pred) {
    auto& vec = AddTarget();
    if (!vec.empty()) {
        vec.back().visibleWhen = std::move(pred);
    }
    return *this;
}

ContainerRef& ContainerRef::SettingsVisibleWhen(bool* cond) {
    auto& vec = AddTarget();
    if (!vec.empty() && cond) {
        vec.back().settingsVisibleWhen = [cond]() { return *cond; };
    }
    return *this;
}

ContainerRef& ContainerRef::SettingsVisibleWhen(std::function<bool()> pred) {
    auto& vec = AddTarget();
    if (!vec.empty()) {
        vec.back().settingsVisibleWhen = std::move(pred);
    }
    return *this;
}

ContainerRef& ContainerRef::WithTooltip(const char* text) {
    auto& vec = AddTarget();
    if (!vec.empty()) {
        vec.back().tooltip = text ? text : "";
    }
    return *this;
}

ContainerRef& ContainerRef::WithPlaceholder(const char* text) {
    auto& vec = AddTarget();
    if (!vec.empty()) {
        vec.back().placeholder = text ? text : "";
    }
    return *this;
}

ContainerRef& ContainerRef::WithBoldIndex(int* ptr) {
    auto& vec = AddTarget();
    if (!vec.empty()) vec.back().boldIndexPtr = ptr;
    return *this;
}

ContainerRef& ContainerRef::Transient() {
    auto& vec = AddTarget();
    if (!vec.empty()) vec.back().transient = true;
    return *this;
}

ContainerRef& ContainerRef::BeginSettings() {
    if (!m_cont->widgets.empty()) {
        m_settingsTarget = &m_cont->widgets.back().settingsWidgets;
    }
    return *this;
}

ContainerRef& ContainerRef::EndSettings() {
    m_settingsTarget = nullptr;
    return *this;
}

ContainerRef& ContainerRef::BeginItemSettings(int itemIdx) {
    if (!m_cont->widgets.empty()) {
        // Creates an empty vector on first access — subsequent AddXxx calls
        // append to it. The itemIdx must be the ORIGINAL source-array index,
        // matching what the listbox will pass to its itemCogProvider.
        m_settingsTarget = &m_cont->widgets.back().itemSettings[itemIdx];
    }
    return *this;
}

ContainerRef& ContainerRef::EndItemSettings() {
    m_settingsTarget = nullptr;
    return *this;
}

ContainerRef& ContainerRef::Column(int col) {
    m_cont->column = col;
    return *this;
}

ContainerRef& ContainerRef::ForSelector(int slot) {
    if (slot < 0) m_cont->selectorSlots.clear();
    else          m_cont->selectorSlots.push_back(slot);
    return *this;
}

ContainerRef& ContainerRef::ForSelector(std::initializer_list<int> slots) {
    for (int s : slots) {
        if (s < 0) { m_cont->selectorSlots.clear(); return *this; }
        m_cont->selectorSlots.push_back(s);
    }
    return *this;
}

// ─── IconSelectorRef ────────────────────────────────────────────────────────

IconSelectorRef& IconSelectorRef::AddOption(const char* label, const char* icon) {
    IconSelectorOption opt;
    opt.label = label ? label : "";
    opt.icon  = icon  ? icon  : "";
    m_def->options.push_back(std::move(opt));
    return *this;
}

IconSelectorRef& IconSelectorRef::AddOption(const char* label, ImTextureID image,
                                             ImVec2 uv0, ImVec2 uv1, ImU32 tint) {
    IconSelectorOption opt;
    opt.label     = label ? label : "";
    opt.image     = image;
    opt.uv0       = uv0;
    opt.uv1       = uv1;
    opt.imageTint = tint;
    m_def->options.push_back(std::move(opt));
    return *this;
}

// ─── SubTabRef / TabRef / Builder registration ──────────────────────────────

ContainerRef SubTabRef::AddContainer(const char* name, int column) {
    ContainerDef def;
    def.name = name ? name : "";
    def.column = column;
    m_sub->containers.push_back(std::move(def));
    return ContainerRef(&m_sub->containers.back(), m_tab, m_sub);
}

ContainerRef SubTabRef::AddContainer(const char* name, int column,
                                     std::function<bool()> predicate) {
    ContainerDef def;
    def.name        = name ? name : "";
    def.column      = column;
    def.visibleWhen = std::move(predicate);
    m_sub->containers.push_back(std::move(def));
    return ContainerRef(&m_sub->containers.back(), m_tab, m_sub);
}

IconSelectorRef SubTabRef::AddIconSelector(int* selectedIndex) {
    m_sub->selector.selectedIndex = selectedIndex;
    m_sub->selector.options.clear();
    m_sub->hasSelector = true;
    return IconSelectorRef(&m_sub->selector);
}

SubTabRef TabRef::AddSubTab(const char* name, const char* icon) {
    SubTabDef def;
    def.name = name ? name : "";
    def.icon = icon ? icon : "";
    m_tab->subtabs.push_back(std::move(def));
    return SubTabRef(&m_tab->subtabs.back(), m_tab);
}

Builder& Builder::Get() {
    static Builder instance;
    return instance;
}

void Builder::Clear() {
    m_tabs.clear();
    m_sidebarIndex.clear();
    m_currentTab = 0;
}

TabRef Builder::AddTab(const char* name, const char* icon) {
    m_tabs.push_back(TabDef{name, icon, {}});
    m_sidebarIndex.push_back(0);
    return TabRef(&m_tabs.back());
}

void Builder::SetCurrentTab(int idx) {
    if (idx >= 0 && idx < (int)m_tabs.size())
        m_currentTab = idx;
}

int Builder::GetCurrentSubTab() const {
    if (m_currentTab >= 0 && m_currentTab < (int)m_sidebarIndex.size())
        return m_sidebarIndex[m_currentTab];
    return 0;
}

void Builder::SetCurrentSubTab(int idx) {
    if (m_currentTab >= 0 && m_currentTab < (int)m_sidebarIndex.size())
        m_sidebarIndex[m_currentTab] = idx;
}

// ─── Rendering ──────────────────────────────────────────────────────────────

float Builder::EstimateContainerHeight(const ContainerDef& container) const {
    float h = S(26.0f) + S(6.0f) + S(10.0f); // title + padTop + padBottom
    for (auto& w : container.widgets) {
        switch (w.type) {
        case WidgetType::Checkbox:    h += S(24.0f); break;
        case WidgetType::SliderFloat: h += S(38.0f); break;
        case WidgetType::SliderInt:   h += S(38.0f); break;
        case WidgetType::Combo:       h += S(46.0f); break;
        case WidgetType::MultiCombo:  h += S(46.0f); break;
        case WidgetType::Button:      h += S(34.0f); break;
        case WidgetType::InputText:   h += S(46.0f); break;
        case WidgetType::ListBox:     h += (w.listBoxHeightItems > 0 ? w.listBoxHeightItems : 4) * S(20.0f) + S(30.0f); break;
        case WidgetType::SearchableListBox:
            // Same as ListBox + one frame-only input below (no label row,
            // and zero gap since the input is drawn flush against the
            // listbox bottom).
            h += (w.listBoxHeightItems > 0 ? w.listBoxHeightItems : 4) * S(20.0f) + S(30.0f);
            h += S(18.0f);
            break;
        case WidgetType::ColorPicker: h += S(30.0f); break;
        case WidgetType::Separator:   h += S(8.0f); break;
        case WidgetType::StatusText:  h += S(20.0f); break;
        // RenderCallback height is unknowable here — caller owns layout.
        // Reserve a single-row default so containers aren't sized to zero;
        // the actual cursor advance happens at render time.
        case WidgetType::RenderCallback: h += S(24.0f); break;
        }
        if (w.hasInlineColor) h += S(4.0f); // inline color adds minimal height
    }
    return h;
}

void Builder::RenderWidget(const WidgetDef& w) {
    // Keep the keybind list label override in sync (cheap map upsert per frame).
    if (!w.elementId.empty() && !w.keybindLabel.empty())
        KeybindSystem::SetDisplayLabel(w.elementId, w.keybindLabel);

    // Snapshot the cursor BEFORE anything else so cog + tooltip icons can be
    // placed relative to a known-stable label origin. Reading GetItemRectMin
    // after the widget renders returns the last submitted item's rect — for
    // multi-row widgets (slider/combo/input/listbox) that's the widget frame
    // BELOW the label, which puts the cog nowhere near the label. Snapping
    // here gives us the row where the label actually paints.
    ImVec2 widgetBasePos = ImGui::GetCursorScreenPos();

    // Always wrap so we can read the widget's bounding rect for tooltips + bind indicator.
    ImGui::BeginGroup();

    float reserveRight = 0.0f;
    if (w.hasInlineColor && w.type == WidgetType::Checkbox) {
        int cnt = w.inlineColor.colorCount;
        reserveRight = (cnt > 0 ? S(14.0f) * cnt : S(14.0f)) + S(4.0f);
    }

    // Chip gutter is reserved by RenderContainer for the container that
    // owns this widget. Here we just need to know:
    //   • chipBlockN            — how many chips to draw (0 = skip block)
    //   • chipOverflow          — how much this widget's chip block spills
    //                             beyond its natural NonCheckboxIndent
    //                             margin (used to find the anchor)
    //   • chipBlockW            — raw chip block width (only used by
    //                             Checkbox to shrink its InvisibleButton)
    int   chipBlockN = w.elementId.empty()
                       ? 0
                       : KeybindSystem::CountBindsFor(w.elementId);
    float chipOverflow = 0.0f;
    float chipBlockW   = 0.0f;
    if (chipBlockN > 0) {
        chipOverflow = KeybindSystem::ChipBlockOverflowFor(
            w.elementId, UI::NonCheckboxIndent());
        chipBlockW = KeybindSystem::ChipBlockWidthFor(w.elementId);
    }
    if (w.type == WidgetType::Checkbox && chipBlockW > 0.0f) {
        // Checkbox's InvisibleButton spans full width — shrink its
        // clickable area by the chip block so chip clicks don't also
        // toggle the checkbox.
        reserveRight += chipBlockW;
    }

    // Snapshot the effective widget width so the chip loop can right-anchor
    // to the container's inner right edge (widgetBasePos.x + origWidgetWidth
    // + chipOverflow — the reserved gutter sits immediately past the
    // widget's usable area).
    float origWidgetWidth = UI::GetWidgetWidth();

    switch (w.type) {
    case WidgetType::Checkbox:
        UI::Checkbox(w.label.c_str(), w.boolPtr,
                     w.elementId.empty() ? nullptr : w.elementId.c_str(),
                     reserveRight);
        break;
    case WidgetType::SliderFloat:
        UI::SliderFloat(w.label.c_str(), w.floatPtr, w.floatMin, w.floatMax,
                        w.decimalPlaces, w.unit,
                        w.elementId.empty() ? nullptr : w.elementId.c_str());
        break;
    case WidgetType::SliderInt:
        UI::SliderInt(w.label.c_str(), w.intPtr, w.intMin, w.intMax,
                      w.unit,
                      w.elementId.empty() ? nullptr : w.elementId.c_str());
        break;
    case WidgetType::Combo:
        UI::Combo(w.label.c_str(), w.intPtr, w.items, w.itemsCount,
                  w.elementId.empty() ? nullptr : w.elementId.c_str());
        break;
    case WidgetType::MultiCombo:
        UI::MultiCombo(w.label.c_str(), w.multiSelected, w.items, w.itemsCount,
                       w.elementId.empty() ? nullptr : w.elementId.c_str());
        break;
    case WidgetType::Button:
        if (UI::Button(w.label.c_str(), w.buttonWidth)) {
            if (w.onClick) w.onClick();
        }
        break;
    case WidgetType::InputText:
        // Labelless + placeholder path — use InputTextPlaceholder so the
        // empty input frame shows the hint text until the user types
        // something. Labelled path renders the normal label-row layout.
        if (w.label.empty() && !w.placeholder.empty()) {
            const char* idScope = w.elementId.empty() ? "##itph"
                                                      : w.elementId.c_str();
            UI::InputTextPlaceholder(idScope, w.placeholder.c_str(),
                                     w.textBuf, w.textBufSize);
        } else {
            UI::InputText(w.label.c_str(), w.textBuf, w.textBufSize);
        }
        break;
    case WidgetType::ListBox: {
        int boldIdx = (w.boldIndexPtr ? *w.boldIndexPtr : -1);
        // Item-cog provider: for each item index, return a lambda that
        // renders the item's settings widget list — or an empty function if
        // this item has no cog attached.
        UI::ListBoxItemCogProvider provider;
        if (!w.itemSettings.empty()) {
            const auto* itemSettings = &w.itemSettings;
            provider = [itemSettings](int idx) -> std::function<void()> {
                auto it = itemSettings->find(idx);
                if (it == itemSettings->end() || it->second.empty())
                    return {};
                const auto* widgets = &it->second;
                return [widgets]() {
                    Builder::Get().RenderWidgetList(*widgets);
                };
            };
        }
        UI::ListBox(w.label.c_str(), w.intPtr, w.items, w.itemsCount,
                    w.listBoxHeightItems, boldIdx,
                    /*noBottomBorder=*/false, provider,
                    w.elementId.empty() ? w.label.c_str() : w.elementId.c_str());
        break;
    }
    case WidgetType::SearchableListBox: {
        // Filter items by case-insensitive substring match against the
        // per-widget search buffer. The listbox renders only the matches;
        // selection is translated back to the ORIGINAL index so *intPtr
        // stays meaningful regardless of the current filter.
        const char* query = w.searchBuf;
        const bool noQuery = (query[0] == '\0');

        auto contains_ci = [](const char* hay, const char* needle) -> bool {
            if (!*needle) return true;
            for (const char* h = hay; *h; ++h) {
                const char* hh = h;
                const char* nn = needle;
                while (*hh && *nn &&
                       std::tolower((unsigned char)*hh) == std::tolower((unsigned char)*nn)) {
                    ++hh; ++nn;
                }
                if (!*nn) return true;
            }
            return false;
        };

        // Build the parallel filtered arrays once per frame. Reserves up
        // front to avoid reallocation under the typical "no filter" case
        // where every item matches.
        std::vector<int>           filteredIdx;
        std::vector<const char*>   filteredItems;
        filteredIdx.reserve((size_t)w.itemsCount);
        filteredItems.reserve((size_t)w.itemsCount);
        for (int i = 0; i < w.itemsCount; i++) {
            if (!w.items || !w.items[i]) continue;
            if (noQuery || contains_ci(w.items[i], query)) {
                filteredIdx.push_back(i);
                filteredItems.push_back(w.items[i]);
            }
        }

        // Translate the live selection (original index) and bold index to
        // filtered positions. -1 means "not in the current filter view"
        // which UI::ListBox renders as no highlight, which is correct.
        const int liveIdx = (w.intPtr ? *w.intPtr : -1);
        const int boldOrig = (w.boldIndexPtr ? *w.boldIndexPtr : -1);
        int mappedLive = -1, mappedBold = -1;
        for (int fi = 0; fi < (int)filteredIdx.size(); fi++) {
            if (filteredIdx[fi] == liveIdx)  mappedLive = fi;
            if (filteredIdx[fi] == boldOrig) mappedBold = fi;
        }

        // Layout: label row → search input (noBottomBorder, top piece) →
        // listbox (full border, top edge IS the 1 px divider). Render the
        // label ourselves so it sits above the input instead of getting
        // owned by the listbox's internal label row.
        //
        // Offset the label's X by UI::NonCheckboxIndent() so it aligns with
        // the search input / listbox frame below (both self-indent), instead
        // of sitting flush-left where a checkbox label would live.
        ImDrawList* dl     = ImGui::GetWindowDrawList();
        ImVec2      base   = ImGui::GetCursorScreenPos();
        float       lblH   = ImGui::GetTextLineHeight();
        ImVec2      lblPos(base.x + UI::NonCheckboxIndent(), base.y);
        MenuStyle::TextShadow(dl, lblPos, MenuStyle::Colors::Text.u32(),
                              w.label.c_str());
        ImGui::SetCursorScreenPos(base);
        ImGui::Dummy(ImVec2(0, lblH));

        const char* idScope = w.elementId.empty() ? "##slbsearch" : w.elementId.c_str();
        UI::InputTextPlaceholder(idScope, w.searchPlaceholder.c_str(),
                                 w.searchBuf, sizeof(w.searchBuf),
                                 /*noTopBorder=*/false,
                                 /*noBottomBorder=*/true);

        // Flush against the input's bottom — back up by the standard
        // inter-widget spacing so the listbox sits with NO inter-widget
        // gap. The listbox's full top border lands exactly where the
        // input's bottom border would have been: one crisp 1 px divider.
        ImVec2 curAfterInput = ImGui::GetCursorScreenPos();
        float  spacingY      = ImGui::GetStyle().ItemSpacing.y;
        ImGui::SetCursorScreenPos(
            ImVec2(curAfterInput.x, curAfterInput.y - spacingY));

        // Item-cog provider — the listbox sees FILTERED positions, so we
        // translate back to the ORIGINAL source-array index before looking
        // up the caller-registered per-item settings.
        UI::ListBoxItemCogProvider provider;
        if (!w.itemSettings.empty()) {
            const auto* itemSettings = &w.itemSettings;
            const auto* fIdx = &filteredIdx;
            provider = [itemSettings, fIdx](int filteredPos) -> std::function<void()> {
                if (filteredPos < 0 || filteredPos >= (int)fIdx->size()) return {};
                int origIdx = (*fIdx)[filteredPos];
                auto it = itemSettings->find(origIdx);
                if (it == itemSettings->end() || it->second.empty())
                    return {};
                const auto* widgets = &it->second;
                return [widgets]() {
                    Builder::Get().RenderWidgetList(*widgets);
                };
            };
        }
        bool listClicked = UI::ListBox(/*label=*/"", &mappedLive,
                    filteredItems.empty() ? nullptr : filteredItems.data(),
                    (int)filteredItems.size(),
                    w.listBoxHeightItems, mappedBold,
                    /*noBottomBorder=*/false, provider,
                    w.elementId.empty() ? w.label.c_str() : w.elementId.c_str());

        // Click → write *intPtr from the filtered selection. We use the
        // CLICK event (not an index-change check) so re-clicking the same
        // row — the single-row case — still propagates. The mappedLive >=0
        // guard protects the typed-query case where the current selection
        // gets filtered out (listClicked is false there, no overwrite).
        if (listClicked && w.intPtr &&
            mappedLive >= 0 && mappedLive < (int)filteredIdx.size()) {
            *w.intPtr = filteredIdx[mappedLive];
        }
        // Caller-supplied click callback — fires on every click so callers
        // can refresh dependent widgets (textbox mirroring etc.) without
        // chasing change-detection that misses the same-row reclick.
        if (listClicked && w.onClick) w.onClick();
        break;
    }
    case WidgetType::ColorPicker:
        UI::ColorPicker(w.label.c_str(), w.floatPtr);
        break;
    case WidgetType::Separator:
        ImGui::Separator();
        break;
    case WidgetType::StatusText: {
        ImGui::TextUnformatted(w.statusPrefix.c_str());
        if (w.statusProvider) {
            auto pair = w.statusProvider();
            if (pair.first) {
                ImGui::SameLine();
                const ImVec4 col = ImGui::ColorConvertU32ToFloat4(pair.second);
                ImGui::TextColored(col, "%s", pair.first);
            }
        }
        break;
    }
    case WidgetType::RenderCallback:
        if (w.renderCallback) w.renderCallback();
        break;
    }

    // Nothing to pop — chip gutter is managed by RenderContainer.

    // Inline color after the widget
    if (w.hasInlineColor) {
        if (w.inlineColor.colorCount > 0 && w.inlineColor.multiColors) {
            UI::ColorPickerInline(w.inlineColor.id.c_str(),
                                  w.inlineColor.multiColors,
                                  w.inlineColor.colorNames,
                                  w.inlineColor.colorCount);
        } else if (w.inlineColor.singleColor) {
            UI::ColorPickerInline(w.inlineColor.id.c_str(), *w.inlineColor.singleColor);
        }
    }

    // Cog + tooltip placement: pin to the label's actual draw position.
    // labelX = where the label starts, labelY = the label's top-left Y as
    // computed by each widget's own draw code. Cog sits at (nextSlotX, labelY)
    // — same Y as the label, moved right.
    const ImVec2 labelSize = ImGui::CalcTextSize(w.label.c_str());
    float labelX = widgetBasePos.x;
    float labelY = widgetBasePos.y;
    if (w.type == WidgetType::Checkbox) {
        // UI::Checkbox draws its label at textY, computed from box center
        // and font ascent. Mirror the same formula here so the cog lands
        // exactly at that Y.
        const float cbBox = S(10.0f);
        const float cbGap = S(7.0f);
        labelX += cbBox + cbGap;
        float rowH = std::max(labelSize.y, cbBox);
        float cy = std::floor(widgetBasePos.y + (rowH - cbBox) * 0.5f + 0.5f);
        float boxCenterY = cy + cbBox * 0.5f;
        ImFontBaked* baked = ImGui::GetFontBaked();
        float ascent = baked ? baked->Ascent : ImGui::GetFontSize();
        const float kLabelUpShift = S(2.0f);
        labelY = std::floor(boxCenterY - ascent * 0.5f - kLabelUpShift + 0.5f);
    } else if (w.type == WidgetType::ColorPicker) {
        // UI::ColorPicker draws its label at cy = base + (rowH-lineH)/2 - 1
        // (rowH = S(22)). Mirror that here.
        labelX += UI::NonCheckboxIndent();
        const float cpRowH = S(22.0f);
        float lineH = ImGui::GetTextLineHeight();
        labelY = widgetBasePos.y + (cpRowH - lineH) * 0.5f - 1.0f;
    } else {
        // Sliders / combos / inputs — label paints at widgetBasePos.y.
        labelX += UI::NonCheckboxIndent();
    }

    // Manual +2 px nudge so the cog + tooltip icons sit slightly below the
    // label's top edge — accounts for the FA glyph filling its em-box more
    // fully than the label text does, which otherwise reads as too high.
    labelY += S(2.0f);

    // Slot layout: label → (S(4) gap) → cog → (S(4) gap) → tooltip
    const float cogIconSz     = S(10.0f);
    const float tooltipIconSz = S(10.0f);
    const float slotGap       = S(4.0f);
    float nextSlotX = labelX + labelSize.x + slotGap;

    const bool hasCog = !w.settingsWidgets.empty() &&
                       (!w.settingsVisibleWhen || w.settingsVisibleWhen());

    // Settings cog — must be BEFORE EndGroup so OpenPopup is in the right
    // window context (same reason ColorPickerInline lives inside the group).
    if (hasCog) {
        // Cog Y = label's own draw Y (labelY is the exact top-left the
        // widget's label paints at — computed above per widget type).
        ImVec2 cogPos(nextSlotX, labelY);
        std::string cogKey = w.elementId.empty() ? ("cog." + w.label)
                                                 : ("cog." + w.elementId);

        // Row-hover test uses the group rect from THIS frame (matches the
        // previous behaviour — no group rect is available yet, so read the
        // last item's rect; close enough for animation gating).
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        ImVec2 mp = ImGui::GetIO().MousePos;
        bool rowHovered = mp.x >= rmin.x && mp.x <= rmax.x &&
                          mp.y >= rmin.y && mp.y <= rmax.y;
        const auto* widgets = &w.settingsWidgets;
        auto contentFn = [widgets]() {
            Builder::Get().RenderWidgetList(*widgets);
        };
        // Compute the max chip-block overflow across settings widgets so
        // the popup can grow to fit any keybind chips attached to them.
        // Without this, a chip on a widget inside the cog popup either
        // clips the popup or shrinks the widget.
        float popupChipGutter = 0.0f;
        for (auto& sw : w.settingsWidgets) {
            if (sw.elementId.empty()) continue;
            float ov = KeybindSystem::ChipBlockOverflowFor(
                sw.elementId, UI::NonCheckboxIndent());
            if (ov > popupChipGutter) popupChipGutter = ov;
        }
        UI::SettingsCog(cogKey.c_str(), w.label.c_str(), cogPos, cogIconSz,
                        contentFn, rowHovered, popupChipGutter);
        nextSlotX += cogIconSz + slotGap;
    }

    ImGui::EndGroup();

    // Tooltip marker — after EndGroup since it doesn't use popups. Sits in
    // the next slot after the cog (or right after the label if no cog).
    // Same Y as the label — moved right only.
    if (!w.tooltip.empty()) {
        ImVec2 iconPos(nextSlotX, labelY);
        std::string key = w.elementId.empty() ? ("tt." + w.label)
                                              : ("tt." + w.elementId);
        UI::TooltipMarker(key.c_str(), iconPos, tooltipIconSz, w.tooltip.c_str());
        nextSlotX += tooltipIconSz + slotGap;
    }

    // Keybind chips — text-only, right-aligned at the container column's
    // true right edge (origWidgetWidth, before the chip-gutter shrink).
    // The widget rendered inside the shrunk width, so its InvisibleButton
    // stops before the chip block — no click bleed.
    if (chipBlockN > 0) {
        float chipRowH = std::max(labelSize.y, S(16.0f));
        const float chipGap = S(6.0f);

        // Precompute widths so we can right-align the block as a unit.
        const auto& all = KeybindSystem::GetAllKeybinds();
        float totalW = 0.0f;
        float widths[8] = {};
        for (int b = 0; b < chipBlockN && b < 8; b++) {
            int gi = KeybindSystem::IndexOfBind(w.elementId, b);
            if (gi < 0) continue;
            const auto& kb = all[gi];
            bool editingKey =
                (KeybindSystem::GetEditingBindIndex() == gi &&
                 KeybindSystem::GetEditPhase() ==
                     KeybindSystem::ChipPhase::WaitingForKey);
            std::string inside;
            if (editingKey)      inside = "...";
            else if (kb.key > 0) inside = KeybindSystem::GetKeyName(kb.key);
            else                 inside = "-";
            char text[48];
            snprintf(text, sizeof(text), "[%s]", inside.c_str());
            // Match the chip's smaller render font (85% of body text) so the
            // reserved gutter matches what actually gets drawn.
            ImFont* cf = ImGui::GetFont();
            float   cfs = ImGui::GetFontSize() * 0.85f;
            widths[b] = cf->CalcTextSizeA(cfs, FLT_MAX, 0.0f, text).x;
            totalW += widths[b];
            if (b > 0) totalW += chipGap;
        }

        // Container's true inner right edge — widget's usable width plus
        // the reserved chip gutter for THIS widget's overflow. Chip block
        // ends at this edge (which lies inside the reserved gutter).
        float rightEdge = widgetBasePos.x + origWidgetWidth + chipOverflow;
        float x = rightEdge - totalW;
        for (int b = 0; b < chipBlockN && b < 8; b++) {
            float drawn = UI::KeybindChip(w.elementId.c_str(), b,
                                          ImVec2(x, labelY), chipRowH);
            if (drawn > 0.0f) x += drawn + chipGap;
        }
    }
}

void Builder::RenderWidgetList(const std::vector<WidgetDef>& widgets) {
    for (auto& w : widgets) {
        if (w.visibleWhen && !w.visibleWhen())
            continue;
        RenderWidget(w);
    }
}

void Builder::RenderContainer(const ContainerDef& container, float colW,
                              const std::string& parentScope) {
    std::string persistKey = parentScope + "." + container.name;
    if (UI::BeginContainer(container.name.c_str(), colW, persistKey.c_str())) {
        // Compute the chip-gutter overflow for THIS container only — the
        // largest excess over natural NonCheckboxIndent among widgets in
        // this container that have binds. Zero if none of this container's
        // widgets have binds (or their chips fit in the natural margin).
        // Only that container reserves the gutter; other containers stay
        // untouched.
        float chipGutter = 0.0f;
        for (auto& w : container.widgets) {
            if (w.elementId.empty()) continue;
            float ov = KeybindSystem::ChipBlockOverflowFor(
                w.elementId, UI::NonCheckboxIndent());
            if (ov > chipGutter) chipGutter = ov;
        }
        if (chipGutter > 0.0f) {
            UI::PushWidgetWidth(UI::GetWidgetWidth() - chipGutter);
        }
        for (auto& w : container.widgets) {
            if (w.visibleWhen && !w.visibleWhen())
                continue; // hidden — skip rendering entirely (does not reserve layout space)
            RenderWidget(w);
        }
        if (chipGutter > 0.0f) UI::PopWidgetWidth();
    }
    UI::EndContainer();
}

// ─── Local animation helpers (selector strip only) ─────────────────────────
//
// menu_builder.cpp can't reach the file-static Animate/LerpColor that live
// in ui_components.cpp, so we mirror the lerp pattern locally. State is
// keyed by ImGuiID so each selector cell animates independently — no risk
// of two selectors fighting over the same float.
static std::unordered_map<ImGuiID, float> s_selAnim;

static float SelAnimate(ImGuiID id, float target, float speed = 16.0f) {
    float dt = ImGui::GetIO().DeltaTime;
    auto it = s_selAnim.find(id);
    if (it == s_selAnim.end()) { s_selAnim[id] = 0.0f; return 0.0f; }
    float& cur = it->second;
    cur += (target - cur) * std::min(speed * dt, 1.0f);
    if (std::abs(target - cur) < 0.005f) cur = target;
    return cur;
}

static ImU32 SelLerpColor(ImU32 a, ImU32 b, float t) {
    int ra = (a >> 0) & 0xFF, ga = (a >> 8) & 0xFF, ba = (a >> 16) & 0xFF, aa = (a >> 24) & 0xFF;
    int rb = (b >> 0) & 0xFF, gb = (b >> 8) & 0xFF, bb = (b >> 16) & 0xFF, ab = (b >> 24) & 0xFF;
    return IM_COL32(
        (int)(ra + (rb - ra) * t),
        (int)(ga + (gb - ga) * t),
        (int)(ba + (bb - ba) * t),
        (int)(aa + (ab - aa) * t));
}

// ─── Icon selector strip ────────────────────────────────────────────────────
//
// Full-width tab strip drawn above the subtab's container columns. Each
// option = a square button with the FA icon centered on top and the label
// beneath it. Selected option lerps to accent + drops a thin accent
// underline; unselected options stay muted. Click changes *def.selectedIndex.
//
// Layout: the strip spans the same width as the two-column container area
// (contentW - 2*kContentPad), evenly divided per option. Height is fixed at
// S(56.0f) so the icon + label pair stays readable across DPI scales.
static void RenderIconSelectorStrip(const IconSelectorDef& def,
                                    float originX, float originY,
                                    float stripW)
{
    if (!def.selectedIndex || def.options.empty()) return;
    const int n = (int)def.options.size();
    const float stripH = S(40.0f);
    const float optW = stripW / (float)n;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 mutedCol  = MenuStyle::Colors::TextMuted.u32();
    ImU32 accentCol = MenuStyle::Colors::Accent.u32();
    ImU32 border    = MenuStyle::Colors::Border.u32();

    // Strip surface = container body — flat PanelBg (MenuDark) fill +
    // Border outline. Reads as part of the container chrome family, not
    // as its own widget-language frame.
    ImVec2 stripMin(originX,          originY);
    ImVec2 stripMax(originX + stripW, originY + stripH);
    dl->AddRectFilled(stripMin, stripMax,
                      MenuStyle::Colors::PanelBg.u32());
    dl->AddRect(stripMin, stripMax, border, 0.0f, 0, 1.0f);

    for (int i = 0; i < n; i++) {
        const auto& opt = def.options[i];
        float optX = originX + optW * (float)i;
        float optY = originY;

        // Invisible button per cell. PushID(i) so adjacent cells don't share
        // ImGui IDs across same-label selectors.
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(ImVec2(optX, optY));
        bool clicked = ImGui::InvisibleButton("##icnsel_opt",
                                              ImVec2(optW, stripH));
        ImGuiID id = ImGui::GetItemID();

        bool selected = (*def.selectedIndex == i);
        // Smooth lerp for both selection and hover. Independent values so
        // a hover doesn't visually displace a selection underline.
        float selAnim = SelAnimate(id, selected ? 1.0f : 0.0f, 22.0f);
        float hovAnim = SelAnimate(id ^ 0xA5A5u,
                                   ImGui::IsItemHovered() ? 1.0f : 0.0f,
                                   18.0f);

        // Hover wash — dark film when not selected. Zero rounding to
        // match the flat strip. Top/bottom inset 1 px so the strip's
        // outer border stays visible; horizontal inset only on the OUTER
        // edge of the first / last cells so adjacent cells butt up flush.
        //
        // Gate on `!selected` (not on selAnim) so the wash disappears the
        // very next frame after a click. Using selAnim < threshold would
        // let the hover wash linger through the entire selection ramp-up
        // and read as a sluggish-feeling click.
        if (hovAnim > 0.005f && !selected) {
            ImU32 hoverBg = MenuStyle::Colors::ShadowInk
                                .alpha(hovAnim * (40.0f / 255.0f)).u32();
            float leftIn  = (i == 0)     ? S(1.0f) : 0.0f;
            float rightIn = (i == n - 1) ? S(1.0f) : 0.0f;
            dl->AddRectFilled(ImVec2(optX + leftIn,         optY + S(1.0f)),
                              ImVec2(optX + optW - rightIn, optY + stripH - S(1.0f)),
                              hoverBg);
        }

        // Selected cell gets a bottom-up gradient overlay in the Border
        // tone (matching the strip's outline color) plus a 1 px Accent
        // underline flush against the cell bottom. This is what indicates
        // the active selection now — the icon-only layout dropped the
        // text label that used to serve that role.
        if (selAnim > 0.005f) {
            // Peak opacity ~25% so the fill stays subtle and reads as a
            // "raised" cell rather than a heavy panel swap.
            const float kSelPeakAlpha = 0.25f;
            ImU32 gradTop = MenuStyle::Colors::Border.alpha(0.0f).u32();
            ImU32 gradBot = MenuStyle::Colors::Border.alpha(selAnim * kSelPeakAlpha).u32();
            float leftIn  = (i == 0)     ? S(1.0f) : 0.0f;
            float rightIn = (i == n - 1) ? S(1.0f) : 0.0f;
            ImVec2 fmin(optX + leftIn,         optY + S(1.0f));
            ImVec2 fmax(optX + optW - rightIn, optY + stripH - S(1.0f));
            // Bottom-up: transparent at top, Border at bottom.
            dl->AddRectFilledMultiColor(fmin, fmax,
                                         gradTop, gradTop, gradBot, gradBot);
            // 1 px Accent bar flush against the cell's bottom inside edge.
            ImU32 accentBar = MenuStyle::Colors::Accent.alpha(selAnim).u32();
            dl->AddRectFilled(ImVec2(fmin.x, fmax.y - 1.0f),
                              ImVec2(fmax.x, fmax.y),
                              accentBar);
        }

        // Icon — vertically centered in the cell. Idle = dim (Border tone,
        // reads as chrome); selected = Accent. Two render paths remain:
        //   - Font Awesome glyph  (opt.image == nullptr && opt.icon non-empty)
        //   - Texture (atlas/UV)  (opt.image != nullptr)
        // Label rendering is gone — this widget is icon-only now.
        float iconBoxCY = optY + stripH * 0.5f;

        ImU32 iconIdle = MenuStyle::Colors::Border.u32();
        ImU32 iconCol  = SelLerpColor(iconIdle, accentCol, selAnim);

        if (opt.image) {
            // Texture path. Width derived from the UV-rect's aspect to
            // avoid squashing; clamped to the cell width.
            float uvW = std::abs(opt.uv1.x - opt.uv0.x);
            float uvH = std::abs(opt.uv1.y - opt.uv0.y);
            float aspect = (uvH > 0.0f) ? (uvW / uvH) : 1.0f;
            float drawH = S(18.0f);
            float drawW = drawH * aspect;
            float maxW = optW - S(8.0f);
            if (drawW > maxW) { drawW = maxW; drawH = drawW / (aspect > 0.001f ? aspect : 1.0f); }
            float ix = std::floor(optX + (optW - drawW) * 0.5f + 0.5f);
            float iy = std::floor(iconBoxCY - drawH * 0.5f + 0.5f);
            auto mulCol = [](ImU32 a, ImU32 b) -> ImU32 {
                int ar = (a >> 0) & 0xFF, ag = (a >> 8) & 0xFF, ab = (a >> 16) & 0xFF, aa = (a >> 24) & 0xFF;
                int br = (b >> 0) & 0xFF, bg = (b >> 8) & 0xFF, bb = (b >> 16) & 0xFF, ba = (b >> 24) & 0xFF;
                return IM_COL32((ar * br) / 255, (ag * bg) / 255,
                                (ab * bb) / 255, (aa * ba) / 255);
            };
            ImU32 imgCol = mulCol(iconCol, opt.imageTint);
            dl->AddImage(opt.image,
                         ImVec2(ix, iy),
                         ImVec2(ix + drawW, iy + drawH),
                         opt.uv0, opt.uv1, imgCol);
        } else if (MenuStyle::g_fontIcon && !opt.icon.empty()) {
            // Fixed size — icon doesn't grow/shrink on selection. The
            // color lerp + bottom-up gradient + accent underline already
            // signal the active cell.
            float iconFs    = S(16.0f);
            ImVec2 sz = MenuStyle::g_fontIcon->CalcTextSizeA(iconFs, FLT_MAX, 0.0f, opt.icon.c_str());
            float ix = std::floor(optX + (optW - sz.x) * 0.5f + 0.5f);
            float iy = std::floor(iconBoxCY - sz.y * 0.5f + 0.5f);
            // No TextShadow — the 55% black shadow drop-behind darkens
            // dim-tone glyphs (like Border, 31/31/31) enough that they
            // read visibly darker than the actual iconCol. Straight
            // AddText keeps the pixel color true to Border.
            dl->AddText(MenuStyle::g_fontIcon, iconFs,
                        ImVec2(ix, iy), iconCol, opt.icon.c_str());
        }

        if (clicked) {
            *def.selectedIndex = i;
            // Don't call UI::ResetAnimations here — it nukes ALL animation
            // state across the whole menu (tabs, buttons, etc.), causing a
            // visible flash on unrelated widgets. The per-cell SelAnimate
            // handles transitions cleanly on its own.
        }

        ImGui::PopID();
    }
}

void Builder::RenderSubTab(const SubTabDef& sub, float contentW,
                           const std::string& parentScope) {
    std::string subScope = parentScope + "." + sub.name;
    // Outer padding (window edge → content) already lives at the menu.cpp
    // Render() level as outerPad, so this function no longer needs to add
    // its own top/left inset. Containers now start flush at (0,0) inside
    // the content child, keeping their top edge in line with the sidebar
    // box across the divider. Only kColumnGap is kept for the between-
    // column breathing space.
    float kColumnGap = S(14.0f);
    float colW = (contentW - kColumnGap) * 0.5f;

    // ── Icon selector strip (optional) ──────────────────────────────────
    //
    // Rendered ABOVE the columns when the subtab has one. We track its Y
    // footprint so the cursor lands just below it when the columns start.
    float topPad = 0.0f;
    if (sub.hasSelector && sub.selector.selectedIndex && !sub.selector.options.empty()) {
        ImVec2 origin = ImGui::GetCursorScreenPos();
        float stripW  = contentW;
        float stripH  = S(40.0f); // keep in sync with RenderIconSelectorStrip
        float stripGap = S(14.0f);
        RenderIconSelectorStrip(sub.selector, origin.x, origin.y, stripW);
        topPad = stripH + stripGap;
    }

    // Filter helper — a container's visibility against the active selector
    // slot. -1 == always; otherwise must match the selector's current
    // index. Subtabs without a selector ignore selectorSlot entirely.
    int activeSlot = (sub.hasSelector && sub.selector.selectedIndex)
                   ? *sub.selector.selectedIndex : -1;
    auto containerVisible = [&](const ContainerDef& c) -> bool {
        if (!sub.hasSelector) return true;
        if (c.selectorSlots.empty()) return true;
        for (int s : c.selectorSlots)
            if (s == activeSlot) return true;
        return false;
    };

    // Separate containers by column assignment (filtered against selector
    // and against the container's own visibleWhen predicate, when set).
    std::vector<const ContainerDef*> leftCols, rightCols, autoCols;
    for (auto& c : sub.containers) {
        if (!containerVisible(c)) continue;
        if (c.visibleWhen && !c.visibleWhen()) continue;
        if (c.column == 0)      leftCols.push_back(&c);
        else if (c.column == 1) rightCols.push_back(&c);
        else                    autoCols.push_back(&c);
    }

    // Auto-balance: assign to shorter column
    float leftH = 0, rightH = 0;
    for (auto* c : leftCols)  leftH += EstimateContainerHeight(*c);
    for (auto* c : rightCols) rightH += EstimateContainerHeight(*c);

    for (auto* c : autoCols) {
        if (leftH <= rightH) {
            leftCols.push_back(c);
            leftH += EstimateContainerHeight(*c);
        } else {
            rightCols.push_back(c);
            rightH += EstimateContainerHeight(*c);
        }
    }

    ImGui::SetCursorPos(ImVec2(0.0f, topPad));

    // Left column
    ImGui::BeginGroup();
    for (auto* c : leftCols)
        RenderContainer(*c, colW, subScope);
    ImGui::EndGroup();

    if (!rightCols.empty()) {
        ImGui::SameLine(0, kColumnGap);

        // Right column
        ImGui::BeginGroup();
        for (auto* c : rightCols)
            RenderContainer(*c, colW, subScope);
        ImGui::EndGroup();
    }

    // No trailing Dummy needed: EndGroup() already includes the per-container trailing
    // S(14) gap (added inside UI::EndContainer) in the group's bounding box, so
    // CursorMaxPos.y after EndGroup sits exactly kContentPad below the last container.
    // A Dummy(0,0) here would actually push ContentSize.y up by ItemSpacing.y (6 px from
    // menu_style) because EndGroup advances the cursor past the group by ItemSpacing.y,
    // and the next item commits that position into CursorMaxPos.y — making the bottom
    // gap visibly larger than the side/inter-container padding when scrolled to max.
}

void Builder::Render(float contentW) {
    if (m_currentTab < 0 || m_currentTab >= (int)m_tabs.size()) return;
    auto& tab = m_tabs[m_currentTab];
    int subIdx = GetCurrentSubTab();
    if (subIdx < 0 || subIdx >= (int)tab.subtabs.size()) return;
    RenderSubTab(tab.subtabs[subIdx], contentW, tab.name);
}

void Builder::RenderTopSubTabs(float menuW, float /*barH*/) {
    if (m_currentTab < 0 || m_currentTab >= (int)m_tabs.size()) return;
    auto& tab = m_tabs[m_currentTab];
    int subCount = (int)tab.subtabs.size();
    // Hide bar entirely when only one (or zero) subtab in the current tab
    if (subCount < 2) return;

    int& sel = m_sidebarIndex[m_currentTab];
    static int s_prevSubTabSel = -1;

    // Distribute full width evenly; last tab absorbs any rounding remainder
    float tabW = std::floor(menuW / (float)subCount);
    float lastW = menuW - tabW * (float)(subCount - 1);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

    for (int i = 0; i < subCount; i++) {
        if (i > 0) ImGui::SameLine(0, 0);
        float w = (i == subCount - 1) ? lastW : tabW;
        if (UI::TopSubTab(tab.subtabs[i].name.c_str(),
                          tab.subtabs[i].icon.c_str(),
                          sel == i, w))
            sel = i;
    }

    // Underline indicator removed — selected state is shown via fade bg + icon/text color.

    if (sel != s_prevSubTabSel) {
        UI::ResetAnimations();
        s_prevSubTabSel = sel;
    }

    ImGui::PopStyleVar();
}

// ─── Config capture/apply ───────────────────────────────────────────────────

Config Builder::CaptureConfig() const {
    Config cfg;

    // Single-widget capture. Returns nothing — the per-type switch either
    // stores into cfg.values keyed by elementId or bails out for
    // non-persistable widgets (buttons, separators, render callbacks,
    // transient flags, unbound pointers). Pulled out so we can call it
    // for BOTH top-level container widgets AND widgets nested inside a
    // settings-cog popup (settingsWidgets) — without recursion, every
    // value behind a .BeginSettings()/.EndSettings() block silently
    // never makes it to disk and ApplyConfig falls back to struct
    // defaults on load.
    auto captureOne = [&](const WidgetDef& w) {
        if (w.elementId.empty() || w.transient) return;
        ConfigValue v;
        switch (w.type) {
        case WidgetType::Checkbox:
            if (!w.boolPtr) return;
            v.type = ConfigValue::Type::Bool;
            v.b = *w.boolPtr;
            break;
        case WidgetType::SliderFloat:
            if (!w.floatPtr) return;
            v.type = ConfigValue::Type::Float;
            v.f = *w.floatPtr;
            break;
        case WidgetType::SliderInt:
            if (!w.intPtr) return;
            v.type = ConfigValue::Type::Int;
            v.i = *w.intPtr;
            break;
        case WidgetType::Combo:
            if (!w.intPtr) return;
            v.type = ConfigValue::Type::Int;
            v.i = *w.intPtr;
            break;
        case WidgetType::MultiCombo:
            if (!w.multiSelected || w.itemsCount <= 0) return;
            v.type = ConfigValue::Type::MultiBool;
            v.multi.assign(w.itemsCount, false);
            for (int i = 0; i < w.itemsCount; i++) v.multi[i] = w.multiSelected[i];
            break;
        case WidgetType::ListBox:
        case WidgetType::SearchableListBox:
            if (!w.intPtr) return;
            v.type = ConfigValue::Type::Int;
            v.i = *w.intPtr;
            break;
        case WidgetType::ColorPicker:
            if (!w.floatPtr) return;
            v.type = ConfigValue::Type::Color;
            for (int i = 0; i < 4; i++) v.color[i] = w.floatPtr[i];
            break;
        case WidgetType::InputText:
            if (!w.textBuf) return;
            v.type = ConfigValue::Type::Text;
            v.text = w.textBuf;
            break;
        case WidgetType::Button:
        case WidgetType::Separator:
        case WidgetType::StatusText:
        case WidgetType::RenderCallback:
            return; // no value to capture
        }
        cfg.values[w.elementId] = std::move(v);
    };

    for (auto& tab : m_tabs) {
        for (auto& sub : tab.subtabs) {
            for (auto& cont : sub.containers) {
                for (auto& w : cont.widgets) {
                    captureOne(w);
                    // Settings-cog popup widgets — same flat element-id
                    // namespace as the top-level widgets, so they round-
                    // trip via cfg.values without any nesting in storage.
                    for (auto& sw : w.settingsWidgets) captureOne(sw);
                    // Per-listbox-item settings cogs — one popup per item.
                    for (auto& kv : w.itemSettings)
                        for (auto& iw : kv.second) captureOne(iw);
                }
            }
        }
    }
    cfg.collapsedContainers = UI::GetCollapseStates();
    return cfg;
}

namespace {
// Linear lookup helper — the widget tree is small enough that a hash index
// would add more upkeep cost than it saves. Returns the first matching def
// (elementId collisions should be impossible by construction).
const WidgetDef* FindWidget(const std::vector<TabDef>& tabs, const char* elementId) {
    if (!elementId || !*elementId) return nullptr;
    for (const auto& tab : tabs) {
        for (const auto& sub : tab.subtabs) {
            for (const auto& cont : sub.containers) {
                for (const auto& w : cont.widgets) {
                    if (w.elementId == elementId) return &w;
                    for (const auto& sw : w.settingsWidgets)
                        if (sw.elementId == elementId) return &sw;
                    for (const auto& kv : w.itemSettings)
                        for (const auto& iw : kv.second)
                            if (iw.elementId == elementId) return &iw;
                }
            }
        }
    }
    return nullptr;
}
} // namespace

bool Builder::GetBool(const char* elementId, bool fallback) const {
    const WidgetDef* w = FindWidget(m_tabs, elementId);
    if (!w || !w->boolPtr) return fallback;
    return *w->boolPtr;
}

int Builder::GetInt(const char* elementId, int fallback) const {
    const WidgetDef* w = FindWidget(m_tabs, elementId);
    if (!w || !w->intPtr) return fallback;
    return *w->intPtr;
}

float Builder::GetFloat(const char* elementId, float fallback) const {
    const WidgetDef* w = FindWidget(m_tabs, elementId);
    if (!w || !w->floatPtr) return fallback;
    return *w->floatPtr;
}

void Builder::UpdateListItems(const char* elementId, const char* const* items, int count) {
    if (!elementId) return;
    for (auto& tab : m_tabs) {
        for (auto& sub : tab.subtabs) {
            for (auto& cont : sub.containers) {
                for (auto& w : cont.widgets) {
                    if (w.elementId == elementId &&
                        (w.type == WidgetType::ListBox ||
                         w.type == WidgetType::SearchableListBox ||
                         w.type == WidgetType::Combo ||
                         w.type == WidgetType::MultiCombo)) {
                        w.items = items;
                        w.itemsCount = count;
                    }
                }
            }
        }
    }
}

void Builder::ApplyConfig(const Config& cfg) {
    // Pre-register every widget's live variable pointer with the keybind
    // system BEFORE we let any bound keybind try to apply. RegisterVar
    // normally fires from the widget's render path, so a bind loaded from
    // config for a widget the user hasn't visited yet has no pointer in
    // s_varPtrs and ProcessKeybinds silently skips it — the bind's key
    // shows up in the list but the value never sticks until the user
    // scrolls to the widget's tab and paints it once. Walking the tree
    // here fixes the load path (widgets in unrelated tabs still get
    // pointers registered) without changing the render-path
    // registration, which stays as a safety net for dynamically added
    // widgets.
    auto registerOne = [&](WidgetDef& w) {
        if (w.elementId.empty()) return;
        void* ptr = nullptr;
        switch (w.type) {
        case WidgetType::Checkbox:    ptr = w.boolPtr; break;
        case WidgetType::SliderFloat: ptr = w.floatPtr; break;
        case WidgetType::SliderInt:
        case WidgetType::Combo:       ptr = w.intPtr; break;
        case WidgetType::MultiCombo:  ptr = w.multiSelected; break;
        default: return; // no keybind-eligible pointer
        }
        if (ptr) KeybindSystem::RegisterVar(w.elementId, ptr);
    };
    for (auto& tab : m_tabs) {
        for (auto& sub : tab.subtabs) {
            for (auto& cont : sub.containers) {
                for (auto& w : cont.widgets) {
                    registerOne(w);
                    for (auto& sw : w.settingsWidgets) registerOne(sw);
                    for (auto& kv : w.itemSettings)
                        for (auto& iw : kv.second) registerOne(iw);
                }
            }
        }
    }

    // Mirror of CaptureConfig's captureOne — single-widget restore from
    // the cfg snapshot. Pulled out so we can apply it to both top-level
    // container widgets AND settings-cog popup widgets. Without the
    // settingsWidgets pass, every value behind .BeginSettings()/
    // .EndSettings() reverts to its struct default on load because no
    // assignment is ever made to its bound pointer.
    auto applyOne = [&](WidgetDef& w) {
        if (w.elementId.empty() || w.transient) return;
        auto it = cfg.values.find(w.elementId);
        if (it == cfg.values.end()) return;
        const ConfigValue& v = it->second;
        switch (w.type) {
        case WidgetType::Checkbox:
            if (w.boolPtr && v.type == ConfigValue::Type::Bool) *w.boolPtr = v.b;
            break;
        case WidgetType::SliderFloat:
            if (w.floatPtr && v.type == ConfigValue::Type::Float) *w.floatPtr = v.f;
            break;
        case WidgetType::SliderInt:
        case WidgetType::Combo:
        case WidgetType::ListBox:
        case WidgetType::SearchableListBox:
            if (w.intPtr && v.type == ConfigValue::Type::Int) *w.intPtr = v.i;
            break;
        case WidgetType::MultiCombo:
            if (w.multiSelected && v.type == ConfigValue::Type::MultiBool) {
                int n = (int)v.multi.size();
                if (n > w.itemsCount) n = w.itemsCount;
                for (int i = 0; i < n; i++) w.multiSelected[i] = v.multi[i];
            }
            break;
        case WidgetType::ColorPicker:
            if (w.floatPtr && v.type == ConfigValue::Type::Color) {
                for (int i = 0; i < 4; i++) w.floatPtr[i] = v.color[i];
            }
            break;
        case WidgetType::InputText:
            if (w.textBuf && w.textBufSize > 0 && v.type == ConfigValue::Type::Text) {
                size_t n = v.text.size();
                if (n >= w.textBufSize) n = w.textBufSize - 1;
                for (size_t i = 0; i < n; i++) w.textBuf[i] = v.text[i];
                w.textBuf[n] = '\0';
            }
            break;
        case WidgetType::Button:
        case WidgetType::Separator:
        case WidgetType::StatusText:
        case WidgetType::RenderCallback:
            break;
        }
    };

    for (auto& tab : m_tabs) {
        for (auto& sub : tab.subtabs) {
            for (auto& cont : sub.containers) {
                for (auto& w : cont.widgets) {
                    applyOne(w);
                    for (auto& sw : w.settingsWidgets) applyOne(sw);
                    for (auto& kv : w.itemSettings)
                        for (auto& iw : kv.second) applyOne(iw);
                }
            }
        }
    }

    // Restore per-container collapsed state. Empty map = no entries → no-op.
    UI::ApplyCollapseStates(cfg.collapsedContainers);

    // Any keybind whose base was cached pre-config now points at a stale value.
    // Flag entries for recapture so the next ProcessKeybinds tick re-syncs from
    // the freshly-loaded widget values.
    KeybindSystem::OnConfigApplied();
}

void Builder::RenderBottomTabs(float menuW, float tabBarH) {
    int tabCount = (int)m_tabs.size();
    if (tabCount == 0) return;

    float tabsTotalW = menuW * 0.45f;
    float tabW = tabsTotalW / tabCount;
    float tabsOffsetX = (menuW - tabsTotalW) * 0.5f;

    ImGui::SameLine(tabsOffsetX);
    for (int i = 0; i < tabCount; i++) {
        if (i > 0) ImGui::SameLine(0, 0);
        if (UI::BottomTab(m_tabs[i].name.c_str(), m_tabs[i].icon.c_str(),
                          m_currentTab == i, tabW)) {
            if (m_currentTab != i) {
                m_currentTab = i;
                UI::ResetAnimations();
            }
        }
    }

    // Underline indicator removed — selection is shown via gradient bg + icon scale/position.
}

void Builder::RenderSubtabSidebar() {
    if (m_currentTab < 0 || m_currentTab >= (int)m_tabs.size()) return;
    auto& tab = m_tabs[m_currentTab];
    int& sel = m_sidebarIndex[m_currentTab];
    static int s_prevSubTabSel = -1;
    int subCount = (int)tab.subtabs.size();
    // Zero vertical ItemSpacing so subtabs stack flush against each other
    // (the SidebarTab widget bakes its own padding into its row height).
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    for (int i = 0; i < subCount; i++) {
        if (UI::SidebarTab(tab.subtabs[i].name.c_str(), sel == i)) {
            sel = i;
        }
    }
    ImGui::PopStyleVar();
    if (sel != s_prevSubTabSel) {
        UI::ResetAnimations();
        s_prevSubTabSel = sel;
    }
}

float Builder::RenderTopTabs(float stripH, float labelPad) {
    int tabCount = (int)m_tabs.size();
    if (tabCount == 0) return 0.0f;

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImFont* boldFont = MenuStyle::g_fontBold ? MenuStyle::g_fontBold : ImGui::GetFont();
    float fontSize = ImGui::GetFontSize();

    // Tabs are 3/4 of the strip's height (half the strip + half of that
    // half again) and anchored to the strip's bottom so the selected-tab
    // MenuLight fill can visually merge with the content below by
    // covering the strip's border line.
    float tabH = stripH * 0.75f;
    float tabY = origin.y + (stripH - tabH);

    // Open-in animation — slide the tab row up from below the strip
    // (Y = tabY + tabH → tabY) so tabs appear to emerge from beneath the
    // title bar. Also passed through to TopTextTab so the selected tab's
    // fill can independently grow from its bottom edge.
    float openAnim = Menu::GetOpenAnim();
    float slideOff = (1.0f - openAnim) * tabH;
    tabY += slideOff;

    float x = origin.x;
    for (int i = 0; i < tabCount; i++) {
        const char* label = m_tabs[i].name.c_str();
        ImVec2 sz = boldFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
        float w = std::ceil(sz.x + labelPad * 2.0f);
        ImGui::SetCursorScreenPos(ImVec2(x, tabY));
        // Every tab rounds both top corners; the accent bar drawn inside
        // TopTextTab uses the same corner flags so it follows the curve.
        ImDrawFlags corners = ImDrawFlags_RoundCornersTop;
        if (UI::TopTextTab(label, m_currentTab == i, w, tabH, corners,
                           openAnim)) {
            if (m_currentTab != i) {
                m_currentTab = i;
                UI::ResetAnimations();
            }
        }
        x += w;
    }
    return x - origin.x;
}

void Builder::RenderSideTabs(float railW, float tabH) {
    int tabCount = (int)m_tabs.size();
    if (tabCount == 0) return;

    ImVec2 origin = ImGui::GetCursorScreenPos();
    for (int i = 0; i < tabCount; i++) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + i * tabH));
        if (UI::SideTab(m_tabs[i].name.c_str(), m_tabs[i].icon.c_str(),
                        m_currentTab == i, railW, tabH)) {
            if (m_currentTab != i) {
                m_currentTab = i;
                UI::ResetAnimations();
            }
        }
    }
}

} // namespace MenuBuilder
