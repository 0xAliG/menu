#include "config_system.h"
#include "menu.h"
#include "menu_style.h"
#include "menu_builder.h"
#include "keybind_system.h"
#include "debug_window.h"
#include "notifications.h"
#include "ui_components.h"

// HV ring-buffer diagnostic log — captured by debug.exe regardless of client
// visibility / notification lifetime.
//
// Standalone builds (NEW_MENU_STANDALONE_DEMO) run as a normal CPL-3 process
// with no Paradox HV loaded. VMMCALL would #UD on the first call, killing the
// process before the menu even paints its first frame — that's exactly the
// "menu just closes on startup" symptom. Route calls through a local wrapper
// that skips the VMMCALL in that config; client builds keep the real HV log.
#ifdef NEW_MENU_STANDALONE_DEMO
    #include <windows.h>
#else
    #include "paradox_hv.h"
#endif
namespace {
inline void HvDebugLog(const char* line) {
#ifndef NEW_MENU_STANDALONE_DEMO
    ParadoxHv::DebugLog(line);
#else
    (void)line;
#endif
}
} // namespace

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <knownfolders.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <sstream>
#include <unordered_map>

namespace ConfigSystem {

// ─── JSON ──────────────────────────────────────────────────────────────────
// Minimal hand-rolled JSON. Supports null/bool/number/string/array/object.
// Numbers are stored as double (lossless for int32). Pretty-printed on write.

namespace json {

struct Value;
using Array  = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>; // ordered

enum class Type { Null, Bool, Number, String, Array, Object };

struct Value {
    Type type = Type::Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::shared_ptr<Array>  a;
    std::shared_ptr<Object> o;

    static Value Null()                                  { return Value{}; }
    static Value Bool(bool v)                            { Value x; x.type = Type::Bool; x.b = v; return x; }
    static Value Num(double v)                           { Value x; x.type = Type::Number; x.n = v; return x; }
    static Value Str(const std::string& v)               { Value x; x.type = Type::String; x.s = v; return x; }
    static Value Arr()                                   { Value x; x.type = Type::Array;  x.a = std::make_shared<Array>(); return x; }
    static Value Obj()                                   { Value x; x.type = Type::Object; x.o = std::make_shared<Object>(); return x; }

    bool IsNull() const   { return type == Type::Null;   }
    bool IsBool() const   { return type == Type::Bool;   }
    bool IsNum() const    { return type == Type::Number; }
    bool IsStr() const    { return type == Type::String; }
    bool IsArr() const    { return type == Type::Array;  }
    bool IsObj() const    { return type == Type::Object; }

    // Object accessors — return null Value if key/type missing.
    const Value& Get(const char* key) const {
        static const Value nullV;
        if (!IsObj()) return nullV;
        for (auto& kv : *o) if (kv.first == key) return kv.second;
        return nullV;
    }
    Value& Set(const char* key) {
        if (!IsObj()) { type = Type::Object; o = std::make_shared<Object>(); }
        for (auto& kv : *o) if (kv.first == key) return kv.second;
        o->push_back({ key, Value::Null() });
        return o->back().second;
    }
    void Push(Value v) {
        if (!IsArr()) { type = Type::Array; a = std::make_shared<Array>(); }
        a->push_back(std::move(v));
    }
};

// ── Parser ────────────────────────────────────────────────────────────────
struct Parser {
    const char* p;
    const char* end;
    bool        ok = true;

    void skip() {
        while (p < end) {
            char c = *p;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++p; continue; }
            break;
        }
    }
    bool eat(char c) { skip(); if (p < end && *p == c) { ++p; return true; } return false; }
    bool match(const char* lit) {
        skip();
        size_t n = std::strlen(lit);
        if ((size_t)(end - p) < n) return false;
        if (std::memcmp(p, lit, n) != 0) return false;
        p += n;
        return true;
    }

    Value ParseValue() {
        skip();
        if (p >= end) { ok = false; return {}; }
        char c = *p;
        if (c == '{') return ParseObject();
        if (c == '[') return ParseArray();
        if (c == '"') return ParseString();
        if (c == 't' || c == 'f') return ParseBool();
        if (c == 'n') return ParseNull();
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber();
        ok = false;
        return {};
    }

    Value ParseNull() {
        if (match("null")) return Value::Null();
        ok = false; return {};
    }
    Value ParseBool() {
        if (match("true"))  return Value::Bool(true);
        if (match("false")) return Value::Bool(false);
        ok = false; return {};
    }
    Value ParseNumber() {
        skip();
        const char* start = p;
        if (p < end && *p == '-') ++p;
        while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) ++p;
        if (p == start) { ok = false; return {}; }
        std::string buf(start, p - start);
        try {
            return Value::Num(std::stod(buf));
        } catch (...) {
            ok = false; return {};
        }
    }
    Value ParseString() {
        skip();
        if (!eat('"')) { ok = false; return {}; }
        std::string out;
        while (p < end) {
            char c = *p++;
            if (c == '"') return Value::Str(out);
            if (c == '\\' && p < end) {
                char esc = *p++;
                switch (esc) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    // Minimal \uXXXX → UTF-8 (BMP only, surrogate pairs ignored)
                    if (end - p < 4) { ok = false; return {}; }
                    unsigned cp = 0;
                    for (int i = 0; i < 4; i++) {
                        char h = *p++;
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                        else { ok = false; return {}; }
                    }
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) {
                        out += (char)(0xC0 | (cp >> 6));
                        out += (char)(0x80 | (cp & 0x3F));
                    } else {
                        out += (char)(0xE0 | (cp >> 12));
                        out += (char)(0x80 | ((cp >> 6) & 0x3F));
                        out += (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: ok = false; return {};
                }
            } else {
                out += c;
            }
        }
        ok = false; return {};
    }
    Value ParseArray() {
        if (!eat('[')) { ok = false; return {}; }
        Value v = Value::Arr();
        skip();
        if (eat(']')) return v;
        while (p < end) {
            Value elem = ParseValue();
            if (!ok) return {};
            v.a->push_back(std::move(elem));
            skip();
            if (eat(',')) continue;
            if (eat(']')) return v;
            ok = false; return {};
        }
        ok = false; return {};
    }
    Value ParseObject() {
        if (!eat('{')) { ok = false; return {}; }
        Value v = Value::Obj();
        skip();
        if (eat('}')) return v;
        while (p < end) {
            Value key = ParseString();
            if (!ok) return {};
            skip();
            if (!eat(':')) { ok = false; return {}; }
            Value val = ParseValue();
            if (!ok) return {};
            v.o->push_back({ key.s, std::move(val) });
            skip();
            if (eat(',')) continue;
            if (eat('}')) return v;
            ok = false; return {};
        }
        ok = false; return {};
    }
};

static bool Parse(const std::string& text, Value& out) {
    Parser ps{ text.data(), text.data() + text.size() };
    out = ps.ParseValue();
    ps.skip();
    return ps.ok;
}

// ── Writer (pretty-printed) ───────────────────────────────────────────────
static void WriteIndent(std::string& out, int depth) {
    for (int i = 0; i < depth; i++) out += "  ";
}
static void WriteString(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        default:
            if ((unsigned char)c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)(unsigned char)c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    out += '"';
}
static void WriteNumber(std::string& out, double n) {
    if (std::isnan(n) || std::isinf(n)) { out += "0"; return; }
    // Prefer integer formatting when it's a whole number in int32 range.
    if (n == (double)(long long)n && n >= -2147483648.0 && n <= 2147483647.0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", (long long)n);
        out += buf;
        return;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.9g", n);
    out += buf;
}
static void WriteValue(std::string& out, const Value& v, int depth) {
    switch (v.type) {
    case Type::Null:   out += "null"; break;
    case Type::Bool:   out += v.b ? "true" : "false"; break;
    case Type::Number: WriteNumber(out, v.n); break;
    case Type::String: WriteString(out, v.s); break;
    case Type::Array: {
        if (!v.a || v.a->empty()) { out += "[]"; break; }
        out += "[\n";
        for (size_t i = 0; i < v.a->size(); i++) {
            WriteIndent(out, depth + 1);
            WriteValue(out, (*v.a)[i], depth + 1);
            if (i + 1 < v.a->size()) out += ',';
            out += '\n';
        }
        WriteIndent(out, depth);
        out += ']';
        break;
    }
    case Type::Object: {
        if (!v.o || v.o->empty()) { out += "{}"; break; }
        out += "{\n";
        for (size_t i = 0; i < v.o->size(); i++) {
            WriteIndent(out, depth + 1);
            WriteString(out, (*v.o)[i].first);
            out += ": ";
            WriteValue(out, (*v.o)[i].second, depth + 1);
            if (i + 1 < v.o->size()) out += ',';
            out += '\n';
        }
        WriteIndent(out, depth);
        out += '}';
        break;
    }
    }
}
static std::string Serialize(const Value& v) {
    std::string out;
    WriteValue(out, v, 0);
    out += '\n';
    return out;
}

} // namespace json

// ─── Filesystem helpers (Win32) ────────────────────────────────────────────

static std::string WideToUtf8(const wchar_t* w) {
    if (!w) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(n - 1, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n - 1, nullptr, nullptr);
    return out;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

static std::string PathJoin(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char tail = a.back();
    if (tail == '\\' || tail == '/') return a + b;
    return a + "\\" + b;
}

static bool DirExists(const std::string& path) {
    DWORD attr = ::GetFileAttributesW(Utf8ToWide(path).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool FileExists(const std::string& path) {
    DWORD attr = ::GetFileAttributesW(Utf8ToWide(path).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

// Last EnsureDir failure context — populated when EnsureDir returns false so
// Init can surface "which path / which Win32 error" instead of the generic
// "failed to create config directory" notification. Plain TU-local globals;
// EnsureDir is the only writer.
static std::string s_lastEnsureFailPath;
static DWORD       s_lastEnsureFailErr  = 0;

static bool EnsureDir(const std::string& path) {
    if (DirExists(path)) return true;
    // Walk parents.
    size_t pos = 0;
    DWORD lastCreateErr = 0;
    std::string lastCreatePath;
    auto tryCreate = [&](const std::string& p) {
        if (!::CreateDirectoryW(Utf8ToWide(p).c_str(), nullptr)) {
            DWORD e = ::GetLastError();
            if (e != ERROR_ALREADY_EXISTS) {
                lastCreateErr  = e;
                lastCreatePath = p;
            }
        }
    };
    while ((pos = path.find_first_of("\\/", pos + 1)) != std::string::npos) {
        std::string sub = path.substr(0, pos);
        if (sub.empty() || (sub.size() == 2 && sub[1] == ':')) continue;
        if (!DirExists(sub)) tryCreate(sub);
    }
    tryCreate(path);
    if (DirExists(path)) return true;
    s_lastEnsureFailPath = lastCreatePath.empty() ? path : lastCreatePath;
    s_lastEnsureFailErr  = lastCreateErr;
    return false;
}

static std::string ReadTextFile(const std::string& path) {
    std::ifstream f(Utf8ToWide(path).c_str(), std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Atomic-ish write: write to .tmp, then MoveFileExW with REPLACE_EXISTING.
static bool WriteTextFileAtomic(const std::string& path, const std::string& content) {
    std::string tmpPath = path + ".tmp";
    {
        std::ofstream f(Utf8ToWide(tmpPath).c_str(), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(content.data(), (std::streamsize)content.size());
        f.close();
        if (!f) return false;
    }
    if (!::MoveFileExW(Utf8ToWide(tmpPath).c_str(), Utf8ToWide(path).c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ::DeleteFileW(Utf8ToWide(tmpPath).c_str());
        return false;
    }
    return true;
}

static bool DeleteFileFs(const std::string& path) {
    return ::DeleteFileW(Utf8ToWide(path).c_str()) != 0;
}

static bool MoveFileFs(const std::string& src, const std::string& dst) {
    return ::MoveFileExW(Utf8ToWide(src).c_str(), Utf8ToWide(dst).c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

static std::vector<std::string> EnumerateJsonFiles(const std::string& dir) {
    std::vector<std::string> out;
    std::wstring pat = Utf8ToWide(PathJoin(dir, "*.json"));
    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string name = WideToUtf8(fd.cFileName);
        // Strip .json
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0)
            name.resize(name.size() - 5);
        out.push_back(std::move(name));
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    std::sort(out.begin(), out.end(),
        [](const std::string& a, const std::string& b) {
            return _stricmp(a.c_str(), b.c_str()) < 0;
        });
    return out;
}

static std::string DocumentsDir() {
    // Top-level <SystemDrive>\pdx — one obvious, easy-to-find location
    // for every client's data. Writable from SYSTEM (dwm-clone clients)
    // AND from a normal interactive user (loader.exe debug path), so it
    // works in every injection mode without per-profile path juggling.
    // %SystemDrive% follows whichever drive Windows is actually installed
    // on (C:, D:, whatever) — never hardcode C:. Final layout:
    //   <SystemDrive>\pdx\<game>\configs\*.json
    //   <SystemDrive>\pdx\<game>\settings.json
    wchar_t drive[8] = {};
    DWORD n = ::GetEnvironmentVariableW(L"SystemDrive", drive,
                                         (DWORD)(sizeof(drive) / sizeof(drive[0])));
    if (n == 0 || n >= sizeof(drive) / sizeof(drive[0])) {
        // GetWindowsDirectory fallback — first 2 chars are the drive
        // letter + colon. Never returns empty on a live Windows box.
        wchar_t wd[MAX_PATH] = {};
        UINT wn = ::GetWindowsDirectoryW(wd, MAX_PATH);
        if (wn >= 2) { drive[0] = wd[0]; drive[1] = wd[1]; drive[2] = 0; }
        else         { drive[0] = L'C';  drive[1] = L':';  drive[2] = 0; }
    }
    return WideToUtf8(drive) + "\\pdx";
}

// Strip Windows-invalid filename chars and trim. Keeps spaces.
static std::string SanitizeName(const std::string& raw) {
    static const char invalid[] = "\\/:*?\"<>|";
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if ((unsigned char)c < 0x20) continue;
        bool bad = false;
        for (const char* p = invalid; *p; ++p) if (*p == c) { bad = true; break; }
        if (!bad) out += c;
    }
    // Trim trailing dots and spaces (Windows quirk).
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    // Trim leading spaces.
    size_t i = 0;
    while (i < out.size() && out[i] == ' ') ++i;
    return out.substr(i);
}

// ─── State ─────────────────────────────────────────────────────────────────

static bool        s_initialized = false;
static std::string s_gameName;
static std::string s_gameDir;
static std::string s_configsDir;
static std::string s_settingsPath;

static std::string s_active;
static std::string s_default;
static std::string s_lastUsed;
static bool        s_autoSave = false;

// Debounce timers (seconds since something last changed).
static bool  s_settingsDirty  = false;
static float s_settingsDirtyT = 0.0f;
static constexpr float kSettingsDebounceSec = 0.5f;

// Auto-save: periodic, quiet, only while an active config is set.
static float s_autoSaveTimer    = 0.0f;
static constexpr float kAutoSavePeriodSec = 5.0f;
static bool  s_silentSave       = false;   // suppresses save toasts inside Tick

// Baseline snapshots for settings-relevant scalars. Compared against live
// state each Tick — any drift marks settings.json dirty.
static int   s_lastDpi        = -1;
static bool  s_lastLock       = false;
static bool  s_lastDebug      = false;
static bool  s_scalarBaselineInit = false;

static std::vector<ChangeCallback> s_onChanged;

// Window registry — entries keyed by stable id.
struct WindowEntry {
    std::string id;
    std::unique_ptr<WindowState> state;
};
static std::vector<WindowEntry> s_windows;
// Last positions written to settings.json — compared each Tick to detect drag.
static std::unordered_map<std::string, std::pair<ImVec2, ImVec2>> s_lastSavedWindow;

// ─── Forward declarations ──────────────────────────────────────────────────
static void  WriteSettings();
static void  ReadSettings();
static void  ApplyWindowsFromSettings();
static void  CaptureWindowsToSettings(json::Value& root);
static void  NotifyChanged();

// ─── Helpers: get widget value from MenuBuilder by walking tabs ────────────

static MenuBuilder::Config CaptureWidgetsLive() {
    return MenuBuilder::Builder::Get().CaptureConfig();
}
static void ApplyWidgetsLive(const MenuBuilder::Config& c) {
    MenuBuilder::Builder::Get().ApplyConfig(c);
}

// ─── Build / parse a config file ───────────────────────────────────────────

static const int kConfigSchemaVersion = 1;

static const char* ConfigValueTypeName(MenuBuilder::ConfigValue::Type t) {
    using T = MenuBuilder::ConfigValue::Type;
    switch (t) {
    case T::Bool:      return "bool";
    case T::Float:     return "float";
    case T::Int:       return "int";
    case T::MultiBool: return "multi";
    case T::Color:     return "color";
    case T::Text:      return "text";
    case T::None:      return "none";
    }
    return "none";
}

static MenuBuilder::ConfigValue::Type ParseValueType(const std::string& s) {
    using T = MenuBuilder::ConfigValue::Type;
    if (s == "bool")  return T::Bool;
    if (s == "float") return T::Float;
    if (s == "int")   return T::Int;
    if (s == "multi") return T::MultiBool;
    if (s == "color") return T::Color;
    if (s == "text")  return T::Text;
    return T::None;
}

static const char* BindModeName(KeybindSystem::BindMode m) {
    using M = KeybindSystem::BindMode;
    switch (m) {
    case M::Toggle: return "toggle";
    case M::Hold:   return "hold";
    case M::OffKey: return "offkey";
    }
    return "toggle";
}
static KeybindSystem::BindMode ParseBindMode(const std::string& s) {
    using M = KeybindSystem::BindMode;
    if (s == "hold")    return M::Hold;
    if (s == "offkey")  return M::OffKey;
    return M::Toggle;
}

static const char* ElementTypeName(KeybindSystem::ElementType t) {
    using E = KeybindSystem::ElementType;
    switch (t) {
    case E::Checkbox:    return "checkbox";
    case E::SliderFloat: return "slider_f";
    case E::SliderInt:   return "slider_i";
    case E::Combo:       return "combo";
    case E::MultiCombo:  return "multi_combo";
    }
    return "checkbox";
}
static KeybindSystem::ElementType ParseElementType(const std::string& s) {
    using E = KeybindSystem::ElementType;
    if (s == "slider_f")    return E::SliderFloat;
    if (s == "slider_i")    return E::SliderInt;
    if (s == "combo")       return E::Combo;
    if (s == "multi_combo") return E::MultiCombo;
    return E::Checkbox;
}

static std::string IsoTimestampUtc() {
    SYSTEMTIME st;
    ::GetSystemTime(&st);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02uZ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// Build a JSON object representing the current live state.
static json::Value BuildConfigJson(const std::string& name, const std::string& createdUtc) {
    using namespace json;
    Value root = Value::Obj();
    root.Set("version") = Value::Num(kConfigSchemaVersion);
    root.Set("name")    = Value::Str(name);
    root.Set("created_utc")  = Value::Str(createdUtc.empty() ? IsoTimestampUtc() : createdUtc);
    root.Set("modified_utc") = Value::Str(IsoTimestampUtc());

    // Widgets
    Value widgets = Value::Obj();
    auto cfg = CaptureWidgetsLive();
    for (auto& kv : cfg.values) {
        Value entry = Value::Obj();
        entry.Set("type") = Value::Str(ConfigValueTypeName(kv.second.type));
        using T = MenuBuilder::ConfigValue::Type;
        switch (kv.second.type) {
        case T::Bool:  entry.Set("value") = Value::Bool(kv.second.b); break;
        case T::Float: entry.Set("value") = Value::Num((double)kv.second.f); break;
        case T::Int:   entry.Set("value") = Value::Num((double)kv.second.i); break;
        case T::MultiBool: {
            Value arr = Value::Arr();
            for (bool v : kv.second.multi) arr.Push(Value::Bool(v));
            entry.Set("value") = arr;
            break;
        }
        case T::Color: {
            Value arr = Value::Arr();
            for (int i = 0; i < 4; i++) arr.Push(Value::Num((double)kv.second.color[i]));
            entry.Set("value") = arr;
            break;
        }
        case T::Text:  entry.Set("value") = Value::Str(kv.second.text); break;
        case T::None:  entry.Set("value") = Value::Null(); break;
        }
        widgets.Set(kv.first.c_str()) = entry;
    }
    root.Set("widgets") = widgets;

    // Per-container collapsed state — keyed by "tab.sub.container".
    // Only emitted when at least one container has been touched, to keep
    // older configs lean.
    if (!cfg.collapsedContainers.empty()) {
        Value containers = Value::Obj();
        for (auto& kv : cfg.collapsedContainers) {
            containers.Set(kv.first.c_str()) = Value::Bool(kv.second);
        }
        root.Set("containers") = containers;
    }

    // Keybinds
    Value binds = Value::Arr();
    for (auto& kb : KeybindSystem::GetAllKeybinds()) {
        Value e = Value::Obj();
        e.Set("elementId")  = Value::Str(kb.elementId);
        e.Set("label")      = Value::Str(kb.label);
        e.Set("elemType")   = Value::Str(ElementTypeName(kb.elemType));
        e.Set("key")        = Value::Num((double)kb.key);
        e.Set("mode")       = Value::Str(BindModeName(kb.mode));
        e.Set("boolValue")  = Value::Bool(kb.boolValue);
        e.Set("floatValue") = Value::Num((double)kb.floatValue);
        e.Set("intValue")   = Value::Num((double)kb.intValue);
        e.Set("floatMin")   = Value::Num((double)kb.floatMin);
        e.Set("floatMax")   = Value::Num((double)kb.floatMax);
        e.Set("intMin")     = Value::Num((double)kb.intMin);
        e.Set("intMax")     = Value::Num((double)kb.intMax);
        e.Set("decimals")   = Value::Num((double)kb.decimalPlaces);
        Value multi = Value::Arr();
        for (bool v : kb.multiValues) multi.Push(Value::Bool(v));
        e.Set("multiValues") = multi;
        Value items = Value::Arr();
        for (auto& s : kb.comboItems) items.Push(Value::Str(s));
        e.Set("comboItems") = items;
        binds.Push(e);
    }
    root.Set("keybinds") = binds;

    // Style — nothing to serialize now that the accent color is a compile-
    // time constant (the picker was removed). Kept as an empty object so
    // older loaders that expect the key don't error out.
    root.Set("style") = Value::Obj();

    return root;
}

static void ApplyConfigJson(const json::Value& root) {
    using namespace json;

    // Widgets
    MenuBuilder::Config cfg;
    const Value& widgets = root.Get("widgets");
    if (widgets.IsObj()) {
        for (auto& kv : *widgets.o) {
            const Value& entry = kv.second;
            if (!entry.IsObj()) continue;
            MenuBuilder::ConfigValue v;
            v.type = ParseValueType(entry.Get("type").s);
            const Value& vv = entry.Get("value");
            using T = MenuBuilder::ConfigValue::Type;
            switch (v.type) {
            case T::Bool:  if (vv.IsBool()) v.b = vv.b; break;
            case T::Float: if (vv.IsNum())  v.f = (float)vv.n; break;
            case T::Int:   if (vv.IsNum())  v.i = (int)vv.n; break;
            case T::MultiBool:
                if (vv.IsArr()) for (auto& x : *vv.a) v.multi.push_back(x.IsBool() ? x.b : false);
                break;
            case T::Color:
                if (vv.IsArr()) {
                    for (int i = 0; i < 4 && i < (int)vv.a->size(); i++) {
                        const Value& x = (*vv.a)[i];
                        v.color[i] = x.IsNum() ? (float)x.n : 0.0f;
                    }
                }
                break;
            case T::Text:  if (vv.IsStr()) v.text = vv.s; break;
            case T::None:  break;
            }
            cfg.values[kv.first] = std::move(v);
        }
    }

    // Per-container collapsed state. Missing or non-bool entries default to
    // "not collapsed" (open) since that's the historical behavior.
    const Value& containers = root.Get("containers");
    if (containers.IsObj()) {
        for (auto& kv : *containers.o) {
            if (kv.second.IsBool())
                cfg.collapsedContainers[kv.first] = kv.second.b;
        }
    }

    ApplyWidgetsLive(cfg);

    // Keybinds
    std::vector<KeybindSystem::Keybind> kbs;
    const Value& binds = root.Get("keybinds");
    if (binds.IsArr()) {
        for (auto& je : *binds.a) {
            if (!je.IsObj()) continue;
            KeybindSystem::Keybind kb;
            kb.elementId     = je.Get("elementId").s;
            kb.label         = je.Get("label").s;
            kb.elemType      = ParseElementType(je.Get("elemType").s);
            kb.key           = (int)je.Get("key").n;
            kb.mode          = ParseBindMode(je.Get("mode").s);
            kb.boolValue     = je.Get("boolValue").IsBool() ? je.Get("boolValue").b : true;
            kb.floatValue    = (float)je.Get("floatValue").n;
            kb.intValue      = (int)je.Get("intValue").n;
            kb.floatMin      = (float)je.Get("floatMin").n;
            kb.floatMax      = je.Get("floatMax").IsNum() ? (float)je.Get("floatMax").n : 1.0f;
            kb.intMin        = (int)je.Get("intMin").n;
            kb.intMax        = je.Get("intMax").IsNum() ? (int)je.Get("intMax").n : 100;
            kb.decimalPlaces = (int)je.Get("decimals").n;
            const Value& mv = je.Get("multiValues");
            if (mv.IsArr()) for (auto& x : *mv.a) kb.multiValues.push_back(x.IsBool() ? x.b : false);
            const Value& ci = je.Get("comboItems");
            if (ci.IsArr()) for (auto& x : *ci.a) if (x.IsStr()) kb.comboItems.push_back(x.s);
            kbs.push_back(std::move(kb));
        }
    }
    KeybindSystem::ReplaceAll(kbs);

    // Style — accent color is compile-time only now (the picker UI was
    // removed). We deliberately do NOT read style.accentColor back, so
    // whatever value menu_style.h defines wins on every run. Saved
    // configs may still contain the field from older runs; it's ignored.
}

// ─── settings.json ─────────────────────────────────────────────────────────

static void ReadSettings() {
    using namespace json;
    if (!FileExists(s_settingsPath)) return;
    std::string text = ReadTextFile(s_settingsPath);
    if (text.empty()) return;

    Value root;
    if (!Parse(text, root) || !root.IsObj()) {
        Notifications::Push(Notifications::Type::Warning, "settings.json was malformed; starting fresh");
        return;
    }

    s_default  = root.Get("defaultConfig").IsStr() ? root.Get("defaultConfig").s : "";
    s_lastUsed = root.Get("lastConfig").IsStr()    ? root.Get("lastConfig").s    : "";
    s_autoSave = root.Get("autoSave").IsBool()     ? root.Get("autoSave").b      : false;

    // Workflow preferences
    if (root.Get("dpiIndex").IsNum())     Menu::SetDpiIndex((int)root.Get("dpiIndex").n);
    if (root.Get("lockLayout").IsBool())  Menu::SetLockLayout(root.Get("lockLayout").b);
    if (root.Get("debugOverlay").IsBool()) DebugWindow::SetVisible(root.Get("debugOverlay").b);

    // Color picker presets — packed ABGR (IM_COL32 layout) integers.
    {
        const Value& presets = root.Get("colorPresets");
        if (presets.IsArr()) {
            std::vector<uint32_t> v;
            v.reserve(presets.a->size());
            for (auto& x : *presets.a) {
                if (x.IsNum()) v.push_back((uint32_t)(uint64_t)x.n);
            }
            UI::SetColorPresets(v);
        }
    }

    // Window positions
    const Value& wins = root.Get("windows");
    if (wins.IsObj()) {
        for (auto& kv : *wins.o) {
            // Find or stash for late registration.
            WindowState* ws = nullptr;
            for (auto& e : s_windows) if (e.id == kv.first) { ws = e.state.get(); break; }
            if (!ws) {
                // Late-bind: create the entry preemptively so RegisterWindow finds it.
                WindowEntry e;
                e.id = kv.first;
                e.state = std::make_unique<WindowState>();
                s_windows.push_back(std::move(e));
                ws = s_windows.back().state.get();
            }
            const Value& obj = kv.second;
            if (!obj.IsObj()) continue;
            if (obj.Get("x").IsNum() && obj.Get("y").IsNum())
                ws->pos = ImVec2((float)obj.Get("x").n, (float)obj.Get("y").n);
            if (obj.Get("w").IsNum() && obj.Get("h").IsNum())
                ws->size = ImVec2((float)obj.Get("w").n, (float)obj.Get("h").n);
            ws->pendingApply = true;
            s_lastSavedWindow[kv.first] = { ws->pos, ws->size };
        }
    }
}

static void CaptureWindowsToSettings(json::Value& root) {
    using namespace json;
    Value wins = Value::Obj();
    for (auto& e : s_windows) {
        Value w = Value::Obj();
        if (e.state->pos.x >= 0.0f) {
            w.Set("x") = Value::Num((double)e.state->pos.x);
            w.Set("y") = Value::Num((double)e.state->pos.y);
        }
        if (e.state->persistSize && e.state->size.x > 0.0f) {
            w.Set("w") = Value::Num((double)e.state->size.x);
            w.Set("h") = Value::Num((double)e.state->size.y);
        }
        wins.Set(e.id.c_str()) = w;
    }
    root.Set("windows") = wins;
}

static void WriteSettings() {
    using namespace json;
    Value root = Value::Obj();
    root.Set("version")       = Value::Num(kConfigSchemaVersion);
    root.Set("defaultConfig") = Value::Str(s_default);
    root.Set("lastConfig")    = Value::Str(s_lastUsed);
    root.Set("autoSave")      = Value::Bool(s_autoSave);
    root.Set("dpiIndex")      = Value::Num((double)Menu::GetDpiIndex());
    root.Set("lockLayout")    = Value::Bool(Menu::GetLockLayout());
    root.Set("debugOverlay")  = Value::Bool(DebugWindow::IsVisible());

    // Color picker presets
    {
        Value arr = Value::Arr();
        for (uint32_t c : UI::GetColorPresets()) arr.Push(Value::Num((double)c));
        root.Set("colorPresets") = arr;
    }

    CaptureWindowsToSettings(root);

    if (!WriteTextFileAtomic(s_settingsPath, Serialize(root))) {
        Notifications::Push(Notifications::Type::Error, "failed to write settings.json");
        return;
    }

    // Snapshot for delta detection.
    s_lastSavedWindow.clear();
    for (auto& e : s_windows)
        s_lastSavedWindow[e.id] = { e.state->pos, e.state->size };
    s_lastDpi   = Menu::GetDpiIndex();
    s_lastLock  = Menu::GetLockLayout();
    s_lastDebug = DebugWindow::IsVisible();
    s_scalarBaselineInit = true;
}

// ─── Public API ────────────────────────────────────────────────────────────

bool Init(const char* gameName) {
    s_gameName = SanitizeName(gameName ? gameName : "default");
    if (s_gameName.empty()) s_gameName = "default";

    {
        char line[192];
        std::snprintf(line, sizeof(line),
                      "[cfg] Init game='%s'", s_gameName.c_str());
        HvDebugLog(line);
    }

    std::string docs = DocumentsDir();
    {
        char line[256];
        std::snprintf(line, sizeof(line),
                      "[cfg] DocumentsDir='%s' (empty=%d)",
                      docs.c_str(), (int)docs.empty());
        HvDebugLog(line);
    }
    if (docs.empty()) {
        Notifications::Push(Notifications::Type::Error, "config: failed to resolve Documents folder");
        return false;
    }
    // Top-level ProgramData folder is the game name itself — no extra
    // "pdx" parent. Final layout: <ProgramData>\<game>\configs\*.json
    // + <ProgramData>\<game>\settings.json.
    s_gameDir       = PathJoin(docs, s_gameName);
    s_configsDir    = PathJoin(s_gameDir, "configs");
    s_settingsPath  = PathJoin(s_gameDir, "settings.json");

    {
        char line[256];
        std::snprintf(line, sizeof(line),
                      "[cfg] gameDir='%s'", s_gameDir.c_str());
        HvDebugLog(line);
        std::snprintf(line, sizeof(line),
                      "[cfg] configsDir='%s'", s_configsDir.c_str());
        HvDebugLog(line);
    }

    bool gameOk = EnsureDir(s_gameDir);
    {
        char line[256];
        std::snprintf(line, sizeof(line),
                      "[cfg] EnsureDir(gameDir)=%d failPath='%s' err=%lu",
                      (int)gameOk,
                      s_lastEnsureFailPath.c_str(),
                      (unsigned long)s_lastEnsureFailErr);
        HvDebugLog(line);
    }
    bool cfgOk = gameOk && EnsureDir(s_configsDir);
    if (gameOk) {
        char line[256];
        std::snprintf(line, sizeof(line),
                      "[cfg] EnsureDir(configsDir)=%d failPath='%s' err=%lu",
                      (int)cfgOk,
                      s_lastEnsureFailPath.c_str(),
                      (unsigned long)s_lastEnsureFailErr);
        HvDebugLog(line);
    }
    if (!cfgOk) {
        Notifications::Push(Notifications::Type::Error,
                            "config: failed to create config directory");
        return false;
    }

    s_initialized = true;

    ReadSettings();
    // Window positions read from settings are applied via state.pendingApply
    // — the renderers pick them up on their next frame.

    // Auto-load priority: last-used wins so the user picks up where they left
    // off. Falls back to the explicitly-pinned default if last-used is empty
    // or has been deleted since the last run.
    if (!s_lastUsed.empty() && Exists(s_lastUsed.c_str())) {
        Load(s_lastUsed.c_str());
    } else if (!s_default.empty() && Exists(s_default.c_str())) {
        Load(s_default.c_str());
    }

    // Let UI subscribers populate their config lists now that the disk scan is ready.
    NotifyChanged();
    return true;
}

std::vector<std::string> List() {
    if (!s_initialized) return {};
    return EnumerateJsonFiles(s_configsDir);
}

bool Exists(const char* name) {
    if (!s_initialized || !name) return false;
    return FileExists(GetConfigPath(name));
}

std::string GetActive()   { return s_active; }
std::string GetDefault()  { return s_default; }
std::string GetLastUsed() { return s_lastUsed; }

bool Create(const char* name) {
    if (!s_initialized || !name) return false;
    std::string n = SanitizeName(name);
    if (n.empty()) {
        Notifications::Push(Notifications::Type::Warning, "config: empty name");
        return false;
    }
    if (Exists(n.c_str())) {
        Notifications::Push(Notifications::Type::Warning, "config '" + n + "' already exists");
        return false;
    }
    json::Value root = BuildConfigJson(n, "");
    if (!WriteTextFileAtomic(GetConfigPath(n.c_str()), json::Serialize(root))) {
        Notifications::Push(Notifications::Type::Error, "config: failed to create '" + n + "'");
        return false;
    }
    Notifications::Push(Notifications::Type::Success, "config '" + n + "' created");
    NotifyChanged();
    return true;
}

bool Save(const char* name) {
    if (!s_initialized || !name) return false;
    std::string n = SanitizeName(name);
    if (n.empty()) return false;

    bool wasNew = !FileExists(GetConfigPath(n.c_str()));

    std::string created;
    if (!wasNew) {
        // Preserve the original created_utc.
        json::Value existing;
        if (json::Parse(ReadTextFile(GetConfigPath(n.c_str())), existing) && existing.IsObj()) {
            const json::Value& c = existing.Get("created_utc");
            if (c.IsStr()) created = c.s;
        }
    }
    json::Value root = BuildConfigJson(n, created);
    if (!WriteTextFileAtomic(GetConfigPath(n.c_str()), json::Serialize(root))) {
        Notifications::Push(Notifications::Type::Error, "config: save '" + n + "' failed");
        return false;
    }
    // Saving a brand-new file (wasNew) ALSO promotes it to the active /
    // last-used config — the listbox's bold indicator follows s_active,
    // so the just-saved row lights up immediately. Subsequent saves of
    // an existing file leave s_active alone (user might be snapshotting
    // a working state under a new name without wanting to switch focus).
    if (wasNew) {
        s_active   = n;
        s_lastUsed = n;
        WriteSettings();
    }
    if (!s_silentSave)
        Notifications::Push(Notifications::Type::Success, "config '" + n + "' saved");
    if (wasNew) NotifyChanged();
    return true;
}

bool Load(const char* name) {
    if (!s_initialized || !name) return false;
    std::string n = SanitizeName(name);
    if (n.empty()) return false;
    std::string path = GetConfigPath(n.c_str());
    if (!FileExists(path)) {
        Notifications::Push(Notifications::Type::Warning, "config '" + n + "' not found");
        return false;
    }
    std::string text = ReadTextFile(path);
    json::Value root;
    if (!json::Parse(text, root) || !root.IsObj()) {
        // Quarantine the corrupt file so we don't keep trying to load it.
        char ts[32];
        SYSTEMTIME st; ::GetSystemTime(&st);
        std::snprintf(ts, sizeof(ts), "%04u%02u%02u%02u%02u%02u",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        MoveFileFs(path, path + ".corrupt-" + ts);
        Notifications::Push(Notifications::Type::Error, "config '" + n + "' was corrupt; renamed");
        return false;
    }
    ApplyConfigJson(root);
    s_active   = n;
    s_lastUsed = n;
    // Persist lastConfig synchronously so a fast F10 right after a Load
    // can't lose the auto-load anchor — the previous debounce path
    // (s_settingsDirty=true + wait for Tick) had a 0.5 s window where the
    // shutdown could fire before WriteSettings flushed.
    WriteSettings();
    s_settingsDirty = false; s_settingsDirtyT = 0.0f;
    s_autoSaveTimer = 0.0f;
    Notifications::Push(Notifications::Type::Info, "config '" + n + "' loaded");
    return true;
}

bool Delete(const char* name) {
    if (!s_initialized || !name) return false;
    std::string n = SanitizeName(name);
    if (n.empty()) return false;
    if (!DeleteFileFs(GetConfigPath(n.c_str()))) {
        Notifications::Push(Notifications::Type::Error, "config: delete '" + n + "' failed");
        return false;
    }
    if (s_active   == n) s_active.clear();
    if (s_default  == n) { s_default.clear(); s_settingsDirty = true; s_settingsDirtyT = 0.0f; }
    if (s_lastUsed == n) { s_lastUsed.clear(); s_settingsDirty = true; s_settingsDirtyT = 0.0f; }
    Notifications::Push(Notifications::Type::Info, "config '" + n + "' deleted");
    NotifyChanged();
    return true;
}

bool Rename(const char* oldName, const char* newName) {
    if (!s_initialized || !oldName || !newName) return false;
    std::string o = SanitizeName(oldName);
    std::string n = SanitizeName(newName);
    if (o.empty() || n.empty() || o == n) return false;
    std::string oldPath = GetConfigPath(o.c_str());
    std::string newPath = GetConfigPath(n.c_str());
    if (!FileExists(oldPath)) {
        Notifications::Push(Notifications::Type::Warning, "config '" + o + "' not found");
        return false;
    }
    if (FileExists(newPath)) {
        Notifications::Push(Notifications::Type::Warning, "config '" + n + "' already exists");
        return false;
    }
    if (!MoveFileFs(oldPath, newPath)) {
        Notifications::Push(Notifications::Type::Error, "config: rename failed");
        return false;
    }
    if (s_active   == o) s_active   = n;
    if (s_default  == o) { s_default  = n; s_settingsDirty = true; s_settingsDirtyT = 0.0f; }
    if (s_lastUsed == o) { s_lastUsed = n; s_settingsDirty = true; s_settingsDirtyT = 0.0f; }
    NotifyChanged();
    return true;
}

bool Duplicate(const char* src, const char* dst) {
    if (!s_initialized || !src || !dst) return false;
    std::string s = SanitizeName(src);
    std::string d = SanitizeName(dst);
    if (s.empty() || d.empty() || s == d) return false;
    if (!FileExists(GetConfigPath(s.c_str()))) {
        Notifications::Push(Notifications::Type::Warning, "config '" + s + "' not found");
        return false;
    }
    if (FileExists(GetConfigPath(d.c_str()))) {
        Notifications::Push(Notifications::Type::Warning, "config '" + d + "' already exists");
        return false;
    }
    std::string text = ReadTextFile(GetConfigPath(s.c_str()));
    json::Value root;
    if (!json::Parse(text, root) || !root.IsObj()) {
        Notifications::Push(Notifications::Type::Error, "config: source '" + s + "' is corrupt");
        return false;
    }
    root.Set("name") = json::Value::Str(d);
    root.Set("created_utc")  = json::Value::Str(IsoTimestampUtc());
    root.Set("modified_utc") = json::Value::Str(IsoTimestampUtc());
    if (!WriteTextFileAtomic(GetConfigPath(d.c_str()), json::Serialize(root))) {
        Notifications::Push(Notifications::Type::Error, "config: duplicate failed");
        return false;
    }
    NotifyChanged();
    return true;
}

bool SetDefault(const char* name) {
    if (!s_initialized) return false;
    std::string n = name ? SanitizeName(name) : "";
    if (!n.empty() && !Exists(n.c_str())) {
        Notifications::Push(Notifications::Type::Warning, "config '" + n + "' not found");
        return false;
    }
    s_default = n;
    s_settingsDirty = true; s_settingsDirtyT = 0.0f;
    return true;
}

void SaveActive() {
    if (s_active.empty()) return;
    Save(s_active.c_str());
}

void SetAutoSave(bool enabled) {
    if (s_autoSave == enabled) return;
    s_autoSave = enabled;
    s_settingsDirty = true; s_settingsDirtyT = 0.0f;
}
bool GetAutoSave() { return s_autoSave; }

void MarkDirty() {
    // Reserved for future fine-grained dirty tracking. Currently a no-op —
    // auto-save uses a fixed periodic flush instead.
}

void MarkSettingsDirty() {
    if (!s_initialized) return;
    s_settingsDirty = true;
    s_settingsDirtyT = 0.0f;
}

void Flush() {
    if (!s_initialized) return;
    // Unconditional write — the dirty flag only flips after Tick runs the
    // change-detection pass, and the user may quit before that next frame
    // ever runs (e.g. drag a window then immediately Alt+F4). One extra
    // write on shutdown is cheap; lost positions are not.
    WriteSettings();
    s_settingsDirty = false;
    s_settingsDirtyT = 0.0f;
    if (s_autoSave && !s_active.empty()) {
        s_silentSave = true;
        Save(s_active.c_str());
        s_silentSave = false;
    }
}

void Tick() {
    if (!s_initialized) return;
    float dt = ImGui::GetIO().DeltaTime;

    // Initialize scalar baseline on first tick so we don't mark dirty on
    // values that were just loaded by ReadSettings.
    if (!s_scalarBaselineInit) {
        s_lastDpi   = Menu::GetDpiIndex();
        s_lastLock  = Menu::GetLockLayout();
        s_lastDebug = DebugWindow::IsVisible();
        s_scalarBaselineInit = true;
    }

    // Detect scalar changes (DPI / lock layout / debug overlay).
    {
        int  curDpi   = Menu::GetDpiIndex();
        bool curLock  = Menu::GetLockLayout();
        bool curDebug = DebugWindow::IsVisible();
        if (curDpi != s_lastDpi || curLock != s_lastLock || curDebug != s_lastDebug) {
            s_settingsDirty = true;
            s_settingsDirtyT = 0.0f;
            s_lastDpi = curDpi; s_lastLock = curLock; s_lastDebug = curDebug;
        }
    }

    // Detect window position/size changes vs last saved snapshot.
    for (auto& e : s_windows) {
        auto it = s_lastSavedWindow.find(e.id);
        ImVec2 lastP = (it != s_lastSavedWindow.end()) ? it->second.first  : ImVec2(-1, -1);
        ImVec2 lastS = (it != s_lastSavedWindow.end()) ? it->second.second : ImVec2(0, 0);
        bool changed =
            std::abs(e.state->pos.x - lastP.x) > 0.5f ||
            std::abs(e.state->pos.y - lastP.y) > 0.5f ||
            (e.state->persistSize && (
                std::abs(e.state->size.x - lastS.x) > 0.5f ||
                std::abs(e.state->size.y - lastS.y) > 0.5f));
        if (changed) {
            s_settingsDirty = true;
            s_settingsDirtyT = 0.0f;
            s_lastSavedWindow[e.id] = { e.state->pos, e.state->size };
            break; // one debounce timer for the whole batch
        }
    }

    if (s_settingsDirty) {
        s_settingsDirtyT += dt;
        if (s_settingsDirtyT >= kSettingsDebounceSec) {
            WriteSettings();
            s_settingsDirty = false;
            s_settingsDirtyT = 0.0f;
        }
    }

    // Auto-save: periodic, silent. Re-evaluated each frame so toggling the
    // checkbox stops/starts the cadence immediately. Tiny IO either way.
    if (s_autoSave && !s_active.empty()) {
        s_autoSaveTimer += dt;
        if (s_autoSaveTimer >= kAutoSavePeriodSec) {
            s_silentSave = true;
            Save(s_active.c_str());
            s_silentSave = false;
            s_autoSaveTimer = 0.0f;
        }
    } else {
        s_autoSaveTimer = 0.0f;
    }
}

std::string GetGameDir()    { return s_gameDir; }
std::string GetConfigsDir() { return s_configsDir; }
std::string GetConfigPath(const char* name) {
    if (!s_initialized || !name) return {};
    return PathJoin(s_configsDir, std::string(name) + ".json");
}
std::string GetSettingsPath() { return s_settingsPath; }

void AddOnConfigsChanged(ChangeCallback cb) {
    s_onChanged.push_back(std::move(cb));
}

static void NotifyChanged() {
    for (auto& cb : s_onChanged) if (cb) cb();
}

void OpenGameFolder() {
    if (!s_initialized || s_gameDir.empty()) return;
    ::ShellExecuteW(nullptr, L"open", Utf8ToWide(s_gameDir).c_str(),
                    nullptr, nullptr, SW_SHOWNORMAL);
}

WindowState* RegisterWindow(const char* id, bool persistSize) {
    if (!id || !*id) return nullptr;
    for (auto& e : s_windows) {
        if (e.id == id) {
            e.state->persistSize = persistSize;
            return e.state.get();
        }
    }
    WindowEntry e;
    e.id = id;
    e.state = std::make_unique<WindowState>();
    e.state->persistSize = persistSize;
    s_windows.push_back(std::move(e));
    return s_windows.back().state.get();
}

} // namespace ConfigSystem
