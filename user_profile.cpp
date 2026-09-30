#include "user_profile.h"
#ifdef NEW_MENU_STANDALONE_DEMO
    #include <windows.h>
#else
    #include "paradox_hv.h"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PSD
#define STBI_NO_PNM
#define STBI_NO_TGA
#include "stb_image.h"

#include <d3d11.h>
#include <atomic>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <vector>

// 96 KB pic capacity — backend caps decoded image at 64 KB; leave slack.
static constexpr uint32_t kPicCap = 96 * 1024;

namespace {

std::atomic<bool>           g_hv_fetch_enabled{false};
std::atomic<bool>           g_ready{false};
std::string                 g_username;
std::string                 g_expires_at;
std::string                 g_expiry_display;
std::vector<uint8_t>        g_pic_bytes;
ID3D11Device*               g_device = nullptr;
ID3D11ShaderResourceView*   g_avatar_srv = nullptr;
int                         g_avatar_w   = 0;
int                         g_avatar_h   = 0;
bool                        g_avatar_tried = false;

// Parse "2026-09-12T14:30:00.000Z" → seconds since epoch (UTC). 0 on parse
// failure. Tolerates a missing fractional and/or trailing 'Z'.
uint64_t ParseIso8601Utc(const std::string& iso)
{
    if (iso.size() < 19) return 0;
    int y, mo, d, h, mi, s;
    if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d",
                    &y, &mo, &d, &h, &mi, &s) != 6) {
        return 0;
    }
    std::tm tm = {};
    tm.tm_year = y - 1900;
    tm.tm_mon  = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min  = mi;
    tm.tm_sec  = s;
#ifdef _WIN32
    const time_t t = _mkgmtime(&tm);
#else
    const time_t t = timegm(&tm);
#endif
    if (t < 0) return 0;
    return static_cast<uint64_t>(t);
}

void ComputeExpiryDisplay()
{
    g_expiry_display.clear();
    if (g_expires_at.empty()) {
        g_expiry_display = "lifetime";
        return;
    }
    const uint64_t expiry = ParseIso8601Utc(g_expires_at);
    if (!expiry) {
        g_expiry_display = "unknown";
        return;
    }
    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    if (expiry <= now) {
        g_expiry_display = "expired";
        return;
    }
    const uint64_t seconds_left = expiry - now;
    const uint64_t hours_left   = seconds_left / 3600ull;
    const uint64_t days_left    = hours_left   / 24ull;

    // Days when the remaining duration is 24h or more, otherwise hours.
    // Floors at "1 hour left" so the line never reads "0 ... left" in the
    // last sub-hour window before expiry triggers.
    char buf[64];
    if (days_left >= 1) {
        std::snprintf(buf, sizeof(buf),
                      "%llu day%s left",
                      static_cast<unsigned long long>(days_left),
                      days_left == 1 ? "" : "s");
    } else {
        const uint64_t hrs = hours_left ? hours_left : 1ull;
        std::snprintf(buf, sizeof(buf),
                      "%llu hour%s left",
                      static_cast<unsigned long long>(hrs),
                      hrs == 1 ? "" : "s");
    }
    g_expiry_display = buf;
}

}  // namespace

namespace UserProfile {

void SetHvFetchEnabled(bool enabled)
{
    g_hv_fetch_enabled.store(enabled, std::memory_order_release);
}

bool IsHvFetchEnabled()
{
    return g_hv_fetch_enabled.load(std::memory_order_acquire);
}

void SetDevice(ID3D11Device* device) { g_device = device; }

bool Fetch()
{
    #ifndef NEW_MENU_STANDALONE_DEMO
        if (!g_hv_fetch_enabled.load(std::memory_order_acquire)) return false;
        if (g_ready.load(std::memory_order_acquire)) return true;

        PdxUserProfile hdr = {};
        const bool ok = ParadoxHv::UserProfileGet(&hdr);
        if (!ok) return false;

        g_username   = hdr.username;
        g_expires_at = hdr.expires_at;
        ComputeExpiryDisplay();
        g_ready.store(true, std::memory_order_release);
        return true;
    #else
        return false;
    #endif
}

bool IsReady() {
    // Compile-time kill-switch (see user_profile.h::kDisable). When the
    // build flips this to true every accessor behaves as if no profile
    // has been received and consumers fall through to their fallback path.
    if constexpr (kDisable) return false;
    return g_ready.load(std::memory_order_acquire);
}

const std::string& GetUsername()      { return g_username; }
const std::string& GetExpiresAt()     { return g_expires_at; }
const std::string& GetExpiryDisplay() { return g_expiry_display; }

ID3D11ShaderResourceView* GetAvatarTexture()
{
    if (g_avatar_srv) return g_avatar_srv;
    if (g_avatar_tried) return nullptr;
    ID3D11Device* device = g_device;
    if (!device || !g_ready.load() || g_pic_bytes.empty()) return nullptr;
    g_avatar_tried = true;

    int w = 0, h = 0, ch = 0;
    stbi_uc* rgba = stbi_load_from_memory(g_pic_bytes.data(),
                                          static_cast<int>(g_pic_bytes.size()),
                                          &w, &h, &ch, 4);
    if (!rgba) return nullptr;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width      = static_cast<UINT>(w);
    td.Height     = static_cast<UINT>(h);
    td.MipLevels  = 1;
    td.ArraySize  = 1;
    td.Format     = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage      = D3D11_USAGE_DEFAULT;
    td.BindFlags  = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem     = rgba;
    sd.SysMemPitch = static_cast<UINT>(w) * 4u;

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = device->CreateTexture2D(&td, &sd, &tex);
    stbi_image_free(rgba);
    if (FAILED(hr) || !tex) return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format        = td.Format;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    hr = device->CreateShaderResourceView(tex, &sv, &g_avatar_srv);
    tex->Release();
    if (FAILED(hr) || !g_avatar_srv) return nullptr;
    g_avatar_w = w;
    g_avatar_h = h;
    return g_avatar_srv;
}

void GetAvatarSize(int* w, int* h)
{
    if (w) *w = g_avatar_w;
    if (h) *h = g_avatar_h;
}

void ReleaseAvatarTexture()
{
    if (g_avatar_srv) { g_avatar_srv->Release(); g_avatar_srv = nullptr; }
    g_avatar_w = 0;
    g_avatar_h = 0;
    g_avatar_tried = false;
}

}  // namespace UserProfile
