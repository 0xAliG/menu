#pragma once
#include "imgui.h"
#include <functional>
#include <string>
#include <vector>

namespace ConfigSystem {

// ── Setup ──────────────────────────────────────────────────────────────────
// Resolves Documents/pdx/<gameName>/, creates dirs, reads settings.json,
// and auto-loads the default config if one is set. Call once during startup,
// AFTER Menu::Init / KeybindSystem::Init (so widgets and binds exist to apply to).
bool Init(const char* gameName);

// ── Discovery ──────────────────────────────────────────────────────────────
std::vector<std::string> List();           // alphabetical, no extension
bool                     Exists(const char* name);
std::string              GetActive();      // currently-loaded config, "" if none
std::string              GetDefault();     // auto-loaded on Init
std::string              GetLastUsed();

// ── CRUD ───────────────────────────────────────────────────────────────────
// All return true on success. Failure paths push a Notification.
bool Create   (const char* name);                   // creates an empty/default config
bool Save     (const char* name);                   // captures live state -> file
bool Load     (const char* name);                   // file -> live state
bool Delete   (const char* name);
bool Rename   (const char* oldName, const char* newName);
bool Duplicate(const char* src,     const char* dst);

// ── Meta ───────────────────────────────────────────────────────────────────
bool SetDefault(const char* name);          // "" clears default
void SaveActive();                          // shortcut for Save(GetActive())

// ── Auto-save ──────────────────────────────────────────────────────────────
void SetAutoSave(bool enabled);
bool GetAutoSave();
void MarkDirty();                           // any widget/state change calls this
void Tick();                                // called once per frame; flushes saves
void Flush();                               // write everything dirty NOW; call on shutdown

// ── Paths ──────────────────────────────────────────────────────────────────
std::string GetGameDir();                   // Documents/pdx/<game>/
std::string GetConfigsDir();                // Documents/pdx/<game>/configs/
std::string GetConfigPath(const char* name);
std::string GetSettingsPath();              // Documents/pdx/<game>/settings.json

// Open the game's config folder in the system file explorer.
void OpenGameFolder();

// ── Change notifications (for the configs UI list to refresh) ──────────────
using ChangeCallback = std::function<void()>;
void AddOnConfigsChanged(ChangeCallback cb);

// ── Window position persistence ────────────────────────────────────────────
// Stable handle a renderer reads pos/size from (apply path) and writes
// pos/size to (capture path). See debug_window.cpp / menu.cpp for examples.
struct WindowState {
    ImVec2 pos          = ImVec2(-1.0f, -1.0f);   // -1 = "no stored position"
    ImVec2 size         = ImVec2(0.0f,  0.0f);    // 0  = "no stored size"
    bool   persistSize  = false;
    bool   pendingApply = false;                   // ConfigSystem flips on after load
};

// Register a window for position persistence. Stable pointer — keep it for
// the program lifetime. `persistSize` only matters for user-resizable windows.
WindowState* RegisterWindow(const char* id, bool persistSize = false);

// Mark settings dirty (window dragged/resized, default changed, etc.).
// Tick() will flush settings.json after the debounce window.
void MarkSettingsDirty();

} // namespace ConfigSystem
