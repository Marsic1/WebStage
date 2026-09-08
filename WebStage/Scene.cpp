// ============================================================================
// WebStage - Scene Model Implementation
// JSON persistence (nlohmann) + hotkey string helpers
// ============================================================================

#include "Scene.h"
#include "Ui.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <shlobj.h>

using json = nlohmann::json;

// ============================================================================
// Hotkey string helpers (adapted from Spout2OverlayHUD)
// ============================================================================

static bool IsExtendedKey(UINT vk)
{
    // Keys whose scancode carries the 0xE0 prefix: GetKeyNameTextW needs
    // bit 24 set for them, otherwise it names the non-extended twin
    // (e.g. numpad "/" would come back as the main "-" key).
    switch (vk)
    {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
    case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_DIVIDE: case VK_NUMLOCK:
    case VK_RCONTROL: case VK_RMENU: case VK_LWIN: case VK_RWIN: case VK_APPS:
        return true;
    }
    return false;
}

std::wstring VkToWString(UINT vk)
{
    // Stable names first: letters, digits, F-keys and the classic editing /
    // navigation keys keep their English names on every layout (these are
    // also the persistence tokens, see VkToToken).
    if (vk >= 'A' && vk <= 'Z') return std::wstring(1, (wchar_t)vk);
    if (vk >= '0' && vk <= '9') return std::wstring(1, (wchar_t)vk);
    if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    switch (vk)
    {
    case VK_SPACE: return L"Space";
    case VK_RETURN: return L"Enter";
    case VK_TAB: return L"Tab";
    case VK_BACK: return L"Backspace";
    case VK_INSERT: return L"Insert";
    case VK_DELETE: return L"Delete";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"PgUp";
    case VK_NEXT: return L"PgDn";
    case VK_LEFT: return L"Left";
    case VK_RIGHT: return L"Right";
    case VK_UP: return L"Up";
    case VK_DOWN: return L"Down";
    case VK_SNAPSHOT: return L"PrtSc";
    default: break;
    }
    // Everything else (OEM punctuation, numpad, exotic keys) is named by the
    // OS for the current keyboard layout: "ì" on IT, "]" on US, and real
    // numpad names instead of "Key109".
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
    if (sc)
    {
        LONG lp = (LONG)(sc << 16);
        if (IsExtendedKey(vk)) lp |= 0x1000000;
        wchar_t name[64] = {};
        if (GetKeyNameTextW(lp, name, (int)(sizeof(name) / sizeof(name[0]))) > 0 && name[0])
            return name;
    }
    // Last resort: legacy US-layout table, then the raw code.
    switch (vk)
    {
    case VK_OEM_3: return L"`";
    case VK_OEM_MINUS: return L"-";
    case VK_OEM_COMMA: return L",";
    case VK_OEM_PERIOD: return L".";
    case VK_OEM_2: return L"/";
    case VK_OEM_1: return L";";
    case VK_OEM_7: return L"'";
    case VK_OEM_4: return L"[";
    case VK_OEM_5: return L"\\";
    case VK_OEM_6: return L"]";
    }
    return L"Key" + std::to_wstring(vk);
}

// Stable ASCII token for scene.json: identical to the display name whenever
// that name parses back to the same VK, otherwise "#<code>" so no key is
// ever lost in a save/load round-trip (e.g. numpad keys).
static std::string VkToToken(UINT vk)
{
    if (vk >= 'A' && vk <= 'Z') return std::string(1, (char)vk);
    if (vk >= '0' && vk <= '9') return std::string(1, (char)vk);
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    switch (vk)
    {
    case VK_SPACE: return "Space";
    case VK_RETURN: return "Enter";
    case VK_TAB: return "Tab";
    case VK_BACK: return "Backspace";
    case VK_INSERT: return "Insert";
    case VK_DELETE: return "Delete";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "PgUp";
    case VK_NEXT: return "PgDn";
    case VK_LEFT: return "Left";
    case VK_RIGHT: return "Right";
    case VK_UP: return "Up";
    case VK_DOWN: return "Down";
    case VK_SNAPSHOT: return "PrtSc";
    case VK_OEM_3: return "`";
    case VK_OEM_MINUS: return "-";
    case VK_OEM_COMMA: return ",";
    case VK_OEM_PERIOD: return ".";
    case VK_OEM_2: return "/";
    case VK_OEM_1: return ";";
    case VK_OEM_7: return "'";
    case VK_OEM_4: return "[";
    case VK_OEM_5: return "\\";
    case VK_OEM_6: return "]";
    }
    return "#" + std::to_string(vk);
}

std::wstring HotkeyToString(const HotkeyCombo& hk)
{
    if (!hk.IsSet()) return L"";
    std::wstring s;
    if (hk.modifiers & MOD_CONTROL) s += L"Ctrl+";
    if (hk.modifiers & MOD_ALT) s += L"Alt+";
    if (hk.modifiers & MOD_SHIFT) s += L"Shift+";
    if (hk.modifiers & MOD_WIN) s += L"Win+";
    s += VkToWString(hk.vk);
    return s;
}

std::string HotkeyToPersistString(const HotkeyCombo& hk)
{
    if (!hk.IsSet()) return "none";
    std::string s;
    if (hk.modifiers & MOD_CONTROL) s += "Ctrl+";
    if (hk.modifiers & MOD_ALT) s += "Alt+";
    if (hk.modifiers & MOD_SHIFT) s += "Shift+";
    if (hk.modifiers & MOD_WIN) s += "Win+";
    s += VkToToken(hk.vk);
    return s;
}

// Render a persisted hotkey string for the current keyboard layout
// ("Ctrl+ì" on IT, "Ctrl+]" on US), falling back to the raw string.
std::wstring HotkeyDisplayString(const std::string& persist)
{
    HotkeyCombo hk;
    if (ParseHotkeyString(persist, hk) && hk.IsSet())
        return HotkeyToString(hk);
    if (persist.empty() || persist == "none")
        return L"none";
    return Ui::FromUtf8(persist);
}

static UINT NumericVkToken(const std::string& token)
{
    // "#109" (current) or "Key109" (written by older builds that had no
    // name for the key): both carry the raw virtual-key code.
    const char* num = nullptr;
    if (!token.empty() && token[0] == '#')
        num = token.c_str() + 1;
    else if (token.size() > 3 && (token[0] == 'K' || token[0] == 'k') &&
        (token[1] == 'e' || token[1] == 'E') && (token[2] == 'y' || token[2] == 'Y'))
        num = token.c_str() + 3;
    else
        return 0;
    if (!*num) return 0;
    for (const char* p = num; *p; p++)
        if (*p < '0' || *p > '9') return 0;
    int n = atoi(num);
    if (n < 1 || n > 254) return 0;
    // Pure modifier keys can never be the main key of a combo.
    if (n == VK_SHIFT || n == VK_CONTROL || n == VK_MENU ||
        n == VK_LWIN || n == VK_RWIN || (n >= VK_LSHIFT && n <= VK_RMENU))
        return 0;
    return (UINT)n;
}

static UINT StringToVk(const std::string& token)
{
    if (token.size() == 1)
    {
        char c = (char)toupper((unsigned char)token[0]);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (UINT)c;
        switch (c)
        {
        case '`': return VK_OEM_3;
        case '-': return VK_OEM_MINUS;
        case ',': return VK_OEM_COMMA;
        case '.': return VK_OEM_PERIOD;
        case '/': return VK_OEM_2;
        case ';': return VK_OEM_1;
        case '\'': return VK_OEM_7;
        case '[': return VK_OEM_4;
        case '\\': return VK_OEM_5;
        case ']': return VK_OEM_6;
        }
        return 0;
    }
    auto ieq = [](const std::string& a, const char* b) {
        std::string t(b);
        if (a.size() != t.size()) return false;
        for (size_t i = 0; i < a.size(); i++)
            if (tolower((unsigned char)a[i]) != tolower((unsigned char)t[i])) return false;
        return true;
    };
    if (token.size() >= 2 && (token[0] == 'F' || token[0] == 'f'))
    {
        int n = atoi(token.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + (UINT)(n - 1);
    }
    if (ieq(token, "Space")) return VK_SPACE;
    if (ieq(token, "Enter")) return VK_RETURN;
    if (ieq(token, "Tab")) return VK_TAB;
    if (ieq(token, "Backspace")) return VK_BACK;
    if (ieq(token, "Insert")) return VK_INSERT;
    if (ieq(token, "Delete")) return VK_DELETE;
    if (ieq(token, "Home")) return VK_HOME;
    if (ieq(token, "End")) return VK_END;
    if (ieq(token, "PgUp")) return VK_PRIOR;
    if (ieq(token, "PgDn")) return VK_NEXT;
    if (ieq(token, "Left")) return VK_LEFT;
    if (ieq(token, "Right")) return VK_RIGHT;
    if (ieq(token, "Up")) return VK_UP;
    if (ieq(token, "Down")) return VK_DOWN;
    if (ieq(token, "PrtSc")) return VK_SNAPSHOT;
    UINT numeric = NumericVkToken(token);
    if (numeric) return numeric;
    return 0;
}

static std::string TrimNarrow(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool ParseHotkeyString(const std::string& str, HotkeyCombo& out)
{
    out = HotkeyCombo();
    std::string s = TrimNarrow(str);
    if (s.empty() || s == "none" || s == "None" || s == "NONE")
        return true;

    std::vector<std::string> tokens;
    size_t pos = 0;
    while (pos <= s.size())
    {
        size_t next = s.find('+', pos);
        std::string token = TrimNarrow(s.substr(pos, (next == std::string::npos ? s.size() : next) - pos));
        if (!token.empty())
            tokens.push_back(token);
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    if (tokens.size() < 2) return false; // need at least modifier + key

    UINT mods = 0;
    for (size_t i = 0; i + 1 < tokens.size(); i++)
    {
        const std::string& t = tokens[i];
        if (t == "Ctrl" || t == "ctrl" || t == "CTRL") mods |= MOD_CONTROL;
        else if (t == "Alt" || t == "alt" || t == "ALT") mods |= MOD_ALT;
        else if (t == "Shift" || t == "shift" || t == "SHIFT") mods |= MOD_SHIFT;
        else if (t == "Win" || t == "win" || t == "WIN") mods |= MOD_WIN;
        else return false;
    }

    UINT vk = StringToVk(tokens.back());
    if (!vk) return false;
    out.modifiers = mods;
    out.vk = vk;
    return true;
}

// ============================================================================
// SceneFile defaults
// ============================================================================

SceneFile SceneFile::Defaults()
{
    return SceneFile{};
}

std::string SceneFile::DefaultCss()
{
    // OBS browser-source style default: transparent page background,
    // no scrollbars, no scrolling. CEF renders real per-pixel alpha.
    return "html, body { background: rgba(0, 0, 0, 0) !important; background-color: rgba(0, 0, 0, 0) !important; overflow: hidden !important; } ::-webkit-scrollbar { display: none !important; width: 0 !important; height: 0 !important; }";
}

// ============================================================================
// SceneStore - scene.json next to the exe
// ============================================================================

std::wstring SceneStore::Path()
{
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir = exePath;
    size_t sep = dir.find_last_of(L"\\/");
    if (sep != std::wstring::npos)
        dir = dir.substr(0, sep + 1);
    return dir + L"scene.json";
}

static int JsonInt(const json& j, const char* key, int fallback)
{
    try
    {
        if (j.contains(key) && j.at(key).is_number_integer())
            return j.at(key).get<int>();
    }
    catch (const std::exception&) {}
    return fallback;
}

static bool JsonBool(const json& j, const char* key, bool fallback)
{
    try
    {
        if (j.contains(key) && j.at(key).is_boolean())
            return j.at(key).get<bool>();
    }
    catch (const std::exception&) {}
    return fallback;
}

static std::string JsonString(const json& j, const char* key, const std::string& fallback)
{
    try
    {
        if (j.contains(key) && j.at(key).is_string())
            return j.at(key).get<std::string>();
    }
    catch (const std::exception&) {}
    return fallback;
}

bool SceneStore::Load(SceneFile& out)
{
    out = SceneFile::Defaults();

    std::ifstream file(Path());
    if (!file.is_open())
        return false; // first run: keep defaults

    json j;
    try
    {
        file >> j;
    }
    catch (const std::exception&)
    {
        return false;
    }

    try
    {
        out.senderName = JsonString(j, "senderName", out.senderName);
        if (out.senderName.empty() || out.senderName.size() > 240)
            out.senderName = "WebStage";

        out.sceneWidth = JsonInt(j, "sceneWidth", out.sceneWidth);
        out.sceneHeight = JsonInt(j, "sceneHeight", out.sceneHeight);
        out.sendFps = JsonInt(j, "sendFps", out.sendFps);

        if (out.sceneWidth < 64) out.sceneWidth = 64;
        if (out.sceneWidth > 7680) out.sceneWidth = 7680;
        if (out.sceneHeight < 64) out.sceneHeight = 64;
        if (out.sceneHeight > 4320) out.sceneHeight = 4320;
        if (out.sendFps < 1) out.sendFps = 1;
        if (out.sendFps > 240) out.sendFps = 240;

        if (j.contains("sources") && j.at("sources").is_array())
        {
            for (const auto& s : j.at("sources"))
            {
                if (!s.is_object())
                    continue;
                SourceSettings src;
                src.name = Ui::FromUtf8(JsonString(s, "name", "Source"));
                src.url = Ui::FromUtf8(JsonString(s, "url", ""));
                src.hotkey = JsonString(s, "hotkey", "none");
                src.customCss = JsonString(s, "customCss", SceneFile::DefaultCss());
                src.x = JsonInt(s, "x", 0);
                src.y = JsonInt(s, "y", 0);
                src.width = JsonInt(s, "width", 640);
                src.height = JsonInt(s, "height", 360);
                src.renderWidth = JsonInt(s, "renderWidth", src.width);
                src.renderHeight = JsonInt(s, "renderHeight", src.height);
                src.visible = JsonBool(s, "visible", true);
                src.muted = JsonBool(s, "muted", true);
                src.volume = JsonInt(s, "volume", 100);
                src.opacity = JsonInt(s, "opacity", 100);
                src.locked = JsonBool(s, "locked", false);
                if (src.volume < 0) src.volume = 0;
                if (src.volume > 100) src.volume = 100;
                if (src.opacity < 0) src.opacity = 0;
                if (src.opacity > 100) src.opacity = 100;

                // Migrate chroma-key era scenes: any magenta page background
                // would now paint magenta for real (CEF renders true alpha
                // and no chroma key removes it anymore). Match every
                // spelling on a whitespace-stripped lowercase copy: rgb()
                // in any spacing, hex long/short, and named colors.
                {
                    std::string norm;
                    norm.reserve(src.customCss.size());
                    for (char c : src.customCss)
                    {
                        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                            continue;
                        norm += (char)tolower((unsigned char)c);
                    }
                    if (norm.find("255,0,255") != std::string::npos ||
                        norm.find("ff00ff") != std::string::npos ||
                        norm.find("#f0f") != std::string::npos ||
                        norm.find("fuchsia") != std::string::npos ||
                        norm.find("magenta") != std::string::npos)
                    {
                        Ui::Log(L"Scene: '%s' had chroma-era magenta CSS, reset to default",
                            src.name.c_str());
                        src.customCss = SceneFile::DefaultCss();
                    }
                }

                if (src.width < 16) src.width = 16;
                if (src.height < 16) src.height = 16;
                if (src.renderWidth < 16) src.renderWidth = 16;
                if (src.renderHeight < 16) src.renderHeight = 16;

                out.sources.push_back(std::move(src));
            }
        }
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

bool SceneStore::Save(const SceneFile& in)
{
    // Never lose the user's scene to a bad write: keep the previous file
    // as scene.json.bak (single generation) before truncating. A missing
    // or corrupt live file can always be recovered from the backup.
    try
    {
        std::wstring path = Path();
        DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
        {
            std::wstring bak = path + L".bak";
            CopyFileW(path.c_str(), bak.c_str(), FALSE);
        }
    }
    catch (...)
    {
    }

    std::ofstream file(Path(), std::ios::trunc);
    if (!file.is_open())
        return false;

    json j;
    j["senderName"] = in.senderName;
    j["sceneWidth"] = in.sceneWidth;
    j["sceneHeight"] = in.sceneHeight;
    j["sendFps"] = in.sendFps;

    json arr = json::array();
    for (const auto& s : in.sources)
    {
        json o;
        o["name"] = Ui::ToUtf8(s.name);
        o["url"] = Ui::ToUtf8(s.url);
        o["hotkey"] = s.hotkey;
        o["customCss"] = s.customCss;
        o["x"] = s.x;
        o["y"] = s.y;
        o["width"] = s.width;
        o["height"] = s.height;
        o["renderWidth"] = s.renderWidth;
        o["renderHeight"] = s.renderHeight;
        o["visible"] = s.visible;
        o["muted"] = s.muted;
        o["volume"] = s.volume;
        o["opacity"] = s.opacity;
        o["locked"] = s.locked;
        arr.push_back(o);
    }
    j["sources"] = arr;

    file << j.dump(4);
    return true;
}


