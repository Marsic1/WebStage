#pragma once

// ============================================================================
// WebStage - Web Source (CEF)
// One HTTP overlay page rendered offscreen by Chromium Embedded Framework
// (windowless, transparent painting) into a BGRA+alpha CPU buffer, uploaded
// to a D3D11 texture for the scene compositor. No window, no occlusion
// issues, no chroma key: real per-pixel alpha like OBS browser sources.
// Audio flows through CefAudioHandler into the shared AudioMixer (mute +
// per-source volume). Hide = WasHidden(true) so Chromium stops rendering.
//
// CEF callbacks arrive on CEF threads; all shared state lives in a
// shared_ptr<State> also held by the CefClient, so callbacks can never
// touch a destroyed WebSource. Texture copies happen only on the main
// thread; the CEF thread only opens shared handles (ID3D11Device is
// thread-safe) and parks them in a mailbox (see State).
// ============================================================================

#include <Windows.h>
#include <d3d11.h>
#include <wil/com.h>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "cef_app.h"
#include "cef_client.h"
#include "cef_browser.h"

#include "Scene.h"

class GraphicsRig;
class AudioMixer;

// Posted to the notify window when this source has a new frame
// (wParam = source index). Main thread drains via DrainFrame().
constexpr UINT WM_SOURCE_FRAME = WM_APP + 3;

// Posted when the browser exists (wParam = source index).
constexpr UINT WM_SOURCE_READY = WM_APP + 4;

class WebSource
{
public:
    WebSource(GraphicsRig* rig, AudioMixer* mixer, HWND notifyWnd, int index);
    ~WebSource();

    WebSource(const WebSource&) = delete;
    WebSource& operator=(const WebSource&) = delete;

    // Async creation; onReady(true) once the browser exists and navigates.
    void Create(const SourceSettings& settings, std::function<void(bool)> onReady);

    void Close();
    bool IsReady() const;

    void SetIndex(int index);

    // Stable mixer stream key: unlike the list index it survives
    // reorder/delete, so volume/mute routing never breaks.
    int AudioKey() const;

    // Drain the queued CPU frame into the D3D texture (main thread).
    // Returns true when a new frame was uploaded.
    bool DrainFrame();
    ID3D11Texture2D* LatestTexture() const;
    ID3D11ShaderResourceView* LatestSrv() const; // cached, may be null

    // --cpu-paint escape hatch (see .cpp): force CPU OnPaint for all sources.
    static void SetForceCpuPaint(bool on);

    void ApplyRect(const SourceSettings& settings);
    void SetVisible(bool visible);
    void SetUrl(const std::wstring& url);
    void SetRenderSize(int renderWidth, int renderHeight);
    void SetMuted(bool muted);
    void SetVolume(int volume01); // 0..100
    void SetCss(const std::string& cssUtf8);

    // Host-window era leftover: kept for interface compatibility (no-op).
    HWND Host() const { return nullptr; }

private:
    struct State;
    class SourceClient;
    class InvalidateTask;
    std::shared_ptr<State> m_state;

    static std::wstring BuildCssScript(const std::string& cssUtf8);
    static void InjectCss(const std::shared_ptr<State>& st);
    static void ApplyAudio(const std::shared_ptr<State>& st);
    static void InjectCssFor(CefRefPtr<CefBrowser> browser,
        const std::shared_ptr<State>& st);
    static void ApplyAudioFor(const std::shared_ptr<State>& st);
    // (Re)create the persistent texture + cached SRV when the size changed
    // (main thread). Shared by the CPU and accelerated drain paths.
    static bool EnsureLatest(const std::shared_ptr<State>& st, int w, int h);
    // One-time downgrade to CPU OnPaint when the GPU handoff is unusable.
    // Runs synchronously on the CEF thread using only State (never the
    // WebSource), so a concurrently-destroyed owner can't dangle. Setters
    // landing mid-downgrade still update settings (re-applied below and in
    // OnAfterCreated); only a URL change racing this window needs a retype.
    static void RequestCpuFallback(const std::shared_ptr<State>& st,
        const wchar_t* why);
};

