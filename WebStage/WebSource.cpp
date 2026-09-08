// ============================================================================
// WebStage - Web Source (CEF) Implementation
// ============================================================================

#include "WebSource.h"
#include "AudioMixer.h"
#include "CefBridge.h"
#include "GraphicsRig.h"
#include "Ui.h"

#include "cef_app.h"
#include "cef_client.h"
#include "cef_render_handler.h"
#include "cef_life_span_handler.h"
#include "cef_load_handler.h"
#include "cef_audio_handler.h"
#include "cef_request_handler.h"
#include "cef_jsdialog_handler.h"
#include "cef_permission_handler.h"
#include "cef_frame.h"
#include "cef_browser.h"
#include "cef_task.h"

#include <atomic>
#include <d3d11_1.h>

namespace
{

// Process-wide stable audio stream keys (never reused, never shifted).
std::atomic<int> s_nextAudioKey{ 1 };

// --cpu-paint: skip the GPU shared-texture handoff and use CPU OnPaint
// for every source (diagnostic escape hatch for broken GPU stacks).
bool g_forceCpuPaint = false;

}

struct WebSource::State
{
    GraphicsRig* rig = nullptr;
    AudioMixer* mixer = nullptr;
    HWND notifyWnd = nullptr;
    int index = -1;

    CefRefPtr<CefBrowser> browser;
    CefRefPtr<WebSource::SourceClient> client;

    SourceSettings settings;
    std::function<void(bool)> onReady;
    bool ready = false;
    bool closed = false;
    bool navigated = false;
    bool pendingMuted = true;
    int pendingVolume = 100;
    std::string pendingCss;

    // Pending CPU frame from OnPaint (CEF thread) -> DrainFrame (main thread)
    std::mutex frameMutex;
    std::vector<uint8_t> pending;
    int pendingW = 0;
    int pendingH = 0;
    bool hasPending = false;

    // Accelerated frame: with shared_texture_enabled CEF renders on the GPU
    // and hands us a shared handle per frame (OnAcceleratedPaint). The CEF
    // thread only OPENS the handle (ID3D11Device is thread-safe) and parks
    // the texture in this mailbox; the main thread copies it into `latest`
    // in DrainFrame, so texture lifetime stays single-threaded and the
    // compositor needs no locks. Zero CPU readback. If the handoff is
    // unavailable the browser is recreated once without it (CPU OnPaint).
    bool useSharedTex = true;
    bool accelFailed = false;
    bool accelLogged = false; // one-time "GPU path active" line per source
    wil::com_ptr<ID3D11Device1> device1; // QI'd once at Create (main thread)
    wil::com_ptr<ID3D11Texture2D> accelMail;
    int accelW = 0;
    int accelH = 0;
    bool hasAccel = false;

    // Persistent D3D texture (main thread only) + one cached SRV so the
    // compositor never pays for view creation per frame.
    wil::com_ptr<ID3D11Texture2D> latest;
    wil::com_ptr<ID3D11ShaderResourceView> latestSrv;
    int texW = 0;
    int texH = 0;

    // Audio stream format (audio thread writes, mixer reads under its lock)
    int audioChannels = 2;
    int audioLayout = 0; // cef_channel_layout_t as int

    // Stable mixer key (see WebSource::AudioKey)
    int audioKey = 0;
};

// ============================================================================
// InvalidateTask - deferred forced repaint (see OnLoadEnd)
// ============================================================================
class WebSource::InvalidateTask : public CefTask
{
public:
    InvalidateTask(CefRefPtr<CefBrowser> browser, std::shared_ptr<State> state)
        : m_browser(browser), m_state(std::move(state)) {}

    void Execute() override
    {
        if (!m_state->closed && m_browser && m_browser->GetHost())
            m_browser->GetHost()->Invalidate(PET_VIEW);
    }

    IMPLEMENT_REFCOUNTING(InvalidateTask);

private:
    CefRefPtr<CefBrowser> m_browser;
    std::shared_ptr<WebSource::State> m_state;
};

// ============================================================================
// SourceClient - all CEF handlers for one source
// ============================================================================
class WebSource::SourceClient : public CefClient,
                                public CefLifeSpanHandler,
                                public CefRenderHandler,
                                public CefLoadHandler,
                                public CefAudioHandler,
                                public CefRequestHandler,
                                public CefJSDialogHandler,
                                public CefPermissionHandler
{
public:
    explicit SourceClient(std::shared_ptr<State> state) : m_state(std::move(state)) {}

    // CefClient
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefAudioHandler> GetAudioHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
    CefRefPtr<CefPermissionHandler> GetPermissionHandler() override { return this; }

    // CefLifeSpanHandler
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override
    {
        auto st = m_state;
        st->browser = browser;
        CefBridge::NotifyBrowserCreated();
        Ui::Log(L"Source: browser created id=%d", browser->GetIdentifier());
        // Initial state (browser did not exist at Create() time)
        browser->GetHost()->WasHidden(!st->settings.visible);
        WebSource::ApplyAudioFor(st);
        PostMessageW(st->notifyWnd, WM_SOURCE_READY, (WPARAM)st->index, 0);
    }

    void OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) override
    {
        CefBridge::NotifyBrowserClosed();
    }

    bool OnBeforePopup(CefRefPtr<CefBrowser> /*browser*/,
        CefRefPtr<CefFrame> /*frame*/,
        int /*popup_id*/,
        const CefString& /*target_url*/,
        const CefString& /*target_frame_name*/,
        CefLifeSpanHandler::WindowOpenDisposition /*target_disposition*/,
        bool /*user_gesture*/,
        const CefPopupFeatures& /*popupFeatures*/,
        CefWindowInfo& /*windowInfo*/,
        CefRefPtr<CefClient>& /*client*/,
        CefBrowserSettings& /*settings*/,
        CefRefPtr<CefDictionaryValue>& /*extra_info*/,
        bool* /*no_javascript_access*/) override
    {
        return true; // display-only overlays: no popups
    }

    // CefRenderHandler
    void GetViewRect(CefRefPtr<CefBrowser> /*browser*/, CefRect& rect) override
    {
        auto st = m_state;
        rect.x = 0;
        rect.y = 0;
        rect.width = st->settings.renderWidth > 0 ? st->settings.renderWidth : 16;
        rect.height = st->settings.renderHeight > 0 ? st->settings.renderHeight : 16;
    }

    bool GetScreenInfo(CefRefPtr<CefBrowser> /*browser*/, CefScreenInfo& screenInfo) override
    {
        // Deterministic 1:1 raster regardless of monitor DPI
        screenInfo.device_scale_factor = 1.0f;
        screenInfo.depth = 24;
        screenInfo.depth_per_component = 8;
        screenInfo.is_monochrome = false;
        auto st = m_state;
        screenInfo.rect.x = 0;
        screenInfo.rect.y = 0;
        screenInfo.rect.width = st->settings.renderWidth;
        screenInfo.rect.height = st->settings.renderHeight;
        screenInfo.available_rect.x = 0;
        screenInfo.available_rect.y = 0;
        screenInfo.available_rect.width = st->settings.renderWidth;
        screenInfo.available_rect.height = st->settings.renderHeight;
        return true;
    }
    void OnPaint(CefRefPtr<CefBrowser> /*browser*/,
        PaintElementType type,
        const RectList& /*dirtyRects*/,
        const void* buffer,
        int width,
        int height) override
    {
        if (type != PET_VIEW || !buffer || width <= 0 || height <= 0)
            return;
        auto st = m_state;
        if (st->closed)
            return;
        {
            std::lock_guard<std::mutex> lock(st->frameMutex);
            size_t bytes = (size_t)width * (size_t)height * 4;
            st->pending.resize(bytes);
            memcpy(st->pending.data(), buffer, bytes);
            st->pendingW = width;
            st->pendingH = height;
            st->hasPending = true;
        }
        static LONG s_paintLog = 0;
        if (Ui::IsVerbose() && InterlockedIncrement(&s_paintLog) <= 12)
            Ui::Log(L"Source[%d]: OnPaint %dx%d", st->index, width, height);
        PostMessageW(st->notifyWnd, WM_SOURCE_FRAME, (WPARAM)st->index, 0);
    }

    // GPU frame handoff (see State): open the shared texture and park it in
    // the mailbox; the main thread copies it into `latest` in DrainFrame.
    // Runs on the CEF UI thread. The handle/pool slot must not outlive this
    // callback, so only the device open (thread-safe, reference-counted)
    // happens here; the copy runs later on the main thread, which may pick
    // up a newer frame on pool reuse (harmless tear at worst, never unsafe:
    // our open reference keeps the allocation alive).
    void OnAcceleratedPaint(CefRefPtr<CefBrowser> /*browser*/,
        PaintElementType type,
        const RectList& /*dirtyRects*/,
        const CefAcceleratedPaintInfo& info) override
    {
        if (type != PET_VIEW)
            return;
        auto st = m_state;
        if (st->closed || st->accelFailed || !st->useSharedTex)
            return;
        if (!info.shared_texture_handle ||
            info.format != CEF_COLOR_TYPE_BGRA_8888 || !st->device1)
        {
            WebSource::RequestCpuFallback(st,
                info.format != CEF_COLOR_TYPE_BGRA_8888
                ? L"unexpected shared pixel format" : L"no shared handle");
            return;
        }
        wil::com_ptr<ID3D11Texture2D> opened;
        ID3D11Texture2D* raw = nullptr;
        HRESULT hr = st->device1->OpenSharedResource1(info.shared_texture_handle,
            __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&raw));
        if (FAILED(hr) || !raw)
        {
            if (raw)
                raw->Release();
            WebSource::RequestCpuFallback(st, L"OpenSharedResource1 failed");
            return;
        }
        D3D11_TEXTURE2D_DESC td = {};
        raw->GetDesc(&td);
        if (td.Format != DXGI_FORMAT_B8G8R8A8_UNORM || td.Width == 0 || td.Height == 0)
        {
            raw->Release();
            WebSource::RequestCpuFallback(st, L"unexpected shared texture desc");
            return;
        }
        opened.attach(raw);
        {
            std::lock_guard<std::mutex> lock(st->frameMutex);
            st->accelMail = std::move(opened); // replaces any stale frame
            st->accelW = (int)td.Width;
            st->accelH = (int)td.Height;
            st->hasAccel = true;
        }
        PostMessageW(st->notifyWnd, WM_SOURCE_FRAME, (WPARAM)st->index, 0);
    }

    // CefLoadHandler
    void OnLoadStart(CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> /*frame*/,
        TransitionType /*transition_type*/) override
    {
        WebSource::InjectCssFor(browser, m_state);
    }

    void OnLoadEnd(CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> /*frame*/,
        int httpStatusCode) override
    {
        auto st = m_state;
        Ui::Log(L"Source[%d]: OnLoadEnd status=%d", st->index, httpStatusCode);
        WebSource::InjectCssFor(browser, m_state);
        // Safety net: force a repaint shortly after load. The initial
        // damage burst can precede first paint (blank frame), and static
        // pages produce no further damage on their own.
        for (int delay : { 900, 2500 })
        {
            CefPostDelayedTask(TID_UI,
                new InvalidateTask(browser, st), delay);
        }
    }

    // CefAudioHandler
    bool GetAudioParameters(CefRefPtr<CefBrowser> /*browser*/,
        CefAudioParameters& /*params*/) override
    {
        // Return true to proceed with capture (params carry CEF's defaults;
        // the mixer resamples to its own rate as needed).
        return true;
    }

    void OnAudioStreamStarted(CefRefPtr<CefBrowser> /*browser*/,
        const CefAudioParameters& params,
        int channels) override
    {
        auto st = m_state;
        Ui::Log(L"Source: audio stream %d Hz ch=%d layout=%d",
            params.sample_rate, channels, (int)params.channel_layout);
        st->audioChannels = channels > 0 ? channels : 2;
        st->audioLayout = (int)params.channel_layout;
        if (st->mixer)
        {
            st->mixer->RemoveStream(st->audioKey);
            st->mixer->AddStream(st->audioKey, params.sample_rate);
            WebSource::ApplyAudioFor(st);
        }
    }

    void OnAudioStreamPacket(CefRefPtr<CefBrowser> /*browser*/,
        const float** data,
        int frames,
        int64_t /*pts*/) override
    {
        auto st = m_state;
        if (!data || frames <= 0 || !st->mixer)
            return;
        // Planar float per channel -> interleaved stereo
        m_audioBuf.resize((size_t)frames * 2);
        int ch = st->audioChannels;
        for (int f = 0; f < frames; f++)
        {
            float l = 0.0f, r = 0.0f;
            if (ch == 1)
            {
                l = r = data[0][f];
            }
            else if (ch >= 2)
            {
                l = data[0][f];
                r = data[1][f];
            }
            m_audioBuf[(size_t)f * 2] = l;
            m_audioBuf[(size_t)f * 2 + 1] = r;
        }
        st->mixer->Push(st->audioKey, m_audioBuf.data(), frames);
    }

    void OnAudioStreamStopped(CefRefPtr<CefBrowser> /*browser*/) override
    {
        auto st = m_state;
        if (st->mixer)
            st->mixer->RemoveStream(st->audioKey);
    }

    void OnAudioStreamError(CefRefPtr<CefBrowser> /*browser*/,
        const CefString& message) override
    {
        Ui::Log(L"Source: audio error");
        auto st = m_state;
        if (st->mixer)
            st->mixer->RemoveStream(st->audioKey);
    }

    // CefRequestHandler
    bool OnBeforeBrowse(CefRefPtr<CefBrowser> /*browser*/,
        CefRefPtr<CefFrame> /*frame*/,
        CefRefPtr<CefRequest> /*request*/,
        bool /*user_gesture*/,
        bool /*is_redirect*/) override
    {
        return false; // allow top-level navigation (we drive the URL)
    }

    void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
        TerminationStatus status,
        int error_code,
        const CefString& /*error_string*/) override
    {
        auto st = m_state;
        Ui::Log(L"Source: RENDERER TERMINATED status=%d code=%d (reloading)",
            (int)status, error_code);
        // Reload to recover the source instead of staying blank forever
        if (!st->closed && browser)
            browser->Reload();
    }

    bool OnOpenURLFromTab(CefRefPtr<CefBrowser> /*browser*/,
        CefRefPtr<CefFrame> /*frame*/,
        const CefString& /*target_url*/,
        CefRequestHandler::WindowOpenDisposition /*target_disposition*/,
        bool /*user_gesture*/) override
    {
        return true; // display-only: no new tabs/windows
    }

    // CefJSDialogHandler: suppress alerts/confirms/prompts
    bool OnJSDialog(CefRefPtr<CefBrowser> /*browser*/,
        const CefString& /*origin_url*/,
        CefJSDialogHandler::JSDialogType /*dialog_type*/,
        const CefString& /*message_text*/,
        const CefString& /*default_prompt_text*/,
        CefRefPtr<CefJSDialogCallback> /*callback*/,
        bool& suppress_message) override
    {
        suppress_message = true;
        return true;
    }

    // CefPermissionHandler: trusted overlay URLs get what they ask for
    // (camera/mic for VDO publish links, etc.)
    bool OnRequestMediaAccessPermission(
        CefRefPtr<CefBrowser> /*browser*/,
        CefRefPtr<CefFrame> /*frame*/,
        const CefString& /*requesting_origin*/,
        uint32_t requested_permissions,
        CefRefPtr<CefMediaAccessCallback> callback) override
    {
        if (callback && requested_permissions != 0)
            callback->Continue(requested_permissions);
        else if (callback)
            callback->Cancel();
        return true;
    }

    IMPLEMENT_REFCOUNTING(SourceClient);

private:
    std::shared_ptr<State> m_state;
    std::vector<float> m_audioBuf; // audio thread scratch (planar->stereo)
};

WebSource::WebSource(GraphicsRig* rig, AudioMixer* mixer, HWND notifyWnd, int index)
    : m_state(std::make_shared<State>())
{
    m_state->rig = rig;
    m_state->mixer = mixer;
    m_state->notifyWnd = notifyWnd;
    m_state->index = index;
    m_state->audioKey = s_nextAudioKey.fetch_add(1);
}

int WebSource::AudioKey() const
{
    return m_state->audioKey;
}

void WebSource::SetForceCpuPaint(bool on)
{
    g_forceCpuPaint = on;
}

WebSource::~WebSource()
{
    Close();
}

void WebSource::SetIndex(int index)
{
    m_state->index = index;
}

void WebSource::Create(const SourceSettings& settings, std::function<void(bool)> onReady)
{
    auto st = m_state;
    st->settings = settings;
    st->pendingMuted = settings.muted;
    st->pendingVolume = settings.volume;
    st->pendingCss = settings.customCss;
    st->onReady = std::move(onReady);

    CefWindowInfo wi;
    wi.SetAsWindowless(nullptr);
    // GPU frame handoff (zero CPU readback): CEF renders into a shared
    // D3D11 texture per frame (see OnAcceleratedPaint). If the open/copy
    // fails the browser is recreated once without it (CPU OnPaint).
    wi.shared_texture_enabled =
        (!g_forceCpuPaint && st->useSharedTex) ? TRUE : FALSE;

    CefBrowserSettings bs;
    // OBS browser cadence: 30 fps is plenty for overlays and halves CEF
    // paint work vs 60.
    bs.windowless_frame_rate = 30;
    // NOTE: bs.background_color is deliberately left at its default.
    // Windowless browsers already paint with real per-pixel alpha, and an
    // explicit color was verified to have no observable effect on either
    // body backgrounds or transparent overlay pages (CEF 144).

    st->client = new SourceClient(st);

    // Cache the D3D11.1 device for OpenSharedResource1 (main thread here;
    // read-only on the CEF thread afterwards).
    st->device1.reset();
    if (st->useSharedTex && st->rig && st->rig->Device())
    {
        st->rig->Device()->QueryInterface(__uuidof(ID3D11Device1),
            (void**)&st->device1);
    }
    st->closed = false; // (re)creation clears the Close() poison

    std::string url = Ui::ToUtf8(settings.url);
    if (url.empty())
        url = "about:blank";
    else
        st->navigated = true;

    Ui::Log(L"Source: CreateBrowser");
    if (!CefBrowserHost::CreateBrowser(wi, st->client, url, bs, nullptr, nullptr))
    {
        Ui::Log(L"Source: CreateBrowser failed");
        if (st->onReady) st->onReady(false);
        return;
    }

    // WasHidden applied once the browser exists (also re-applied there);
    // mark intent now so OnAfterCreated honors it.
    st->ready = false;
}

void WebSource::Close()
{
    auto st = m_state;
    st->closed = true;
    if (st->mixer)
        st->mixer->RemoveStream(st->audioKey);
    if (st->browser)
    {
        st->browser->GetHost()->CloseBrowser(true);
        st->browser = nullptr;
    }
    st->client = nullptr;
    st->latest.reset();
    st->latestSrv.reset();
    st->ready = false;
}

bool WebSource::IsReady() const
{
    return m_state->ready;
}

void WebSource::RequestCpuFallback(const std::shared_ptr<State>& st,
    const wchar_t* why)
{
    if (st->accelFailed)
        return;
    st->accelFailed = true;
    st->useSharedTex = false;
    Ui::Log(L"Source[%d]: %s, falling back to CPU paint (recreating browser)",
        st->index, why);

    if (st->browser)
    {
        st->browser->GetHost()->CloseBrowser(true);
        st->browser = nullptr;
    }
    st->client = nullptr;
    st->latest.reset();
    st->latestSrv.reset();
    st->texW = st->texH = 0;
    {
        std::lock_guard<std::mutex> lock(st->frameMutex);
        st->accelMail.reset();
        st->hasAccel = false;
        st->hasPending = false;
        st->pending.clear();
    }
    st->closed = false;
    st->ready = false;

    CefWindowInfo wi;
    wi.SetAsWindowless(nullptr);
    wi.shared_texture_enabled = FALSE;
    CefBrowserSettings bs;
    bs.windowless_frame_rate = 30;
    st->client = new SourceClient(st);
    std::string url = Ui::ToUtf8(st->settings.url);
    if (url.empty())
        url = "about:blank";
    else
        st->navigated = true;
    if (!CefBrowserHost::CreateBrowser(wi, st->client, url, bs, nullptr, nullptr))
        Ui::Log(L"Source[%d]: CPU fallback CreateBrowser failed", st->index);
    // WasHidden/audio/CSS re-apply in OnAfterCreated + InjectCssFor, same
    // as the normal path. st->browser is set there.
}

bool WebSource::EnsureLatest(const std::shared_ptr<State>& st, int w, int h)
{
    // (Re)create the persistent texture (+ cached SRV) when the size changed
    if (!st->latest || st->texW != w || st->texH != h)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = (UINT)w;
        td.Height = (UINT)h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(st->rig->Device()->CreateTexture2D(&td, nullptr, &st->latest)))
            return false;
        st->latestSrv.reset();
        if (FAILED(st->rig->Device()->CreateShaderResourceView(
                st->latest.get(), nullptr, &st->latestSrv)))
            return false;
        st->texW = w;
        st->texH = h;
    }
    return true;
}

bool WebSource::DrainFrame()
{
    auto st = m_state;

    std::vector<uint8_t> buf;
    wil::com_ptr<ID3D11Texture2D> mail;
    int w = 0, h = 0;
    {
        std::lock_guard<std::mutex> lock(st->frameMutex);
        if (st->hasAccel && st->accelMail)
        {
            mail = std::move(st->accelMail);
            w = st->accelW;
            h = st->accelH;
            st->hasAccel = false;
        }
        else
        {
            if (!st->hasPending)
                return false;
            buf.swap(st->pending);
            w = st->pendingW;
            h = st->pendingH;
            st->hasPending = false;
        }
    }
    if (w <= 0 || h <= 0)
        return false;
    if (!EnsureLatest(st, w, h))
        return false;

    if (mail)
    {
        // GPU-GPU copy, no CPU round trip (main thread owns `latest`).
        st->rig->Context()->CopySubresourceRegion(st->latest.get(), 0,
            0, 0, 0, mail.get(), 0, nullptr);
        if (!st->accelLogged)
        {
            st->accelLogged = true;
            Ui::Log(L"Source[%d]: GPU shared texture active %dx%d",
                st->index, w, h);
        }
        return true;
    }
    if (buf.empty())
        return false;

    static LONG s_probe = 0;
    if (Ui::IsVerbose() && InterlockedIncrement(&s_probe) <= 8)
    {
        auto px = [&](int x, int y) -> unsigned {
            if (x < 0 || y < 0 || x >= w || y >= h) return 0xDEADDEAD;
            return *(unsigned*)(buf.data() + ((size_t)y * w + x) * 4);
        };
        Ui::Log(L"DrainProbe[%d]: %dx%d px(0,0)=0x%08X px(c)=0x%08X",
            st->index, w, h, px(0, 0), px(w / 2, h / 2));
    }

    D3D11_BOX box = {};
    box.left = 0; box.top = 0; box.front = 0;
    box.right = (UINT)w; box.bottom = (UINT)h; box.back = 1;
    st->rig->Context()->UpdateSubresource(st->latest.get(), 0, &box,
        buf.data(), (UINT)w * 4, 0);
    return true;
}

ID3D11Texture2D* WebSource::LatestTexture() const
{
    return m_state->latest.get();
}

ID3D11ShaderResourceView* WebSource::LatestSrv() const
{
    return m_state->latestSrv.get();
}

void WebSource::ApplyRect(const SourceSettings& settings)
{
    auto st = m_state;
    st->settings.x = settings.x;
    st->settings.y = settings.y;
    st->settings.width = settings.width;
    st->settings.height = settings.height;
    // Scene placement is applied by the compositor; nothing browser-side.
}

void WebSource::SetVisible(bool visible)
{
    auto st = m_state;
    st->settings.visible = visible;
    if (st->browser)
        st->browser->GetHost()->WasHidden(visible ? false : true);
}

void WebSource::SetUrl(const std::wstring& url)
{
    auto st = m_state;
    st->settings.url = url;
    if (st->browser && !url.empty())
    {
        st->navigated = true;
        st->browser->GetMainFrame()->LoadURL(Ui::ToUtf8(url));
    }
}

void WebSource::SetRenderSize(int renderWidth, int renderHeight)
{
    auto st = m_state;
    st->settings.renderWidth = renderWidth > 0 ? renderWidth : 16;
    st->settings.renderHeight = renderHeight > 0 ? renderHeight : 16;
    if (st->browser)
        st->browser->GetHost()->WasResized();
}

void WebSource::SetMuted(bool muted)
{
    auto st = m_state;
    st->settings.muted = muted;
    st->pendingMuted = muted;
    ApplyAudio(st);
}

void WebSource::SetVolume(int volume01)
{
    auto st = m_state;
    if (volume01 < 0) volume01 = 0;
    if (volume01 > 100) volume01 = 100;
    st->settings.volume = volume01;
    st->pendingVolume = volume01;
    ApplyAudio(st);
}

void WebSource::ApplyAudio(const std::shared_ptr<State>& st)
{
    if (st->browser)
        st->browser->GetHost()->SetAudioMuted(st->settings.muted ? true : false);
    if (st->mixer)
        st->mixer->SetSource(st->audioKey,
            (float)st->settings.volume / 100.0f, st->settings.muted);
}

void WebSource::ApplyAudioFor(const std::shared_ptr<State>& st)
{
    ApplyAudio(st);
}

void WebSource::SetCss(const std::string& cssUtf8)
{
    auto st = m_state;
    st->settings.customCss = cssUtf8;
    st->pendingCss = cssUtf8;
    InjectCss(st);
    if (st->browser && st->navigated)
        st->browser->Reload();
}

void WebSource::InjectCss(const std::shared_ptr<State>& st)
{
    InjectCssFor(st->browser, st);
}

void WebSource::InjectCssFor(CefRefPtr<CefBrowser> browser, const std::shared_ptr<State>& st)
{
    if (!browser)
        return;
    CefRefPtr<CefFrame> frame = browser->GetMainFrame();
    if (!frame)
        return;
    std::wstring script = BuildCssScript(st->pendingCss);
    frame->ExecuteJavaScript(Ui::ToUtf8(script), frame->GetURL(), 0);
}

std::wstring WebSource::BuildCssScript(const std::string& cssUtf8)
{
    // Escape for a JS template literal
    std::wstring css = Ui::FromUtf8(cssUtf8);
    std::wstring esc;
    esc.reserve(css.size() + 16);
    for (wchar_t c : css)
    {
        if (c == L'\\') esc += L"\\\\";
        else if (c == L'`') esc += L"\\`";
        else if (c == L'$') esc += L"\\$";
        else esc += c;
    }

    return L"(function(){"
        L"var id='__WS_css__';"
        L"var el=document.getElementById(id);"
        L"if(!el){el=document.createElement('style');el.id=id;"
        L"var p=document.head||document.documentElement;"
        L"if(p){p.appendChild(el);}else{document.addEventListener('DOMContentLoaded',function(){"
        L"var e2=document.createElement('style');e2.id=id;e2.textContent=`" + esc + L"`;"
        L"(document.head||document.documentElement).appendChild(e2);});return;}}"
        L"el.textContent=`" + esc + L"`;"
        L"})();";
}

