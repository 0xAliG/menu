#pragma once

#include <cstdint>
#include <string>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

// ============================================================================
// User profile — fetches username / avatar / subscription expiry from the
// hypervisor via PDX_CALL_USER_PROFILE_GET. The actual VMCALL is gated by a
// runtime toggle (`SetHvFetchEnabled`) so this menu also runs cleanly as a
// standalone test executable without an HV loaded.
//
// Default: HV fetch DISABLED. The menu falls back to whatever placeholder
// data was poked into UserSystem (see menu.cpp Init), so dev iteration is
// unaffected.
// ============================================================================

namespace UserProfile {

// Compile-time kill-switch — NOT a runtime setting. Lives here so it can't
// be flipped from the config file or the menu chrome (which would be a
// trivial licensing-bypass). When true: Fetch() is suppressed, IsReady()
// always returns false, and every consumer falls through to its "no
// profile" path regardless of the runtime SetHvFetchEnabled flag below.
inline constexpr bool kDisable = true;

// Master gate. When false, Fetch() is a no-op and IsReady() stays false.
// Flip this to true from main.cpp / boot code only when running under the
// HV (production cheat path). Default false.
void SetHvFetchEnabled(bool enabled);
bool IsHvFetchEnabled();

// Cache the D3D11 device once during boot so GetAvatarTexture can lazy-
// upload without callers having to thread the device through Menu::Render.
// Safe to call with nullptr to forget.
void SetDevice(ID3D11Device* device);

// Fire the VMCALL. Idempotent — calling more than once after success is a
// no-op. Safe to call when the gate is off; just returns false immediately.
// Returns true if data is in hand AFTER the call.
bool Fetch();

// True once Fetch() has succeeded at least once this session.
bool IsReady();

// String getters. Empty strings if !IsReady().
const std::string& GetUsername();
const std::string& GetExpiresAt();      // raw ISO-8601 from backend
const std::string& GetExpiryDisplay();  // "expires in N days" / "expired" / etc.

// Avatar texture (ImGui-ready). Lazily uploads on first call. Returns
// nullptr if !IsReady(), no device has been set, or decode failed — caller
// falls back to drawing the username's first letter on an accent circle.
ID3D11ShaderResourceView* GetAvatarTexture();

// Texture dimensions of the uploaded avatar. (0, 0) if no texture.
void GetAvatarSize(int* w, int* h);

// Release the cached avatar SRV — call from teardown before the D3D11
// device dies. Safe to call multiple times.
void ReleaseAvatarTexture();

}  // namespace UserProfile
