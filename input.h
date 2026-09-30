#pragma once
#include <cstdint>

namespace Input {

// Call once per frame BEFORE any IsDevKeyDown / IsHotkeyDown query.
// Refreshes both keystate mirrors:
//   • dev            — Win32 GetAsyncKeyState in the standalone demo,
//                      HV VMMCALL in real cheat builds. Feeds ImGui
//                      (mouse buttons, keyboard events, drag). The
//                      "dev" tag is a deliberate name choice — this
//                      path exists so the standalone demo has mouse +
//                      keyboard when no HV is loaded. NEVER use it for
//                      hotkey / keybind polling in a real cheat build.
//   • hotkey-only    — HV VMMCALL always, SEH-guarded so a missing HV
//                      #UD faults cleanly instead of crashing. Feeds
//                      the menu keybind capture + hotkey polling paths.
void Update();

// ─── Dev / general input (mouse + ImGui keyboard) ──────────────────
//
// Named "Dev" to make it obvious at every call site that this is the
// non-HV, Win32-backed path. If you're wiring a NEW hotkey / keybind
// query, do NOT use these — use IsHotkeyDown below. These functions
// exist so ImGui's mouse and keyboard input keeps working in the
// standalone demo build where no hypervisor is loaded.
bool IsDevKeyDown(int vk);
bool IsDevKeyJustPressed(int vk);
bool WasDevKeyDown(int vk);

// ─── Hotkey input (HV keystates only) ──────────────────────────────
//
// Always sourced from the HV's gafAsyncKeyState mirror, regardless of
// build. When no HV is loaded, every key reads as released — the
// runtime cheat's stealth invariant is that hotkeys leave no usermode
// syscall trail, so we never fall back to Win32 for these paths even
// in the standalone demo build.
bool IsHotkeyDown(int vk);
bool WasHotkeyDown(int vk);
bool IsHotkeyJustPressed(int vk);

} // namespace Input
