#pragma once

// ============================================================================
// WebStage - Main Application
// Editor frame: toolbar, source list, live preview, properties, status bar,
// tray icon. Owns the scene model, D3D11 rig, canvas, WebView2 environment
// and per-source controllers + hotkeys.
// ============================================================================

#include <Windows.h>
#include <wil/com.h>
#include <memory>
#include <string>
#include <vector>

#include "Scene.h"
#include "AudioMixer.h"
#include "GraphicsRig.h"
#include "Canvas.h"
#include "WebSource.h"
#include "SourceList.h"
#include "Preview.h"
#include "Properties.h"
#include "Version.h"

enum class SourceChange
{
    Name,
    Url,
    Rect,
    RenderSize,
    Visibility,
    Mute,
    Volume,
    Opacity,
    Css,
    Hotkey,
    Locked,
};

class App
{
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool Initialize(HINSTANCE instance);
    int Run();
    void Shutdown();
    void ForceQuit(); // no confirmation (automation)

    // Automated diagnostics: save the scene to PNG once FramesSent reaches
    // snapshotFrames OR snapshotDelaySec after startup (whichever fires
    // first; 0 disables that trigger), then quit after quitAfterSec
    // (0 = stay running).
    void SetAutoTest(const std::wstring& snapshotPath,
        unsigned long long snapshotFrames, int snapshotDelaySec, int quitAfterSec);

    // ---- Model access for UI components ----
    SceneFile& Model() { return m_model; }
    int SourceCount() const { return (int)m_model.sources.size(); }
    const SourceSettings& SourceAt(int i) const { return m_model.sources[(size_t)i]; }
    int Selection() const { return m_selection; }
    int SceneWidth() const { return m_model.sceneWidth; }
    int SceneHeight() const { return m_model.sceneHeight; }
    GraphicsRig* Rig() { return &m_rig; }

    // ---- Model operations (called by UI) ----
    void Select(int index);
    void ToggleVisible(int index);
    void ToggleLocked(int index);
    void DeleteSource(int index);
    void MoveSource(int index, int delta); // -1 up, +1 down
    void MoveSourceTo(int from, int to);   // drag-reorder in the list
    void PushSourceChange(int index, SourceChange change);
    void DragSourceTo(int index, int x, int y, int w, int h);
    void EndDrag();
    void SetHotkeyForSelected();
    void ClearHotkeyForSelected();
    void EditCssForSelected();
    void SetVolume(int index, int volume01);
    void SetOpacity(int index, int opacity01);
    void OnSourceReady(int index);
    void ApplyAudioToMixer(int index);

    // Preview
    bool IsPreviewOn() const { return m_previewOn; }
    void SetPreviewOn(bool on);
    void FillSourceToScene(int index);
    void CenterSource(int index, bool horiz, bool vert);

    void SaveSoon();
    void SaveNow();

    // Re-run the compositor right now (static sources produce no frames on
    // their own, so geometry/visibility changes must force a composite)
    // and refresh the preview.
    void Recomposite();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void Layout();
    void RefreshAll();
    void UpdateStatus();
    void OpenAddSource();
    void OpenSceneSettings();
    void SetSending(bool sending);

    // WebView2 + runtime sources
    void CreateAllSources();
    void CreateSource(size_t index);
    void OnSourceFrame(int index);
    void RebuildHotkeys();
    void UnregisterAllHotkeys();

    // Tray
    void AddToTray();
    void RemoveFromTray();
    void ShowTrayMenu();
    void RestoreWindow();
    void UpdateTrayIcon(); // go/stop badge follows m_sending

    // Toolbar
    void InitTooltips();
    enum Cmd : int
    {
        CmdAdd = 1001, CmdRemove, CmdUp, CmdDown, CmdScene, CmdSend,
        CmdCollapse,
    };
    enum TrayCmd : int
    {
        TrayRestore = 2001, TrayScene, TraySend, TrayAbout, TrayExit,
    };

    static constexpr UINT WM_TRAYICON = WM_USER + 1;
    static constexpr UINT_PTR HK_SOURCE_BASE = 100;
    static constexpr UINT_PTR TIMER_SAVE = 1;
    static constexpr UINT_PTR TIMER_STATUS = 2;

    HINSTANCE m_instance = nullptr;
    HWND m_hwnd = nullptr;

    HWND m_btnAdd = nullptr;
    HWND m_btnRemove = nullptr;
    HWND m_btnUp = nullptr;
    HWND m_btnDown = nullptr;
    HWND m_btnScene = nullptr;
    HWND m_btnSend = nullptr;
    HWND m_btnCollapse = nullptr;
    HWND m_status = nullptr;
    HWND m_tip = nullptr; // toolbar tooltips (icon-only compact mode)
    // Last iconOnly state pushed to Add/Remove/Scene (false forces a sync
    // on the first Layout, which always starts compact).
    bool m_toolbarCompact = false;

    // Status footer instrument strip (owner-drawn key/value columns)
    static constexpr UINT_PTR IdStatus = 1101;
    std::wstring m_statSender, m_statScene, m_statFps, m_statFrames, m_statSources;
    void DrawStatusStrip(const DRAWITEMSTRUCT* dis);
    // Compact by default (source list only); the expand arrow opens the
    // right panel (live preview + properties) and grows the window.
    // The live preview runs only while expanded.
    bool m_panelOpen = false;
    // Remembered window widths per mode: toggling restores what the user
    // had (or the default step the first time).
    int m_compactW = 0;
    int m_expandedW = 0;
    // Remembered compact list width: expanding keeps the left column as-is
    // and grows rightward instead of squeezing it down to the default.
    int m_listW = 0;

    SourceList m_list;
    Preview m_preview;
    Properties m_props;

    SceneFile m_model;
    std::vector<std::unique_ptr<WebSource>> m_sources;

    GraphicsRig m_rig;
    Canvas m_canvas;
    AudioMixer m_mixer;

    int m_selection = -1;
    bool m_sending = true;
    bool m_previewOn = false; // follows m_panelOpen (compact = off)
    bool m_inTray = false;
    bool m_trayNotified = false;
    bool m_shuttingDown = false;

    // Automated diagnostics
    std::wstring m_snapPath;
    unsigned long long m_snapFrames = 0;
    int m_snapDelaySec = 0;
    bool m_snapDone = false;
    int m_quitAfterSec = 0;
    ULONGLONG m_startTick = 0;
    void CheckAutoTest();

    ULONGLONG m_dragUiTick = 0; // last list/panel refresh during a drag
};

