#include "debug_window.h"
#include "menu_style.h"
#include "config_system.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>
#include <string>

#define S(x) (MenuStyle::S(x))

namespace DebugWindow {

struct Entry {
    std::string name;
    std::string value;
};

struct Instance {
    std::string        key;
    std::string        title;
    bool               visible = false;
    std::vector<Entry> entries;

    // Frame-local animation state so hide→show plays the expand-in effect
    // starting from the title-bar-only baseline rather than from 0.
    float animHeight   = 0.0f;
    float targetHeight = 0.0f;
};

// Insertion-ordered set of instances so RenderAll draws them in a stable
// order (also matches how the config system saves per-window positions).
static std::vector<std::string>                    s_order;
static std::unordered_map<std::string, Instance>   s_windows;

static Instance& EnsureInstance(const char* key, const char* title) {
    std::string k = key ? key : "";
    auto it = s_windows.find(k);
    if (it == s_windows.end()) {
        Instance inst;
        inst.key   = k;
        inst.title = (title && title[0]) ? title : k.c_str();
        s_windows[k] = std::move(inst);
        s_order.push_back(k);
        it = s_windows.find(k);
    }
    if (title && title[0]) it->second.title = title;
    return it->second;
}

static Instance* FindInstance(const char* key) {
    if (!key) key = "";
    auto it = s_windows.find(std::string(key));
    return it == s_windows.end() ? nullptr : &it->second;
}

static Entry* FindEntry(Instance& inst, const char* name) {
    for (auto& e : inst.entries) if (e.name == name) return &e;
    return nullptr;
}

static void SetInternal(Instance& inst, const char* name, std::string value) {
    if (Entry* e = FindEntry(inst, name)) {
        e->value = std::move(value);
    } else {
        inst.entries.push_back({ std::string(name ? name : ""), std::move(value) });
    }
}

// ── Multi-instance API ────────────────────────────────────────────────

void EnsureCreated(const char* key, const char* title) {
    EnsureInstance(key, title);
}

void Destroy(const char* key) {
    std::string k = key ? key : "";
    s_windows.erase(k);
    s_order.erase(std::remove(s_order.begin(), s_order.end(), k), s_order.end());
}

void Set(const char* key, const char* title, const char* name, const char* value) {
    SetInternal(EnsureInstance(key, title), name, std::string(value ? value : ""));
}
void Set(const char* key, const char* title, const char* name, const std::string& value) {
    SetInternal(EnsureInstance(key, title), name, value);
}
void Set(const char* key, const char* title, const char* name, int value) {
    char buf[32]; std::snprintf(buf, sizeof(buf), "%d", value);
    SetInternal(EnsureInstance(key, title), name, std::string(buf));
}
void Set(const char* key, const char* title, const char* name, float value, int decimals) {
    if (decimals < 0) decimals = 0;
    if (decimals > 9) decimals = 9;
    char fmt[16]; std::snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
    char buf[64]; std::snprintf(buf, sizeof(buf), fmt, value);
    SetInternal(EnsureInstance(key, title), name, std::string(buf));
}
void Set(const char* key, const char* title, const char* name, bool value) {
    SetInternal(EnsureInstance(key, title), name, std::string(value ? "true" : "false"));
}

void Remove(const char* key, const char* name) {
    Instance* inst = FindInstance(key);
    if (!inst || !name) return;
    inst->entries.erase(
        std::remove_if(inst->entries.begin(), inst->entries.end(),
            [name](const Entry& e) { return e.name == name; }),
        inst->entries.end());
}

void Clear(const char* key) {
    if (Instance* inst = FindInstance(key)) inst->entries.clear();
}

void SetVisible(const char* key, bool v) {
    if (Instance* inst = FindInstance(key)) inst->visible = v;
}
bool IsVisible(const char* key) {
    Instance* inst = FindInstance(key);
    return inst ? inst->visible : false;
}
bool* GetVisiblePtr(const char* key, const char* title) {
    return &EnsureInstance(key, title).visible;
}
void Toggle(const char* key) {
    if (Instance* inst = FindInstance(key)) inst->visible = !inst->visible;
}
bool HasEntries(const char* key) {
    Instance* inst = FindInstance(key);
    return inst && !inst->entries.empty();
}

// ── Per-instance render helper ────────────────────────────────────────

static void RenderInstance(Instance& inst, bool menuVisible) {
    const float titleBarH  = S(MenuStyle::Panel::TitleBarH);
    const float sepGap     = S(5.0f);
    const float titleOnlyH = titleBarH + sepGap + S(4.0f) + S(6.0f);

    // Off → keep baseline so the next "on" expands from the title bar.
    if (!inst.visible) {
        inst.animHeight   = titleOnlyH;
        inst.targetHeight = titleOnlyH;
        return;
    }
    // On but empty and the menu isn't visible: collapse to nothing so we
    // don't leave a title bar floating mid-game with no data.
    if (inst.entries.empty() && !menuVisible) {
        inst.animHeight   = titleOnlyH;
        inst.targetHeight = titleOnlyH;
        return;
    }

    // Auto-fit width to the widest row (name + value or the title bar)
    // with a lower bound so the title / accent cap always look right.
    const float minWidth = S(180.0f);
    const float sidePad  = S(8.0f);   // matches row / title left+right padding
    const float rowGap   = S(12.0f);  // gap between "name:" and value on same row
    ImVec2 titleSize = ImGui::CalcTextSize(inst.title.c_str());
    // Reserve room for the bug icon on the right of the title bar.
    float titleReserve = titleSize.x + sidePad * 2.0f + S(20.0f);
    float widestRow = titleReserve;
    for (auto& e : inst.entries) {
        char labelBuf[256];
        std::snprintf(labelBuf, sizeof(labelBuf), "%s:", e.name.c_str());
        float lblW = ImGui::CalcTextSize(labelBuf).x;
        float valW = ImGui::CalcTextSize(e.value.c_str()).x;
        float rowW = sidePad * 2.0f + lblW + rowGap + valW;
        if (rowW > widestRow) widestRow = rowW;
    }
    const float width = (widestRow > minWidth) ? widestRow : minWidth;

    const float lineH = ImGui::GetTextLineHeight();
    const float rowH  = lineH + S(6.0f);

    // Height lerp
    float dt = ImGui::GetIO().DeltaTime;
    inst.animHeight += (inst.targetHeight - inst.animHeight) * (std::min)(22.0f * dt, 1.0f);
    if (std::abs(inst.targetHeight - inst.animHeight) < 0.5f)
        inst.animHeight = inst.targetHeight;

    ImGui::SetNextWindowSize(ImVec2(width, inst.animHeight), ImGuiCond_Always);

    // Position — ConfigSystem persists per key. Namespaced with "debug." so
    // different instances don't collide with each other in settings.json.
    std::string cfgKey = "debug." + inst.key;
    ConfigSystem::WindowState* winState =
        ConfigSystem::RegisterWindow(cfgKey.c_str(), false);
    if (winState && winState->pos.x >= 0.0f) {
        ImGuiCond cond = winState->pendingApply ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        ImGui::SetNextWindowPos(winState->pos, cond);
        winState->pendingApply = false;
    } else {
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        // Default: top-right, but stagger multiple windows so they don't
        // all stack on top of each other on first show.
        int idx = 0;
        for (auto& k : s_order) { if (k == inst.key) break; ++idx; }
        ImGui::SetNextWindowPos(
            ImVec2(ds.x - width - S(20.0f),
                   S(20.0f) + idx * S(28.0f)),
            ImGuiCond_FirstUseEver);
    }

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(S(8.0f), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoSavedSettings;

    // Unique window ID per instance so multiple debug windows don't share
    // an ImGui window state / hover / drag id.
    char wid[64];
    std::snprintf(wid, sizeof(wid), "##debug_overlay_%s", inst.key.c_str());

    if (ImGui::Begin(wid, nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 winPos  = ImGui::GetWindowPos();
        if (winState) {
            winState->pos  = winPos;
            winState->size = ImGui::GetWindowSize();
        }

        // Split card fills — title strip = MenuLight, body = MenuDark.
        // Matches the keybinds list window exactly.
        ImVec2 bgMin = winPos;
        ImVec2 bgMax(winPos.x + width, winPos.y + inst.animHeight);
        ImVec2 titleStripMax(bgMax.x, winPos.y + titleBarH);
        dl->AddRectFilled(bgMin,          titleStripMax,
                          MenuStyle::Colors::MenuLight.u32());
        dl->AddRectFilled(ImVec2(bgMin.x, titleStripMax.y), bgMax,
                          MenuStyle::Colors::MenuDark.u32());
        dl->AddRect(bgMin, bgMax,
                    MenuStyle::Panel::Border.u32(), 0.0f, 0, 1.0f);

        // Clip during height lerp so overflow doesn't leak past the card.
        dl->PushClipRect(winPos, bgMax, true);

        // Accent cap — 1 px tall left-to-right gradient sitting INSIDE
        // the border, just under the 1 px gray outline. Inset 1 px on
        // the left and 2 px on the right. Full Accent on the left,
        // fading to transparent on the right. Matches the main menu's
        // accent cap so every floating window reads consistently.
        {
            const ImU32 accent = MenuStyle::Colors::Accent.u32();
            const ImU32 clear  = MenuStyle::Colors::Accent.alpha(0.0f).u32();
            float x0 = winPos.x + 1.0f;
            float x1 = bgMax.x - 2.0f;
            float y0 = winPos.y + 1.0f;
            float y1 = y0 + 1.0f;
            dl->AddRectFilledMultiColor(
                ImVec2(x0, y0), ImVec2(x1, y1),
                accent, clear, clear, accent);
        }

        // Title text — bold, vertically centered, left-aligned.
        float fontSize = ImGui::GetFontSize();
        float titleY   = winPos.y + (titleBarH - fontSize) * 0.5f;
        ImFont* titleFont = MenuStyle::g_fontBold ? MenuStyle::g_fontBold : ImGui::GetFont();
        MenuStyle::TextShadow(dl, titleFont, fontSize,
                              ImVec2(winPos.x + S(8.0f), titleY),
                              MenuStyle::Colors::Text.u32(), inst.title.c_str());

        // Bug icon (FA f188) right-aligned in the title bar.
        {
            const char* bugIcon = "\xef\x86\x88";
            if (MenuStyle::g_fontIcon) {
                ImVec2 iconSize = MenuStyle::g_fontIcon->CalcTextSizeA(
                    S(14.0f), FLT_MAX, 0.0f, bugIcon);
                float iconX = winPos.x + width - S(8.0f) - iconSize.x;
                float iconY = winPos.y + (titleBarH - iconSize.y) * 0.5f;
                MenuStyle::TextShadow(dl, MenuStyle::g_fontIcon, S(14.0f),
                                      ImVec2(iconX, iconY),
                                      MenuStyle::Colors::TextMuted.u32(), bugIcon);
            }
        }

        // Entries — "name:" muted-left, value accent-right.
        float sepY = winPos.y + titleBarH;
        float curY = sepY + sepGap + S(4.0f);
        for (auto& e : inst.entries) {
            char labelBuf[256];
            std::snprintf(labelBuf, sizeof(labelBuf), "%s:", e.name.c_str());
            MenuStyle::TextShadow(dl, ImVec2(winPos.x + S(8.0f), curY),
                                  MenuStyle::Colors::TextMuted.u32(), labelBuf);
            ImVec2 valSize = ImGui::CalcTextSize(e.value.c_str());
            MenuStyle::TextShadow(dl,
                ImVec2(winPos.x + width - S(8.0f) - valSize.x, curY),
                MenuStyle::Colors::Accent.u32(), e.value.c_str());
            curY += rowH;
        }

        curY += S(6.0f);
        inst.targetHeight = curY - winPos.y;

        dl->PopClipRect();

        // Reserve content extent so the window stays draggable.
        ImGui::SetCursorScreenPos(ImVec2(winPos.x, curY));
        ImGui::Dummy(ImVec2(0, 0));
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

void RenderAll(bool menuVisible) {
    for (auto& key : s_order) {
        auto it = s_windows.find(key);
        if (it != s_windows.end()) RenderInstance(it->second, menuVisible);
    }
}

// ── Legacy single-instance shims — route into the default window ──────

static constexpr const char* kDefaultKey   = "";
static constexpr const char* kDefaultTitle = "debug";

void  SetVisible(bool v)                                    { SetVisible(kDefaultKey, v); }
bool  IsVisible()                                           { return IsVisible(kDefaultKey); }
bool* GetVisiblePtr()                                       { return GetVisiblePtr(kDefaultKey, kDefaultTitle); }
void  Toggle()                                              { Toggle(kDefaultKey); }
void  Set(const char* name, const char* value)              { Set(kDefaultKey, kDefaultTitle, name, value); }
void  Set(const char* name, const std::string& value)       { Set(kDefaultKey, kDefaultTitle, name, value); }
void  Set(const char* name, int value)                      { Set(kDefaultKey, kDefaultTitle, name, value); }
void  Set(const char* name, float value, int decimals)      { Set(kDefaultKey, kDefaultTitle, name, value, decimals); }
void  Set(const char* name, bool value)                     { Set(kDefaultKey, kDefaultTitle, name, value); }
void  Remove(const char* name)                              { Remove(kDefaultKey, name); }
void  Clear()                                               { Clear(kDefaultKey); }
bool  HasEntries()                                          { return HasEntries(kDefaultKey); }
void  Render(bool menuVisible)                              { RenderAll(menuVisible); }

} // namespace DebugWindow
