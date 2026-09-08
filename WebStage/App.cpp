// ============================================================================
// WebStage - Main Application Implementation
// ============================================================================

#include "App.h"
#include "CefBridge.h"
#include "Ui.h"
#include "resource.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

App::App()
    : m_canvas(&m_rig)
{
}

App::~App()
{
    Shutdown();
}

bool App::Initialize(HINSTANCE instance)
{
    m_instance = instance;

    Ui::ForceDarkAppMode();

    WNDCLASSW wc = {};
    wc.lpfnWndProc = App::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = Ui::DarkBgBrush();
    wc.lpszClassName = L"WS_App";
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APPICON));
    if (!RegisterClassW(&wc))
        return false;

    m_hwnd = CreateWindowExW(0, wc.lpszClassName, APP_TITLE_WITH_VERSION,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 540, 780,
        nullptr, nullptr, instance, this);
    if (!m_hwnd)
        return false;

    // Font scale follows the monitor the window opens on (NOT the 96 that
    // GetDC-based queries report on modern Windows).
    Ui::SetActiveDpi(Ui::WindowDpi(m_hwnd));

    // Compact width must fit the icon-only toolbar at the current DPI
    // (expanded buttons carry labels and are much wider).
    {
        int needClient = Ui::Scale(m_hwnd,
            8 + 30 + 6 + 30 + 6 + 28 + 6 + 28 + 6 + 30 + 6 + 88 + 8);
        RECT wr = {};
        GetWindowRect(m_hwnd, &wr);
        RECT cr = {};
        GetClientRect(m_hwnd, &cr);
        int frameX = (wr.right - wr.left) - (cr.right - cr.left);
        int curW = wr.right - wr.left;
        if (needClient + frameX > curW)
            SetWindowPos(m_hwnd, nullptr, 0, 0, needClient + frameX,
                wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    Ui::EnableDarkFrame(m_hwnd);

    auto mkBtn = [&](const wchar_t* text, int id) -> HWND {
        HWND h = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 10, 10, m_hwnd, (HMENU)(INT_PTR)id, instance, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)Ui::Font(), 0);
        return h;
    };
    m_btnAdd = mkBtn(L"Add", CmdAdd);
    Ui::SetButtonIcon(m_btnAdd, Ui::IconPlus);
    m_btnRemove = mkBtn(L"Remove", CmdRemove);
    Ui::SetButtonIcon(m_btnRemove, Ui::IconTrash);
    m_btnUp = mkBtn(L"Up", CmdUp);
    Ui::SetButtonIcon(m_btnUp, Ui::IconUp, true);
    m_btnDown = mkBtn(L"Down", CmdDown);
    Ui::SetButtonIcon(m_btnDown, Ui::IconDown, true);
    m_btnScene = mkBtn(L"Scene settings", CmdScene);
    Ui::SetButtonIcon(m_btnScene, Ui::IconSliders);
    m_btnSend = mkBtn(L"Send", CmdSend);
    m_btnCollapse = mkBtn(L">", CmdCollapse);
    InitTooltips();

    m_status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0, 0, 10, 10, m_hwnd, (HMENU)(INT_PTR)IdStatus, instance, nullptr);
    SendMessageW(m_status, WM_SETFONT, (WPARAM)Ui::Font(), 0);

    if (!m_list.Create(m_hwnd, this))
        return false;
    if (!m_preview.Create(m_hwnd, this))
        return false;
    if (!m_props.Create(m_hwnd, this))
        return false;

    if (!m_rig.Initialize())
    {
        MessageBoxW(m_hwnd,
            L"Failed to initialize Direct3D 11. A D3D11-capable GPU (or WARP) is required.",
            APP_TITLE_WITH_VERSION, MB_ICONERROR);
        return false;
    }

    SceneStore::Load(m_model);

    if (!m_rig.CreateSceneTexture(m_model.sceneWidth, m_model.sceneHeight))
        return false;

    if (!m_canvas.Initialize(m_model.senderName, m_model.sendFps))
    {
        MessageBoxW(m_hwnd,
            L"Failed to initialize the Spout sender.",
            APP_TITLE_WITH_VERSION, MB_ICONWARNING);
    }

    m_selection = m_model.sources.empty() ? -1 : 0;
    m_startTick = GetTickCount64();

    if (!CefBridge::Initialize(m_instance))
    {
        MessageBoxW(m_hwnd,
            L"Failed to initialize the browser engine (CEF).",
            APP_TITLE_WITH_VERSION, MB_OK | MB_ICONERROR);
        return false;
    }

    // Audio mixer is optional: overlays keep working silently without it.
    m_mixer.Initialize();

    Layout();
    CreateAllSources();
    RebuildHotkeys();
    RefreshAll();
    AddToTray();

    SetTimer(m_hwnd, TIMER_STATUS, 500, nullptr);

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    // Final size sync: the CreateWindowEx footprint is raw pixels, but the
    // design is in logical units (mockup compact = 540x780 @96dpi). Enforce
    // the scaled footprint after the first layout so the window never
    // first-paints small on high-DPI displays.
    {
        int wantClientW = Ui::Scale(m_hwnd, 540);
        int wantClientH = Ui::Scale(m_hwnd, 780);
        RECT wr2 = { 0, 0, wantClientW, wantClientH };
        AdjustWindowRect(&wr2, WS_OVERLAPPEDWINDOW, FALSE);
        int wantW = wr2.right - wr2.left;
        int wantH = wr2.bottom - wr2.top;
        RECT wr = {};
        GetWindowRect(m_hwnd, &wr);
        if ((wr.right - wr.left) != wantW || (wr.bottom - wr.top) != wantH)
            SetWindowPos(m_hwnd, nullptr, 0, 0, wantW, wantH,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return true;
}

int App::Run()
{
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

void App::Shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;

    if (m_hwnd)
    {
        KillTimer(m_hwnd, TIMER_SAVE);
        KillTimer(m_hwnd, TIMER_STATUS);
    }
    UnregisterAllHotkeys();
    m_sources.clear(); // closes all browsers (async close on CEF threads)
    CefBridge::Shutdown(); // waits for browsers, shuts CEF down
    m_mixer.Shutdown();
    m_canvas.Shutdown();
    m_rig.Shutdown();
    RemoveFromTray();
}

void App::ForceQuit()
{
    SaveNow();
    Shutdown();
    if (m_hwnd)
        DestroyWindow(m_hwnd);
}

void App::SetAutoTest(const std::wstring& snapshotPath,
    unsigned long long snapshotFrames, int snapshotDelaySec, int quitAfterSec)
{
    m_snapPath = snapshotPath;
    m_snapFrames = snapshotFrames;
    m_snapDelaySec = snapshotDelaySec;
    m_snapDone = false;
    m_quitAfterSec = quitAfterSec;
}

void App::CheckAutoTest()
{
    if (!m_snapPath.empty() && !m_snapDone)
    {
        bool byFrames = m_snapFrames > 0 && m_canvas.FramesSent() >= m_snapFrames;
        bool byTime = m_snapDelaySec > 0 &&
            (GetTickCount64() - m_startTick) / 1000 >= (ULONGLONG)m_snapDelaySec;
        if (byFrames || byTime)
        {
            m_snapDone = true;
            bool ok = m_rig.SaveScenePng(m_snapPath.c_str());
            Ui::Log(L"Snapshot -> %s : %s (frames=%llu)",
                m_snapPath.c_str(), ok ? L"OK" : L"FAILED", m_canvas.FramesSent());
        }
    }
    if (m_quitAfterSec > 0 &&
        (GetTickCount64() - m_startTick) / 1000 >= (ULONGLONG)m_quitAfterSec)
    {
        ForceQuit();
    }
}

// ============================================================================
// Layout
// ============================================================================
void App::Layout()
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int W = rc.right - rc.left;
    int H = rc.bottom - rc.top;
    if (W <= 0 || H <= 0)
        return;

    auto S = [&](int v) { return Ui::Scale(m_hwnd, v); };
    int toolbarH = S(42);
    int statusH = S(48); // two info rows
    int listW = S(264);
    int pad = S(8);
    int stripW = S(26);

    int bx = S(8);
    auto placeBtn = [&](HWND b, int w) {
        SetWindowPos(b, nullptr, bx, S(7), w, S(28), SWP_NOZORDER | SWP_NOACTIVATE);
        bx += w + S(6);
    };
    // Compact toolbar: icons only (labels return when expanded).
    bool compactBar = !m_panelOpen;
    placeBtn(m_btnAdd, compactBar ? S(30) : S(60));
    placeBtn(m_btnRemove, compactBar ? S(30) : S(78));
    placeBtn(m_btnUp, S(28));
    placeBtn(m_btnDown, S(28));
    placeBtn(m_btnScene, compactBar ? S(30) : S(118));
    if (compactBar != m_toolbarCompact)
    {
        m_toolbarCompact = compactBar;
        Ui::SetButtonIcon(m_btnAdd, Ui::IconPlus, compactBar);
        Ui::SetButtonIcon(m_btnRemove, Ui::IconTrash, compactBar);
        Ui::SetButtonIcon(m_btnScene, Ui::IconSliders, compactBar);
        InvalidateRect(m_btnAdd, nullptr, TRUE);
        InvalidateRect(m_btnRemove, nullptr, TRUE);
        InvalidateRect(m_btnScene, nullptr, TRUE);
    }
    // The send toggle is the hero control: pinned to the toolbar's right edge
    int sendW = S(88);
    SetWindowPos(m_btnSend, nullptr, W - S(8) - sendW, S(7), sendW, S(28),
        SWP_NOZORDER | SWP_NOACTIVATE);

    int midTop = toolbarH + S(4);
    int midH = H - midTop - statusH - S(12);
    if (midH < S(100)) midH = S(100);

    int stripX = W - stripW - S(8);
    SetWindowPos(m_btnCollapse, nullptr, stripX, midTop, stripW, midH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowTextW(m_btnCollapse, m_panelOpen ? L"<" : L">");

    if (m_panelOpen)
    {
        // Keep the compact list width (never shrink the left column on
        // expand), clamped so the panel keeps a usable minimum.
        listW = S(264);
        if (m_listW > listW)
            listW = m_listW;
        int maxList = stripX - pad - S(8) - pad - S(200);
        if (maxList < S(264))
            maxList = S(264);
        if (listW > maxList)
            listW = maxList;
        m_list.SetPosition(S(8), midTop, listW, midH);
        ShowWindow(m_list.Window(), SW_SHOW);

        int panelX = S(8) + listW + pad;
        int panelW = stripX - pad - panelX;
        if (panelW < S(200)) panelW = S(200);
        int prevH = midH * 54 / 100;
        m_preview.SetPosition(panelX, midTop, panelW, prevH);
        m_props.SetPosition(panelX, midTop + prevH + pad, panelW, midH - prevH - pad);
        ShowWindow(m_preview.Window(), SW_SHOW);
        ShowWindow(m_props.Window(), SW_SHOW);
    }
    else
    {
        int fullListW = stripX - S(8) - pad;
        if (fullListW < S(200))
            fullListW = S(200);
        m_listW = fullListW; // remembered: expanding keeps this width
        m_list.SetPosition(S(8), midTop, fullListW, midH);
        ShowWindow(m_list.Window(), SW_SHOW);
        ShowWindow(m_preview.Window(), SW_HIDE);
        ShowWindow(m_props.Window(), SW_HIDE);
    }

    // Live preview runs only while expanded (no separate toggle).
    if (m_previewOn != m_panelOpen)
        m_previewOn = m_panelOpen;

    SetWindowPos(m_status, nullptr, S(8), H - statusH, W - S(16), statusH - S(4),
        SWP_NOZORDER | SWP_NOACTIVATE);

    // Same-size child moves don't repaint on their own: force the toolbar
    // row + status so live resizes never leave trails.
    InvalidateRect(m_btnAdd, nullptr, TRUE);
    InvalidateRect(m_btnRemove, nullptr, TRUE);
    InvalidateRect(m_btnUp, nullptr, TRUE);
    InvalidateRect(m_btnDown, nullptr, TRUE);
    InvalidateRect(m_btnScene, nullptr, TRUE);
    InvalidateRect(m_btnSend, nullptr, TRUE);
    InvalidateRect(m_btnCollapse, nullptr, TRUE);
    InvalidateRect(m_status, nullptr, TRUE);

    // NOTE: the preview swapchain is owned by Preview::Proc WM_SIZE
    // (create-once, resize-after), not recreated here per layout.

    RefreshAll();
}

// ============================================================================
// Refresh + status
// ============================================================================
void App::RefreshAll()
{
    if (m_shuttingDown)
        return;
    m_list.Refresh();
    m_props.Refresh();
    m_preview.Refresh();
    UpdateStatus();

    bool has = m_selection >= 0 && m_selection < SourceCount();
    EnableWindow(m_btnRemove, has ? TRUE : FALSE);
    EnableWindow(m_btnUp, (has && m_selection > 0) ? TRUE : FALSE);
    EnableWindow(m_btnDown, (has && m_selection + 1 < SourceCount()) ? TRUE : FALSE);
}

void App::UpdateStatus()
{
    // Instrument footer data (drawn in DrawStatusStrip; no send-state word
    // here: the toolbar Send LED is the single live-state indicator).
    m_statSender = Ui::FromUtf8(m_model.senderName);
    wchar_t buf[64];
    swprintf_s(buf, L"%dx%d", m_model.sceneWidth, m_model.sceneHeight);
    m_statScene = buf;
    swprintf_s(buf, L"%.0f / %d", m_canvas.SendFps(), m_model.sendFps);
    m_statFps = buf;

    unsigned long long f = m_canvas.FramesSent();
    wchar_t num[32];
    swprintf_s(num, L"%llu", f);
    int len = (int)wcslen(num);
    std::wstring grouped;
    for (int i = 0; i < len; i++)
    {
        grouped += num[i];
        int rem = len - 1 - i;
        if (rem > 0 && rem % 3 == 0)
            grouped += L',';
    }
    m_statFrames = grouped;
    swprintf_s(buf, L"%d", SourceCount());
    m_statSources = buf;

    if (m_status)
        InvalidateRect(m_status, nullptr, TRUE);
}

// ============================================================================
// Model operations
// ============================================================================

void App::Select(int index)
{
    if (index < -1 || index >= SourceCount())
        index = -1;
    if (index == m_selection)
        return;
    m_selection = index;
    RefreshAll();
}

void App::ToggleVisible(int index)
{
    if (index < 0 || index >= SourceCount())
        return;
    m_model.sources[(size_t)index].visible = !m_model.sources[(size_t)index].visible;
    PushSourceChange(index, SourceChange::Visibility);
}

void App::ToggleLocked(int index)
{
    if (index < 0 || index >= SourceCount())
        return;
    m_model.sources[(size_t)index].locked = !m_model.sources[(size_t)index].locked;
    PushSourceChange(index, SourceChange::Locked);
}

void App::DeleteSource(int index)
{
    if (index < 0 || index >= SourceCount())
        return;

    UnregisterHotKey(m_hwnd, (int)(HK_SOURCE_BASE + (UINT_PTR)index));
    m_sources.erase(m_sources.begin() + index);
    m_model.sources.erase(m_model.sources.begin() + index);

    if (m_selection >= SourceCount())
        m_selection = SourceCount() - 1;

    for (size_t i = 0; i < m_sources.size(); i++)
    {
        if (m_sources[i])
            m_sources[i]->SetIndex((int)i);
    }
    RebuildHotkeys();
    RefreshAll();
    Recomposite();
    SaveSoon();
}

void App::MoveSource(int index, int delta)
{
    MoveSourceTo(index, index + delta);
}

void App::MoveSourceTo(int from, int to)
{
    int n = SourceCount();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return;
    SourceSettings s = std::move(m_model.sources[(size_t)from]);
    m_model.sources.erase(m_model.sources.begin() + from);
    m_model.sources.insert(m_model.sources.begin() + to, std::move(s));
    auto ws = std::move(m_sources[(size_t)from]);
    m_sources.erase(m_sources.begin() + from);
    m_sources.insert(m_sources.begin() + to, std::move(ws));
    m_selection = to;
    // Keep per-source frame routing stable (wParam index)
    for (size_t i = 0; i < m_sources.size(); i++)
    {
        if (m_sources[i])
            m_sources[i]->SetIndex((int)i);
    }
    RebuildHotkeys();
    RefreshAll();
    Recomposite();
    SaveSoon();
}

void App::PushSourceChange(int index, SourceChange change)
{
    if (index < 0 || index >= SourceCount())
        return;
    const SourceSettings& s = m_model.sources[(size_t)index];
    WebSource* ws = index < (int)m_sources.size() ? m_sources[(size_t)index].get() : nullptr;

    switch (change)
    {
    case SourceChange::Rect:
        if (ws) ws->ApplyRect(s);
        RefreshAll();
        Recomposite();
        SaveSoon();
        return;
    case SourceChange::RenderSize:
        if (ws) ws->SetRenderSize(s.renderWidth, s.renderHeight);
        RefreshAll();
        Recomposite();
        SaveSoon();
        return;
    case SourceChange::Visibility:
        if (ws) ws->SetVisible(s.visible);
        RefreshAll();
        Recomposite();
        // Synchronous preview repaint so the toggle feels instant even
        // with live sources flooding the message queue.
        if (m_preview.Window())
            UpdateWindow(m_preview.Window());
        SaveSoon();
        return;
    case SourceChange::Url:
        if (ws) ws->SetUrl(s.url);
        break;
    case SourceChange::Mute:
        if (ws) ws->SetMuted(s.muted);
        ApplyAudioToMixer(index);
        break;
    case SourceChange::Volume:
        if (ws) ws->SetVolume(s.volume);
        ApplyAudioToMixer(index);
        break;
    case SourceChange::Opacity:
        // Model-only: the compositor reads it on the next pass
        Recomposite();
        break;
    case SourceChange::Locked:
        // Canvas-only state (preview outlines/hit-test read the model):
        // no recomposite needed, just repaint + persist.
        RefreshAll();
        SaveSoon();
        return;
    case SourceChange::Css:
        if (ws) ws->SetCss(s.customCss);
        break;
    case SourceChange::Name:
    case SourceChange::Hotkey:
        break;
    }

    RefreshAll();
    SaveSoon();
}

// Push model volume/mute into the mixer (slots are keyed by the source's
// stable audio key, so reorder/delete can never misroute them).
void App::ApplyAudioToMixer(int index)
{
    if (index < 0 || index >= SourceCount())
        return;
    if (index >= (int)m_sources.size() || !m_sources[(size_t)index])
        return;
    const SourceSettings& s = m_model.sources[(size_t)index];
    m_mixer.SetSource(m_sources[(size_t)index]->AudioKey(),
        (float)s.volume / 100.0f, s.muted);
}

void App::SetVolume(int index, int volume01)
{
    if (index < 0 || index >= SourceCount())
        return;
    if (volume01 < 0) volume01 = 0;
    if (volume01 > 100) volume01 = 100;
    m_model.sources[(size_t)index].volume = volume01;
    PushSourceChange(index, SourceChange::Volume);
}

void App::SetOpacity(int index, int opacity01)
{
    if (index < 0 || index >= SourceCount())
        return;
    if (opacity01 < 0) opacity01 = 0;
    if (opacity01 > 100) opacity01 = 100;
    m_model.sources[(size_t)index].opacity = opacity01;
    PushSourceChange(index, SourceChange::Opacity);
}

void App::SetPreviewOn(bool on)
{
    // Preview follows the expand/collapse panel (no separate toggle).
    m_previewOn = on;
    m_preview.Refresh();
}

void App::FillSourceToScene(int index)
{
    if (index < 0 || index >= SourceCount())
        return;
    SourceSettings& s = m_model.sources[(size_t)index];
    s.x = 0;
    s.y = 0;
    s.width = m_model.sceneWidth;
    s.height = m_model.sceneHeight;
    PushSourceChange(index, SourceChange::Rect);
}

void App::CenterSource(int index, bool horiz, bool vert)
{
    if (index < 0 || index >= SourceCount())
        return;
    SourceSettings& s = m_model.sources[(size_t)index];
    if (horiz)
        s.x = (m_model.sceneWidth - s.width) / 2;
    if (vert)
        s.y = (m_model.sceneHeight - s.height) / 2;
    PushSourceChange(index, SourceChange::Rect);
}

void App::DragSourceTo(int index, int x, int y, int w, int h)
{
    if (index < 0 || index >= SourceCount())
        return;
    SourceSettings& s = m_model.sources[(size_t)index];
    s.x = x; s.y = y; s.width = w; s.height = h;
    if (index < (int)m_sources.size() && m_sources[(size_t)index])
        m_sources[(size_t)index]->ApplyRect(s);
    // Mousemove storms arrive far faster than paint: composite + preview
    // every tick, but refresh the (GDI-heavy) list/panel at ~12Hz.
    ULONGLONG now = GetTickCount64();
    if (now - m_dragUiTick >= 80)
    {
        m_dragUiTick = now;
        m_list.Refresh();
        m_props.Refresh();
    }
    Recomposite();
}

void App::EndDrag()
{
    SaveSoon();
    RefreshAll();
    Recomposite();
}

void App::SaveSoon()
{
    if (m_hwnd && !m_shuttingDown)
        SetTimer(m_hwnd, TIMER_SAVE, 500, nullptr);
}

void App::SaveNow()
{
    if (m_hwnd)
        KillTimer(m_hwnd, TIMER_SAVE);
    SceneStore::Save(m_model);
}

void App::Recomposite()
{
    if (m_shuttingDown)
        return;
    m_canvas.Composite(m_model, m_sources);
    m_preview.Refresh();
}

// ============================================================================
// Status footer: instrument strip with key/value columns. Columns that do
// not fit the compact window drop out right-to-left.
// ============================================================================
void App::DrawStatusStrip(const DRAWITEMSTRUCT* dis)
{
    if (!dis)
        return;
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;

    HBRUSH bg = CreateSolidBrush(Ui::BgPanel());
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    struct Col
    {
        const wchar_t* key;
        const wchar_t* val;
    };
    Col cols[] = {
        { L"SENDER", m_statSender.c_str() },
        { L"SCENE", m_statScene.c_str() },
        { L"FPS", m_statFps.c_str() },
        { L"FRAMES", m_statFrames.c_str() },
        { L"SOURCES", m_statSources.c_str() },
    };
    const int n = 5;

    UINT dpi = Ui::WindowDpi(dis->hwndItem);
    int gap = MulDiv(18, (int)dpi, 96);
    int pad = MulDiv(14, (int)dpi, 96);
    int midY = (rc.top + rc.bottom) / 2;

    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, Ui::FontMono());
    int x = pad;
    for (int i = 0; i < n; i++)
    {
        SIZE vs = {};
        GetTextExtentPoint32W(dc, cols[i].val, (int)wcslen(cols[i].val), &vs);
        SelectObject(dc, Ui::Font());
        SIZE ks = {};
        GetTextExtentPoint32W(dc, cols[i].key, (int)wcslen(cols[i].key), &ks);
        int colW = (ks.cx > vs.cx) ? ks.cx : vs.cx;
        if (x + colW > rc.right - pad)
            break;
        RECT kr = { x, midY - vs.cy - 4, x + colW + MulDiv(30, (int)dpi, 96), midY };
        SetTextColor(dc, Ui::Gray());
        DrawTextW(dc, cols[i].key, -1, &kr, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, Ui::FontMono());
        RECT vr = { x, midY, x + colW + MulDiv(30, (int)dpi, 96), midY + vs.cy + 4 };
        SetTextColor(dc, Ui::Fg());
        DrawTextW(dc, cols[i].val, -1, &vr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        x += colW + gap;
    }
    SelectObject(dc, oldFont);
}

// ============================================================================
// Hotkeys
// ============================================================================
void App::UnregisterAllHotkeys()
{
    if (!m_hwnd)
        return;
    for (size_t i = 0; i < m_model.sources.size() + 8; i++)
        UnregisterHotKey(m_hwnd, (int)(HK_SOURCE_BASE + i));
}

void App::RebuildHotkeys()
{
    UnregisterAllHotkeys();
    if (!m_hwnd || m_shuttingDown)
        return;
    for (size_t i = 0; i < m_model.sources.size(); i++)
    {
        HotkeyCombo hk;
        if (!ParseHotkeyString(m_model.sources[i].hotkey, hk) || !hk.IsSet())
            continue;
        RegisterHotKey(m_hwnd, (int)(HK_SOURCE_BASE + i), hk.modifiers | MOD_NOREPEAT, hk.vk);
    }
}

void App::SetHotkeyForSelected()
{
    int sel = m_selection;
    if (sel < 0 || sel >= SourceCount())
        return;

    HotkeyCombo hk;
    ParseHotkeyString(m_model.sources[(size_t)sel].hotkey, hk);
    std::string old = m_model.sources[(size_t)sel].hotkey;

    UnregisterAllHotkeys();
    bool confirmed = Ui::HotkeyDialog(m_hwnd, m_model.sources[(size_t)sel].name.c_str(), hk);
    if (!confirmed)
    {
        RebuildHotkeys();
        return;
    }

    std::string persist = HotkeyToPersistString(hk);
    m_model.sources[(size_t)sel].hotkey = persist;

    // Verify the combo can actually be registered before keeping it
    bool ok = true;
    if (hk.IsSet())
    {
        UnregisterHotKey(m_hwnd, HK_SOURCE_BASE + (UINT_PTR)sel);
        ok = RegisterHotKey(m_hwnd, HK_SOURCE_BASE + (UINT_PTR)sel,
            hk.modifiers | MOD_NOREPEAT, hk.vk) ? true : false;
    }
    if (!ok)
    {
        m_model.sources[(size_t)sel].hotkey = old;
        MessageBoxW(m_hwnd,
            L"That key combination is already in use by another application.",
            APP_TITLE_WITH_VERSION, MB_OK | MB_ICONWARNING);
    }
    RebuildHotkeys();
    PushSourceChange(sel, SourceChange::Hotkey);
}

void App::ClearHotkeyForSelected()
{
    int sel = m_selection;
    if (sel < 0 || sel >= SourceCount())
        return;
    UnregisterHotKey(m_hwnd, HK_SOURCE_BASE + (UINT_PTR)sel);
    m_model.sources[(size_t)sel].hotkey = "none";
    PushSourceChange(sel, SourceChange::Hotkey);
}

void App::EditCssForSelected()
{
    int sel = m_selection;
    if (sel < 0 || sel >= SourceCount())
        return;
    std::vector<Ui::Field> fields;
    fields.push_back({ L"Per-source CSS - restyles every page this source loads, OBS browser-source style",
        Ui::FromUtf8(m_model.sources[(size_t)sel].customCss), true, 200 });
    if (Ui::InputDialog(m_hwnd, L"Edit CSS", fields, 560))
    {
        m_model.sources[(size_t)sel].customCss = Ui::ToUtf8(fields[0].value);
        PushSourceChange(sel, SourceChange::Css);
    }
}

// ============================================================================
// Dialogs
// ============================================================================
void App::OpenAddSource()
{
    int n = SourceCount() + 1;
    wchar_t defName[64];
    swprintf_s(defName, L"Source %d", n);

    std::vector<Ui::Field> fields;
    fields.push_back({ L"Name", defName });
    fields.push_back({ L"URL (https://...)", L"" });
    fields.push_back({ L"Render width", std::to_wstring(m_model.sceneWidth) });
    fields.push_back({ L"Render height", std::to_wstring(m_model.sceneHeight) });
    if (!Ui::InputDialog(m_hwnd, L"Add HTTP Source", fields, 560))
        return;

    std::wstring name = Ui::TrimW(fields[0].value);
    std::wstring url = Ui::TrimW(fields[1].value);
    int rw = _wtoi(fields[2].value.c_str());
    int rh = _wtoi(fields[3].value.c_str());
    if (name.empty())
        name = defName;
    if (url.empty())
    {
        MessageBoxW(m_hwnd, L"URL is required.", APP_TITLE_WITH_VERSION,
            MB_OK | MB_ICONWARNING);
        return;
    }
    if (rw < 16) rw = 16;
    if (rh < 16) rh = 16;

    SourceSettings s;
    s.name = name;
    s.url = url;
    s.hotkey = "none";
    s.customCss = SceneFile::DefaultCss();
    // Default rect: render size clamped into the scene, centered
    s.width = (std::min)(rw, m_model.sceneWidth);
    s.height = (std::min)(rh, m_model.sceneHeight);
    s.x = (m_model.sceneWidth - s.width) / 2;
    s.y = (m_model.sceneHeight - s.height) / 2;
    s.renderWidth = rw;
    s.renderHeight = rh;
    s.visible = true;
    s.muted = false; // new sources play audio until muted (OBS-like)

    m_model.sources.push_back(s);
    CreateSource(m_model.sources.size() - 1);

    m_selection = SourceCount() - 1;
    RebuildHotkeys();
    RefreshAll();
    SaveSoon();
}

void App::OpenSceneSettings()
{
    std::vector<Ui::Field> fields;
    fields.push_back({ L"Spout sender name", Ui::FromUtf8(m_model.senderName) });
    fields.push_back({ L"Scene width", std::to_wstring(m_model.sceneWidth) });
    fields.push_back({ L"Scene height", std::to_wstring(m_model.sceneHeight) });
    fields.push_back({ L"Send FPS cap (1-240)", std::to_wstring(m_model.sendFps) });
    if (!Ui::InputDialog(m_hwnd, L"Scene Settings", fields, 420))
        return;

    std::string sender = Ui::ToUtf8(Ui::TrimW(fields[0].value));
    int w = _wtoi(fields[1].value.c_str());
    int h = _wtoi(fields[2].value.c_str());
    int fps = _wtoi(fields[3].value.c_str());

    if (sender.empty()) sender = "WebStage";
    w = (std::max)(64, (std::min)(w, 7680));
    h = (std::max)(64, (std::min)(h, 4320));
    fps = (std::max)(1, (std::min)(fps, 240));

    bool sizeChanged = (w != m_model.sceneWidth || h != m_model.sceneHeight);
    // Auto-scale all sources by the same percentage so the scene keeps its
    // layout when the scene resolution changes.
    double sx = m_model.sceneWidth > 0 ? (double)w / (double)m_model.sceneWidth : 1.0;
    double sy = m_model.sceneHeight > 0 ? (double)h / (double)m_model.sceneHeight : 1.0;
    m_model.senderName = sender;
    m_model.sceneWidth = w;
    m_model.sceneHeight = h;
    m_model.sendFps = fps;

    if (sizeChanged)
    {
        m_rig.CreateSceneTexture(w, h);
        for (auto& s : m_model.sources)
        {
            s.x = (int)(s.x * sx);
            s.y = (int)(s.y * sy);
            s.width = (std::max)(16, (int)(s.width * sx));
            s.height = (std::max)(16, (int)(s.height * sy));
        }
        for (size_t i = 0; i < m_sources.size() && i < m_model.sources.size(); i++)
        {
            if (m_sources[i])
                m_sources[i]->ApplyRect(m_model.sources[i]);
        }
    }
    m_canvas.ApplySettings(sender, fps);

    Layout();
    RefreshAll();
    Recomposite();
    SaveSoon();
}

void App::SetSending(bool sending)
{
    m_sending = sending;
    m_canvas.SetSending(sending);
    if (!sending)
        m_canvas.SendEmptyFrame(); // wipe the receiver on the way out
    // The pill draws Active/Stopped from state; just repaint it.
    if (m_btnSend)
        InvalidateRect(m_btnSend, nullptr, TRUE);
    UpdateTrayIcon();
    UpdateStatus();
}

void App::InitTooltips()
{
    m_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_NOPREFIX,
        0, 0, 0, 0, m_hwnd, nullptr, m_instance, nullptr);
    if (!m_tip)
        return;
    // Dark tooltip to match the theme (border stays system-drawn)
    SendMessageW(m_tip, TTM_SETTIPBKCOLOR, (WPARAM)Ui::BgEdit(), 0);
    SendMessageW(m_tip, TTM_SETTIPTEXTCOLOR, (WPARAM)Ui::Fg(), 0);
    SendMessageW(m_tip, TTM_SETMAXTIPWIDTH, 0, (LPARAM)400);
    auto add = [&](HWND ctl, const wchar_t* text) {
        if (!ctl)
            return;
        TOOLINFOW ti = { sizeof(ti) };
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = m_hwnd;
        ti.uId = (UINT_PTR)ctl;
        ti.lpszText = (LPWSTR)text;
        SendMessageW(m_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    };
    add(m_btnAdd, L"Add source");
    add(m_btnRemove, L"Remove source");
    add(m_btnUp, L"Move source up");
    add(m_btnDown, L"Move source down");
    add(m_btnScene, L"Scene settings");
    add(m_btnSend, L"Toggle Spout output");
    add(m_btnCollapse, L"Expand or collapse the side panel");
}

// ============================================================================
// Runtime sources (CEF browsers)
// ============================================================================
void App::CreateAllSources()
{
    for (size_t i = 0; i < m_model.sources.size(); i++)
        CreateSource(i);
}

void App::CreateSource(size_t index)
{
    if (index >= m_model.sources.size())
        return;
    // Keep runtime vector aligned with the model
    while (m_sources.size() <= index)
        m_sources.push_back(nullptr);
    auto ws = std::make_unique<WebSource>(&m_rig, &m_mixer, m_hwnd, (int)index);
    WebSource* raw = ws.get();
    m_sources[index] = std::move(ws);
    raw->Create(m_model.sources[index], [this](bool) {
        if (m_shuttingDown)
            return;
        RefreshAll();
        Recomposite();
    });
}

void App::OnSourceReady(int index)
{
    if (m_shuttingDown || index < 0 || index >= (int)m_sources.size())
        return;
    if (m_sources[(size_t)index])
    {
        m_sources[(size_t)index]->SetVisible(
            m_model.sources[(size_t)index].visible);
    }
    RefreshAll();
    Recomposite();
}

void App::OnSourceFrame(int index)
{
    if (m_shuttingDown || index < 0 || index >= (int)m_sources.size())
        return;
    WebSource* ws = m_sources[(size_t)index].get();
    if (!ws)
        return;
    if (!ws->DrainFrame())
        return;
    m_canvas.Composite(m_model, m_sources);
    m_preview.Refresh();
}

// ============================================================================
// Tray
// ============================================================================
void App::AddToTray()
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(m_instance, MAKEINTRESOURCEW(IDI_APPICON));
    if (!nid.hIcon)
        nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, APP_TITLE_WITH_VERSION);
    Shell_NotifyIconW(NIM_ADD, &nid);
    m_inTray = true;
    UpdateTrayIcon();
}

void App::UpdateTrayIcon()
{
    if (!m_inTray)
        return;
    // Stopped sender gets the badged icon so the tray reads at a glance.
    HICON icon = LoadIconW(m_instance,
        MAKEINTRESOURCEW(m_sending ? IDI_APPICON : IDI_TRAYSTOP));
    if (!icon)
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON;
    nid.hIcon = icon;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void App::RemoveFromTray()
{
    if (!m_inTray)
        return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    m_inTray = false;
}

void App::RestoreWindow()
{
    ShowWindow(m_hwnd, SW_RESTORE);
    SetForegroundWindow(m_hwnd);
}

void App::ShowTrayMenu()
{
    HMENU menu = Ui::CreateDarkMenu();
    Ui::AddDarkMenuHeader(menu, APP_TITLE_WITH_VERSION);
    Ui::AddDarkMenuSeparator(menu);
    Ui::AddDarkMenuItem(menu, TrayRestore, L"Restore Window");
    Ui::AddDarkMenuItem(menu, TrayScene, L"Scene Settings");
    Ui::AddDarkMenuItem(menu, TraySend, L"Send Output", m_sending);
    Ui::AddDarkMenuSeparator(menu);
    Ui::AddDarkMenuItem(menu, TrayAbout, L"About WebStage");
    Ui::AddDarkMenuItem(menu, TrayExit, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    int cmd = Ui::TrackDarkMenu(menu, m_hwnd, pt.x, pt.y);
    DestroyMenu(menu);

    if (cmd == TrayRestore)
        RestoreWindow();
    else if (cmd == TrayScene)
        OpenSceneSettings();
    else if (cmd == TraySend)
        SetSending(!m_sending);
    else if (cmd == TrayAbout)
        // TODO: point at the real repo once created.
        ShellExecuteW(nullptr, L"open",
            L"https://github.com/Marsic1/WebStage", nullptr, nullptr, SW_SHOWNORMAL);
    else if (cmd == TrayExit)
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
}

// ============================================================================
// Window procedure
// ============================================================================
LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    App* app = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)app);
        app->m_hwnd = hwnd;
    }
    else
    {
        app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!app)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            // Remember the user-resized width per mode (restored on toggle)
            RECT wr = {};
            if (GetWindowRect(hwnd, &wr))
            {
                if (app->m_panelOpen)
                    app->m_expandedW = wr.right - wr.left;
                else
                    app->m_compactW = wr.right - wr.left;
            }
            app->Layout();
        }
        return 0;

    case WM_DPICHANGED:
        // Moving across monitors: rescale fonts + layout, adopt the
        // suggested rect.
    {
        Ui::SetActiveDpi(HIWORD(wParam));
        Ui::ResetFonts();
        RECT* const prc = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, prc->left, prc->top,
            prc->right - prc->left, prc->bottom - prc->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        app->Layout();
        app->RefreshAll();
        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        auto mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        // Per-mode minimums sized so no toolbar/list/panel part clips.
        mmi->ptMinTrackSize.x = Ui::Scale(hwnd, app->m_panelOpen ? 1250 : 500);
        mmi->ptMinTrackSize.y = Ui::Scale(hwnd, 660);
        return 0;
    }

    case WM_ERASEBKGND:
    {
        HDC dc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH b = Ui::DarkBgBrush();
        FillRect(dc, &rc, b);
        return 1;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC dc = (HDC)wParam;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, Ui::Fg());
        SetBkColor(dc, Ui::Bg());
        return (LRESULT)Ui::DarkBgBrush();
    }

    case WM_MEASUREITEM:
    {
        auto mis = reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);
        if (mis && Ui::MeasureDarkMenuItem(hwnd, mis))
            return TRUE;
        break;
    }

    case WM_DRAWITEM:
    {
        auto dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis && dis->CtlType == ODT_STATIC && dis->CtlID == IdStatus)
        {
            app->DrawStatusStrip(dis);
            return TRUE;
        }
        if (dis && dis->CtlType == ODT_MENU && Ui::DrawDarkMenuItem(hwnd, dis))
            return TRUE;
        if (dis && dis->CtlType == ODT_BUTTON)
        {
            if (dis->CtlID == (UINT)CmdSend)
            {
                if (Ui::DrawSendToggle(dis, app->m_sending))
                    return TRUE;
            }
            else if (Ui::DrawDarkButton(dis))
                return TRUE;
        }
        break;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (code == BN_CLICKED)
        {
            switch (id)
            {
            case CmdAdd: app->OpenAddSource(); return 0;
            case CmdRemove:
                if (app->m_selection >= 0) app->DeleteSource(app->m_selection);
                return 0;
            case CmdUp:
                if (app->m_selection >= 0) app->MoveSource(app->m_selection, -1);
                return 0;
            case CmdDown:
                if (app->m_selection >= 0) app->MoveSource(app->m_selection, +1);
                return 0;
            case CmdScene: app->OpenSceneSettings(); return 0;
            case CmdSend: app->SetSending(!app->m_sending); return 0;
            case CmdCollapse:
            {
                app->m_panelOpen = !app->m_panelOpen;
                // Restore the remembered width for the mode being entered
                // (user-resized widths stick); fall back to a fixed step.
                RECT wr = {};
                if (GetWindowRect(app->m_hwnd, &wr))
                {
                    int curW = wr.right - wr.left;
                    int newW;
                    if (app->m_panelOpen)
                    {
                        app->m_compactW = curW;
                        newW = app->m_expandedW > 0 ? app->m_expandedW
                            : curW + Ui::Scale(app->m_hwnd, 640);
                    }
                    else
                    {
                        app->m_expandedW = curW;
                        newW = app->m_compactW > 0 ? app->m_compactW
                            : curW - Ui::Scale(app->m_hwnd, 640);
                    }
                    int minW = Ui::Scale(app->m_hwnd, app->m_panelOpen ? 1250 : 500);
                    if (newW < minW)
                        newW = minW;
                    SetWindowPos(app->m_hwnd, nullptr, 0, 0, newW,
                        wr.bottom - wr.top,
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
                app->Layout();
                // Drop focus back to the frame: a lingering focus ring on
                // the strip reads as panel state, which it is not.
                SetFocus(app->m_hwnd);
                return 0;
            }
            }
        }
        break;
    }

    case WM_HOTKEY:
    {
        int index = (int)(wParam - HK_SOURCE_BASE);
        if (index >= 0 && index < app->SourceCount())
            app->ToggleVisible(index);
        return 0;
    }

    case WM_SOURCE_FRAME:
        app->OnSourceFrame((int)wParam);
        return 0;

    case WM_SOURCE_READY:
        app->OnSourceReady((int)wParam);
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_SAVE)
            app->SaveNow();
        else if (wParam == TIMER_STATUS)
        {
            app->UpdateStatus();
            app->CheckAutoTest();
            // Occlusion recovery: static scenes stop presenting while
            // covered and DXGI keeps dropping frames until a present
            // succeeds, so keep repainting until one lands (skipped
            // while minimized; restore repaints via WM_SIZE anyway).
            if (app->m_rig.IsOccluded() && !IsIconic(hwnd))
                app->m_preview.Refresh();
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE)
        {
            ShowWindow(hwnd, SW_HIDE);
            if (!app->m_trayNotified)
            {
                app->m_trayNotified = true;
                NOTIFYICONDATAW nid = {};
                nid.cbSize = sizeof(nid);
                nid.hWnd = hwnd;
                nid.uID = 1;
                nid.uFlags = NIF_INFO;
                nid.dwInfoFlags = NIIF_INFO;
                nid.uTimeout = 5000;
                wcscpy_s(nid.szInfoTitle, APP_TITLE_WITH_VERSION);
                wcscpy_s(nid.szInfo,
                    L"Minimized to tray. Double-click the tray icon to restore.");
                Shell_NotifyIconW(NIM_MODIFY, &nid);
            }
            return 0;
        }
        break;

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK)
            app->RestoreWindow();
        else if (lParam == WM_RBUTTONUP)
            app->ShowTrayMenu();
        return 0;

    case WM_CLOSE:
    {
        int r = Ui::ConfirmDialog(hwnd,
            APP_TITLE_WITH_VERSION,
            L"Quit WebStage? The scene will be saved.");
        if (r != IDYES)
            return 0;
        app->SaveNow();
        app->Shutdown();
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}


