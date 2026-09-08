#pragma once

// ============================================================================
// WebStage - Scene Model
// Scene + per-source settings, scene.json load/save, hotkey combo helpers
// (hotkey string format adapted from Spout2OverlayHUD: "Ctrl+Shift+F1").
// ============================================================================

#include <Windows.h>
#include <string>
#include <vector>

// ============================================================================
// HotkeyCombo - Global hotkey definition (0 vk = not set)
// ============================================================================
struct HotkeyCombo
{
    UINT vk = 0;        // virtual key code, 0 = not set
    UINT modifiers = 0; // MOD_CONTROL / MOD_ALT / MOD_SHIFT / MOD_WIN
    bool IsSet() const { return vk != 0; }
};

// Display string, e.g. L"Ctrl+Shift+F1" (empty when unset)
std::wstring HotkeyToString(const HotkeyCombo& hk);

// Render a persisted hotkey string for the current keyboard layout
// ("Ctrl+ì" on IT, "Ctrl+]" on US). Falls back to the raw string.
std::wstring HotkeyDisplayString(const std::string& persist);

// Persistence string: same as display, "none" when unset
std::string HotkeyToPersistString(const HotkeyCombo& hk);

// Parse "Ctrl+Shift+P" (or "none" / "") into a HotkeyCombo.
// Requires at least one modifier + key. Returns false when unparsable.
bool ParseHotkeyString(const std::string& str, HotkeyCombo& out);

// Single virtual-key display name
std::wstring VkToWString(UINT vk);

// ============================================================================
// SourceSettings - One HTTP overlay source (OBS browser-source equivalent)
// ============================================================================
struct SourceSettings
{
    std::wstring name;
    std::wstring url;
    std::string hotkey;    // persisted form ("none" when unset)
    std::string customCss; // UTF-8, injected on document create

    int x = 0;
    int y = 0;
    int width = 640;
    int height = 360;
    int renderWidth = 640;   // browser raster resolution
    int renderHeight = 360;

    bool visible = true;
    bool muted = true;
    int volume = 100;  // 0..100 (%)
    int opacity = 100; // 0..100 (%)
    bool locked = false; // canvas lock: not draggable/resizable in preview
};

// ============================================================================
// SceneFile - Whole scene (single scene layout, v1)
// ============================================================================
struct SceneFile
{
    std::string senderName = "WebStage";
    int sceneWidth = 1920;
    int sceneHeight = 1080;
    int sendFps = 60;

    std::vector<SourceSettings> sources;

    static SceneFile Defaults();
    static std::string DefaultCss();
};

// ============================================================================
// SceneStore - scene.json next to the exe
// ============================================================================
struct SceneStore
{
    static std::wstring Path();
    static bool Load(SceneFile& out);
    static bool Save(const SceneFile& in);
};


