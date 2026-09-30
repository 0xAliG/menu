#pragma once
#include <string>

namespace DebugWindow {

// ─── Multi-instance debug windows ─────────────────────────────────────────
//
// Each debug window is keyed by a stable string ID. The first Set / Show /
// EnsureCreated call auto-creates the window with that key; the `title`
// parameter is used as the window's title on creation and updated on
// every subsequent call. Windows are rendered together via RenderAll().
//
// Example:
//     DebugWindow::Set("perf", "Performance", "fps", (int)io.Framerate);
//     DebugWindow::Set("perf", "Performance", "frame_ms", io.DeltaTime * 1000, 2);
//     DebugWindow::Set("net",  "Network",     "ping", pingMs);
//     …
//     DebugWindow::RenderAll(menuVisible);

// Ensure a window with this key exists; sets/updates its title.
void EnsureCreated(const char* key, const char* title);

// Destroy a specific window by key.
void Destroy(const char* key);

// Add or update an entry on the window with the given key. If the window
// doesn't exist yet it's auto-created with the supplied title. Entries
// keep insertion order; calling Set with an existing name replaces the
// value in-place (no reordering).
void Set(const char* key, const char* title, const char* name, const char* value);
void Set(const char* key, const char* title, const char* name, const std::string& value);
void Set(const char* key, const char* title, const char* name, int value);
void Set(const char* key, const char* title, const char* name, float value, int decimals = 2);
void Set(const char* key, const char* title, const char* name, bool value);

// Remove a single entry from a specific window / wipe all entries.
void Remove(const char* key, const char* name);
void Clear(const char* key);

// Per-window visibility. GetVisiblePtr auto-creates the window if needed
// (so you can wire it straight into a menu checkbox).
void  SetVisible(const char* key, bool v);
bool  IsVisible(const char* key);
bool* GetVisiblePtr(const char* key, const char* title = nullptr);
void  Toggle(const char* key);

// True if any window with this key has at least one entry.
bool HasEntries(const char* key);

// Render every registered debug window. Call once per frame from the
// top-level render path. `menuVisible` controls whether empty windows
// stay visible for repositioning.
void RenderAll(bool menuVisible);

// ── Backward-compatibility shims for the original single-instance API.
// All of these route into the default window (key = "" / title = "debug").
void  SetVisible(bool v);
bool  IsVisible();
bool* GetVisiblePtr();
void  Toggle();
void  Set(const char* name, const char* value);
void  Set(const char* name, const std::string& value);
void  Set(const char* name, int value);
void  Set(const char* name, float value, int decimals = 2);
void  Set(const char* name, bool value);
void  Remove(const char* name);
void  Clear();
bool  HasEntries();
void  Render(bool menuVisible);

} // namespace DebugWindow
