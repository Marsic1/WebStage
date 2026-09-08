#pragma once

// ============================================================================
// WebStage - CEF Bridge
// Global Chromium Embedded Framework lifetime: settings, initialize,
// browser counting (for clean shutdown), subprocess dispatch helper.
// ============================================================================

#include <Windows.h>
#include <atomic>
#include <string>

#include "cef_app.h"

namespace CefBridge
{

bool Initialize(HINSTANCE instance);
void Shutdown(); // closes nothing itself; waits for browsers then CefShutdown

// App object shared by the browser process (Initialize) and CEF
// subprocesses (main.cpp CefExecuteProcess): carries the command-line
// tweaks plus the render-process V8 extension (window.obsstudio stub).
CefRefPtr<CefApp> CreateApp();

void NotifyBrowserCreated();
void NotifyBrowserClosed();
int BrowserCount();

// %LOCALAPPDATA%\WebStage\CEF (cache + user data)
std::wstring CacheDir();

} // namespace CefBridge


