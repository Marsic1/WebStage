// ============================================================================
// WebStage - CEF Bridge Implementation
// ============================================================================

#include "CefBridge.h"
#include "Ui.h"

#include "cef_app.h"
#include "cef_client.h"
#include "cef_frame.h"
#include "cef_render_process_handler.h"
#include "cef_v8.h"

#include <shlobj.h>

namespace CefBridge
{

namespace
{

std::atomic<int> g_browsers{ 0 };

// Minimal window.obsstudio stub (OBS parity): pages like SocialStream
// Ninja gate their studio behavior (hidden dock menu, OBS body class,
// output scaling) on this object, which obs-browser injects natively.
// Injected per-context before any page script runs (OnContextCreated),
// http(s) pages only: data:/about:blank contexts are left alone.
// Pages can still opt out at runtime (e.g. SocialStream's &notobs).
constexpr const char* kObsStudioJs = R"js(
(function () {
  if (window.obsstudio) return;
  function fire(cb, val) {
    try { if (typeof cb === 'function') cb(val); } catch (e) {}
  }
  window.obsstudio = {
    getVersion: function (cb) { fire(cb, 'WebStage'); },
    getStatus: function (cb) {
      fire(cb, {
        recording: false,
        recordingPaused: false,
        streaming: false,
        replayBufferActive: false,
        virtualcamActive: false
      });
    }
  };
})();
)js";

// Minimal CefApp (no custom process handlers needed: default render process
// behavior is fine for overlay pages).
class BridgeApp : public CefApp,
                  public CefBrowserProcessHandler,
                  public CefRenderProcessHandler
{
public:
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override
    {
        return this;
    }

    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override
    {
        return this;
    }

    void OnWebKitInitialized() override
    {
        // Intentionally no CefRegisterExtension: registering a global V8
        // extension crashes renderers on instant (data:) navigations.
        // The stub is injected per-context in OnContextCreated instead.
    }

    void OnContextCreated(CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefV8Context> /*context*/) override
    {
        if (!browser || !frame)
            return;
        std::string url = frame->GetURL().ToString();
        if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0)
            return;
        frame->ExecuteJavaScript(kObsStudioJs, url, 0);
    }

    void OnBeforeCommandLineProcessing(const CefString& /*process_type*/,
        CefRefPtr<CefCommandLine> command_line) override
    {
        // Display-only overlays:allow media playback without user gestures.
        // (Subprocesses inherit these switches automatically.)
        command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
        // Never throttle background renderers/timers: our browsers have no
        // visible window by design (windowless OSR).
        command_line->AppendSwitch("disable-background-timer-throttling");
        command_line->AppendSwitch("disable-backgrounding-occluded-windows");
        command_line->AppendSwitch("disable-renderer-backgrounding");
        command_line->AppendSwitchWithValue("disable-features",
            "CalculateNativeWinOcclusion,IntensiveWakeUpThrottling");
        // Exact captured colors on HDR/wide-gamut displays.
        command_line->AppendSwitchWithValue("force-color-profile", "srgb");
    }

    IMPLEMENT_REFCOUNTING(BridgeApp);
};

} // namespace

std::wstring CacheDir()
{
    wchar_t appdata[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appdata)))
        return L"";
    return std::wstring(appdata) + L"\\WebStage\\CEF";
}
CefRefPtr<CefApp> CreateApp()
{
    return CefRefPtr<CefApp>(new BridgeApp);
}

bool Initialize(HINSTANCE instance)
{

    std::wstring cache = CacheDir();
    if (!cache.empty())
        SHCreateDirectoryExW(nullptr, cache.c_str(), nullptr);

    CefMainArgs args(instance);

    CefSettings settings;
    settings.no_sandbox = true;
    settings.multi_threaded_message_loop = true;
    settings.windowless_rendering_enabled = true;
#ifdef WS_CEF_DEBUG_LOG
    settings.log_severity = LOGSEVERITY_INFO;
    wchar_t temp[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, temp))
    {
        std::wstring logFile = std::wstring(temp) + L"cef_debug.log";
        CefString(&settings.log_file) = logFile.c_str();
    }
#else
    settings.log_severity = LOGSEVERITY_DISABLE;
#endif
    settings.persist_session_cookies = true;
    CefString(&settings.locale) = "en-US";
    CefString(&settings.accept_language_list) = "en-US,en";
    if (!cache.empty())
    {
        CefString(&settings.cache_path) = cache.c_str();
        CefString(&settings.root_cache_path) = cache.c_str();
    }

    CefRefPtr<BridgeApp> app(new BridgeApp);
    if (!CefInitialize(args, settings, app.get(), nullptr))
    {
        Ui::Log(L"CEF: CefInitialize failed");
        return false;
    }

    Ui::Log(L"CEF: initialized");
    return true;
}

void Shutdown()
{
    // Browsers must all be closed first (App closes them, then calls this).
    // Wait briefly for OnBeforeClose to fire on the CEF UI thread.
    for (int i = 0; i < 60 && g_browsers.load() > 0; i++)
        Sleep(50);

    CefShutdown();
    Ui::Log(L"CEF: shutdown");
}

void NotifyBrowserCreated()
{
    g_browsers.fetch_add(1);
}

void NotifyBrowserClosed()
{
    g_browsers.fetch_sub(1);
}

int BrowserCount()
{
    return g_browsers.load();
}

} // namespace CefBridge


