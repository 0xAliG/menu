#include "input.h"
#include <cstring>

// Per-frame keystate poll. Two independent mirrors:
//
//   dev (s_devCur / s_devPrev)
//     Feeds ImGui — mouse buttons, keyboard events, drag. Source is
//     picked at compile time:
//       NEW_MENU_STANDALONE_DEMO  →  Win32 GetAsyncKeyState. The standalone
//                                    pdx.exe demo runs as a normal CPL-3
//                                    process with no HV, so VMMCALL would
//                                    #UD and we'd lose all mouse/keyboard
//                                    input.
//       default (client builds)   →  ParadoxHv::ReadKeystates VMCALL.
//                                    Same VMMCALL mirror the hotkey path
//                                    uses — the two arrays end up identical
//                                    but we keep them separate for clarity.
//     Named "dev" so it's obvious at every call site that this is the
//     Win32-backed convenience path, NOT the HV-only hotkey path.
//
//   hotkey-only (s_hotkeyCur / s_hotkeyPrev)
//     Feeds keybind capture + hotkey polling. Always via VMMCALL in
//     client builds, always via Win32 GetAsyncKeyState in the
//     standalone demo. Each frame stands on its own — a call that
//     returns false just zero-fills the mirror for that frame and the
//     next frame retries fresh. NO sticky "HV unavailable" latch: a
//     single hiccup at boot (HV mid-init, race with attach, transient
//     VMCB miss) used to poison the flag for the whole session and
//     silently kill keybind capture forever. If the HV is truly gone
//     the cheat has bigger problems than input; a per-frame retry
//     costs nothing.

#ifdef NEW_MENU_STANDALONE_DEMO
    #include <windows.h>
#endif
#ifdef NEW_MENU_STANDALONE_DEMO
    #include <windows.h>
#else
    #include "paradox_hv.h"
#endif

namespace Input {

static bool s_devCur[256]     = {};
static bool s_devPrev[256]    = {};
static bool s_hotkeyCur[256]  = {};
static bool s_hotkeyPrev[256] = {};

void Update() {
    // ── Dev / general input ───────────────────────────────────────
    memcpy(s_devPrev, s_devCur, sizeof(s_devCur));

#ifdef NEW_MENU_STANDALONE_DEMO
    for (int i = 0; i < 256; i++)
        s_devCur[i] = (::GetAsyncKeyState(i) & 0x8000) != 0;
#else
    {
        KeystateResult ks = {};
        if (ParadoxHv::ReadKeystates(&ks)) {
            for (int i = 0; i < 256; i++)
                s_devCur[i] = ParadoxHv::IsKeyDown(ks, i);
        } else {
            memset(s_devCur, 0, sizeof(s_devCur));
        }
    }
#endif

    // ── Hotkey-only input ─────────────────────────────────────────
    // No sticky latch — retry every frame. The `dev` path above already
    // does this and works reliably; the hotkey path used to guard with a
    // one-shot `s_hvUnavailable` boolean, which poisoned keybind capture
    // permanently if any single VMMCALL at boot returned false. Direct
    // per-frame call matches every other subsystem that reads keystates.
    memcpy(s_hotkeyPrev, s_hotkeyCur, sizeof(s_hotkeyCur));
#ifdef NEW_MENU_STANDALONE_DEMO
    for (int i = 0; i < 256; i++)
        s_hotkeyCur[i] = (::GetAsyncKeyState(i) & 0x8000) != 0;
#else
    {
        KeystateResult hkKs = {};
        if (ParadoxHv::ReadKeystates(&hkKs)) {
            for (int i = 0; i < 256; i++)
                s_hotkeyCur[i] = ParadoxHv::IsKeyDown(hkKs, i);
        } else {
            memset(s_hotkeyCur, 0, sizeof(s_hotkeyCur));
        }
    }
#endif
}

bool IsDevKeyDown(int vk) {
    if (vk < 0 || vk >= 256) return false;
    return s_devCur[vk];
}

bool IsDevKeyJustPressed(int vk) {
    if (vk < 0 || vk >= 256) return false;
    return s_devCur[vk] && !s_devPrev[vk];
}

bool WasDevKeyDown(int vk) {
    if (vk < 0 || vk >= 256) return false;
    return s_devPrev[vk];
}

bool IsHotkeyDown(int vk) {
    if (vk < 0 || vk >= 256) return false;
    return s_hotkeyCur[vk];
}

bool WasHotkeyDown(int vk) {
    if (vk < 0 || vk >= 256) return false;
    return s_hotkeyPrev[vk];
}

bool IsHotkeyJustPressed(int vk) {
    if (vk < 0 || vk >= 256) return false;
    return s_hotkeyCur[vk] && !s_hotkeyPrev[vk];
}

} // namespace Input
