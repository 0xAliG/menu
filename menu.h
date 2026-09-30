#pragma once

namespace Menu {

// Implemented by each client. Init() calls this so the client can register
// its tabs/widgets through the MenuBuilder API. The menu engine deliberately
// knows nothing about client-specific schema; new menu's standalone demo
// implements SetupClient in menu.cpp (SetupDemoMenu).
void SetupClient();

// Implemented by each client. Render() calls this once per frame, BEFORE
// the actual menu draw. Use it for per-frame state sync tied to the
// client's widget schema (config filter rebuild, world-category sync, etc.).
// New menu's demo provides an empty default.
void TickClient();

// Initialize menu state
void Init();

// Render the full menu (call each frame)
void Render();

// Toggle menu visibility
void ToggleVisibility();
bool IsVisible();

// Open animation — lerps 0 → 1 whenever the menu transitions from hidden
// to visible. Every render path that participates in the open animation
// (top accent bar, top tabs slide, selected tab fill grow) samples this
// each frame to modulate its own draw. 1 = fully open (no animation
// active); values in [0,1) mean the menu is currently animating in.
float GetOpenAnim();

// Lock layout — exposed for config persistence.
bool GetLockLayout();
void SetLockLayout(bool v);

// DPI scale index — exposed for config persistence.
int  GetDpiIndex();
void SetDpiIndex(int idx);

// Menu-toggle virtual-key code. Default VK_INSERT (0x2D). Each client's
// per-frame key-poll path (e.g. apex's render.cpp) queries this so the
// engine settings popup can rebind the toggle key without the client
// having to thread the value back through its own state. Persisted via
// ConfigSystem like the other engine knobs.
int  GetMenuKey();
void SetMenuKey(int vk);

// Edge-detected menu-toggle gate. Clients call this once per frame with
// the live "is the menu key down right now" boolean (typically the
// HV-keystate poll). Returns true exactly on a fresh DOWN edge AFTER the
// engine has re-armed — which means: after a rebind, the just-bound key
// has to be released and pressed again to actually toggle. Internal
// was-down + armed state is owned here so callers don't need their own
// sibling vars.
bool PollMenuToggle(bool keyDown);

// Keybind-list panel gate. Controls whether the engine's Menu::Render
// emits KeybindSystem::RenderKeybindListWindow on each frame. Clients
// mirror their panels[kKeybinds] state into this each frame so the
// toggle inside the panels multidropdown actually hides the list when
// the menu is open (the menu-closed case is handled client-side, but
// the engine's Render() also draws keybinds inside the same frame
// otherwise). Rust, which has its own Menu::Panels dispatcher, should
// set this to false in SetupClient so the engine doesn't double-render
// alongside the dispatcher path.
bool GetKeybindListPanel();
void SetKeybindListPanel(bool enabled);

} // namespace Menu
