// ============================================================================
// WebStage - Canvas (scene compositor) Implementation
// ============================================================================

#include "Canvas.h"
#include "GraphicsRig.h"
#include "WebSource.h"
#include "SpoutDX.h"
#include "Ui.h"

struct Canvas::Impl
{
    GraphicsRig* rig = nullptr;
    spoutDX sender;

    std::string senderName;
    int sendFps = 60;
    bool sending = true;

    long long lastSendQpc = 0;
    long long qpcFreq = 1;
    double sendFpsEwma = 0.0;
    unsigned long long framesSent = 0;
};

Canvas::Canvas(GraphicsRig* rig)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->rig = rig;
    LARGE_INTEGER f = {};
    QueryPerformanceFrequency(&f);
    m_impl->qpcFreq = f.QuadPart ? f.QuadPart : 1;
}

Canvas::~Canvas()
{
    Shutdown();
}

bool Canvas::Initialize(const std::string& senderName, int sendFps)
{
    Impl* im = m_impl.get();
    im->senderName = senderName;
    im->sendFps = sendFps > 0 ? sendFps : 60;

    if (!im->sender.OpenDirectX11(im->rig->Device()))
        return false;
    im->sender.SetSenderName(im->senderName.c_str());
    im->sender.SetSenderFormat(DXGI_FORMAT_B8G8R8A8_UNORM);
    return true;
}

void Canvas::Shutdown()
{
    Impl* im = m_impl.get();
    if (!im)
        return;
    im->sender.ReleaseSender();
    im->sender.CloseDirectX11();
}

void Canvas::ApplySettings(const std::string& senderName, int sendFps)
{
    Impl* im = m_impl.get();
    im->senderName = senderName;
    im->sendFps = sendFps > 0 ? sendFps : 60;
    im->lastSendQpc = 0;

    // Rename needs an explicit release + SetSenderName (size changes are
    // handled internally by spoutDX CheckSender on the next SendTexture).
    im->sender.ReleaseSender();
    im->sender.SetSenderName(im->senderName.c_str());
    im->sender.SetSenderFormat(DXGI_FORMAT_B8G8R8A8_UNORM);
}

void Canvas::Composite(const SceneFile& model,
    const std::vector<std::unique_ptr<WebSource>>& sources)
{
    Impl* im = m_impl.get();

    // Always redraw: static sources may never produce another frame, so a
    // skipped redraw would freeze stale (or blank) content permanently.
    im->rig->BeginComposite();

    size_t n = model.sources.size();
    if (sources.size() < n)
        n = sources.size();
    // OBS order: first in list = on top, so composite back-to-front
    // (row 0 is drawn last).
    for (size_t i = n; i-- > 0;)
    {
        const SourceSettings& s = model.sources[i];
        WebSource* ws = sources[i].get();
        if (!s.visible || !ws)
        {
            if (Ui::IsVerbose())
                Ui::Log(L"Composite: skip %d (vis=%d ws=%p)", (int)i, s.visible ? 1 : 0, ws);
            continue;
        }
        ID3D11Texture2D* tex = ws->LatestTexture();
        if (!tex)
        {
            if (Ui::IsVerbose())
                Ui::Log(L"Composite: skip %d (no tex)", (int)i);
            continue;
        }
        float opacity = (float)(s.opacity < 0 ? 0 : (s.opacity > 100 ? 100 : s.opacity)) / 100.0f;
        if (opacity <= 0.0f)
            continue;
        static LONG s_cmpLog = 0;
        if (Ui::IsVerbose() && InterlockedIncrement(&s_cmpLog) <= 10)
            Ui::Log(L"Composite: draw %d tex=%p rect=(%d,%d,%dx%d) op=%.2f",
                (int)i, tex, s.x, s.y, s.width, s.height, (double)opacity);
        im->rig->DrawSourceQuad(tex, ws->LatestSrv(), s.x, s.y, s.width, s.height, opacity);
    }

    im->rig->EndComposite();

    // Send-rate limiter (sources push frames on change; drop the extras)
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    long long minGap = im->qpcFreq / (long long)(im->sendFps > 0 ? im->sendFps : 60);
    if (now.QuadPart - im->lastSendQpc < minGap)
        return;
    double gapSec = (double)(now.QuadPart - im->lastSendQpc) / (double)im->qpcFreq;
    im->lastSendQpc = now.QuadPart;

    if (!im->sending)
        return;

    if (im->sender.SendTexture(im->rig->SceneTexture()))
    {
        im->framesSent++;
        if (gapSec > 0.0)
        {
            double fps = 1.0 / gapSec;
            im->sendFpsEwma = im->sendFpsEwma == 0.0 ? fps
                : im->sendFpsEwma * 0.9 + fps * 0.1;
        }
    }
}

void Canvas::SetSending(bool sending)
{
    m_impl->sending = sending;
}

void Canvas::SendEmptyFrame()
{
    Impl* im = m_impl.get();
    if (!im->rig || !im->rig->SceneTexture())
        return;
    im->rig->ClearScene();
    if (im->sender.SendTexture(im->rig->SceneTexture()))
        im->framesSent++;
}

bool Canvas::Sending() const
{
    return m_impl->sending;
}

double Canvas::SendFps() const
{
    return m_impl->sendFpsEwma;
}

unsigned long long Canvas::FramesSent() const
{
    return m_impl->framesSent;
}

