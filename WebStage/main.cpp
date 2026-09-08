// ============================================================================
// WebStage - Main Entry Point
// Lightweight OBS replacement: composites HTTP overlay pages into a scene
// and sends it over Spout2 (SpoutDX) to Spout2OverlayHUD.
//
// The same exe also hosts CEF subprocesses (renderer, GPU, ...): CefExecuteProcess
// below dispatches those roles and returns before the app ever starts.
// ============================================================================

#include "App.h"
#include "CefBridge.h"
#include "Ui.h"
#include "WebSource.h"

#include "cef_app.h"

#include <processthreadsapi.h>
#include <winrt/base.h>

// ============================================================================
// Disable Eco QoS to prevent CPU throttling (adapted from VDONinjaPlayer)
// ============================================================================
static void DisableEcoQoS()
{
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling,
        &state, sizeof(state));
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    // CEF subprocess roles (renderer/GPU/utility): handle and exit.
    // The app object carries the render-process V8 extension, so every
    // renderer gets it (window.obsstudio stub for OBS parity).
    {
        CefMainArgs mainArgs(hInstance);
        CefRefPtr<CefApp> app = CefBridge::CreateApp();
        int exitCode = CefExecuteProcess(mainArgs, app, nullptr);
        if (exitCode >= 0)
            return exitCode;
    }

    // Single-threaded apartment: required by C++/WinRT capture interop
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    // Prioritize steady rendering (same policy as VDONinjaPlayer)
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    DisableEcoQoS();

    App app;
    if (!app.Initialize(hInstance))
        return -1;

    // Optional automation/diagnostics:
    //   --snapshot <png path>  save the scene texture (frames and/or delay trigger)
    //   --snapshot-frames <N>  frames threshold (default 30, 0 = disabled)
    //   --snapshot-delay <sec> time threshold (default 0 = disabled)
    //   --quit-after <sec>     exit automatically (0 = stay running)
    //   --verbose              per-frame diagnostic logging (off by default:
    //                          each line is a file open/write/close)
    //   --cpu-paint            force CPU OnPaint for all sources (skip the
    //                          GPU shared-texture handoff)
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::wstring snapPath;
        unsigned long long snapFrames = 30;
        int snapDelay = 0;
        int quitAfter = 0;
        for (int i = 1; i < argc; i++)
        {
            std::wstring a = argv[i] ? argv[i] : L"";
            if (a == L"--snapshot" && i + 1 < argc)
                snapPath = argv[++i];
            else if (a == L"--snapshot-frames" && i + 1 < argc)
                snapFrames = (unsigned long long)_wtoi(argv[++i]);
            else if (a == L"--snapshot-delay" && i + 1 < argc)
                snapDelay = _wtoi(argv[++i]);
            else if (a == L"--quit-after" && i + 1 < argc)
                quitAfter = _wtoi(argv[++i]);
            else if (a == L"--verbose")
                Ui::SetVerbose(true);
            else if (a == L"--cpu-paint")
                WebSource::SetForceCpuPaint(true);
        }
        LocalFree(argv);
        if (Ui::IsVerbose())
            Ui::Log(L"Verbose logging on");
        if (!snapPath.empty() || quitAfter > 0)
            app.SetAutoTest(snapPath, snapFrames, snapDelay, quitAfter);
    }

    return app.Run();
}

