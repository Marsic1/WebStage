// ============================================================================
// WebStage - Audio Mixer Implementation
// ============================================================================

#include "AudioMixer.h"
#include "Ui.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wil/com.h>

#include <algorithm>
#include <cmath>

struct AudioMixer::Wasapi
{
    wil::com_ptr<IMMDevice> device;
    wil::com_ptr<IAudioClient> client;
    wil::com_ptr<IAudioRenderClient> render;
    HANDLE event = nullptr;
    UINT32 bufferFrames = 0;
};

AudioMixer::AudioMixer() = default;

AudioMixer::~AudioMixer()
{
    Shutdown();
}

bool AudioMixer::Initialize()
{
    if (m_enabled)
        return true;

    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_stopEvent)
        return false;

    m_wasapi = new (std::nothrow) Wasapi();
    if (!m_wasapi)
        return false;

    // Open the default render device on the mixer thread (COM MTA there).
    // Here: just create the thread; it initializes WASAPI itself so all
    // audio COM objects live on that thread.
    m_thread = CreateThread(nullptr, 0, MixThread, this, 0, nullptr);
    if (!m_thread)
    {
        delete m_wasapi;
        m_wasapi = nullptr;
        return false;
    }

    // Wait for the thread to report readiness (or failure)
    for (int i = 0; i < 100; i++)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_enabled || m_wasapi->bufferFrames == UINT32(-1))
                break;
        }
        Sleep(20);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_enabled)
    {
        Ui::Log(L"AudioMixer: no usable audio device, mixer disabled");
        return false;
    }
    Ui::Log(L"AudioMixer: initialized at %d Hz", m_mixRate);
    return true;
}

void AudioMixer::Shutdown()
{
    if (m_stopEvent)
        SetEvent(m_stopEvent);
    if (m_thread)
    {
        WaitForSingleObject(m_thread, 3000);
        CloseHandle(m_thread);
        m_thread = nullptr;
    }
    if (m_stopEvent)
    {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_streams.clear();
        m_enabled = false;
    }
    delete m_wasapi;
    m_wasapi = nullptr;
}

DWORD WINAPI AudioMixer::MixThread(void* param)
{
    auto self = static_cast<AudioMixer*>(param);
    self->MixLoop();
    return 0;
}

void AudioMixer::MixLoop()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    Wasapi* w = m_wasapi;
    bool ready = false;
    int rate = 48000;

    wil::com_ptr<IMMDeviceEnumerator> enumerator;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) && enumerator)
    {
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &w->device)) && w->device)
        {
            if (SUCCEEDED(w->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                nullptr, (void**)&w->client)) && w->client)
            {
                // Prefer 48 kHz float stereo, fall back to 44.1 kHz
                for (int tryRate : { 48000, 44100 })
                {
                    WAVEFORMATEXTENSIBLE fmt = {};
                    fmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
                    fmt.Format.nChannels = 2;
                    fmt.Format.nSamplesPerSec = (DWORD)tryRate;
                    fmt.Format.wBitsPerSample = 32;
                    fmt.Format.nBlockAlign = 8;
                    fmt.Format.nAvgBytesPerSec = (DWORD)tryRate * 8;
                    fmt.Format.cbSize = 22;
                    fmt.Samples.wValidBitsPerSample = 32;
                    fmt.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
                    fmt.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

                    WAVEFORMATEX* closest = nullptr;
                    HRESULT hr = w->client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                        (WAVEFORMATEX*)&fmt, &closest);
                    if (closest)
                        CoTaskMemFree(closest);
                    if (SUCCEEDED(hr))
                    {
                        REFERENCE_TIME bufDur = 100 * 10000; // 100 ms
                        hr = w->client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_EVENTCALLBACK, bufDur, 0,
                            (WAVEFORMATEX*)&fmt, nullptr);
                        if (SUCCEEDED(hr) &&
                            SUCCEEDED(w->client->GetService(IID_PPV_ARGS(&w->render))) &&
                            w->render &&
                            SUCCEEDED(w->client->GetBufferSize(&w->bufferFrames)) &&
                            w->bufferFrames > 0)
                        {
                            rate = tryRate;
                            ready = true;
                        }
                        break;
                    }
                }
            }
        }
    }

    if (ready)
    {
        w->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (w->event)
        {
            w->client->SetEventHandle(w->event);
            w->client->Start();
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_mixRate = rate;
                m_enabled = true;
            }
            Ui::Log(L"AudioMixer: render started");

            // Event-driven render: wake whenever WASAPI needs data, not on
            // a fixed sleep (sleeping e.g. 2 s between renders produces
            // ~100 ms audio bursts separated by silence = broken audio).
            HANDLE waitEvents[2] = { m_stopEvent, w->event };
            std::vector<float> out;
            for (;;)
            {
                DWORD r = WaitForMultipleObjects(2, waitEvents, FALSE, INFINITE);
                if (r != WAIT_OBJECT_0 + 1)
                    break; // stop requested or wait failed
                UINT32 padding = 0;
                if (FAILED(w->client->GetCurrentPadding(&padding)))
                    break;
                UINT32 avail = w->bufferFrames > padding ? w->bufferFrames - padding : 0;
                if (avail == 0)
                    continue;
                BYTE* data = nullptr;
                if (FAILED(w->render->GetBuffer(avail, &data)))
                    break;
                out.assign((size_t)avail * 2, 0.0f);
                MixBlock(out.data(), (int)avail);
                memcpy(data, out.data(), (size_t)avail * 2 * sizeof(float));
                w->render->ReleaseBuffer(avail, 0);
            }

            w->client->Stop();
            CloseHandle(w->event);
            w->event = nullptr;
        }
    }
    else
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        w->bufferFrames = UINT32(-1); // signal failure to Initialize()
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_enabled = false;
    }
    CoUninitialize();
}

void AudioMixer::MixBlock(float* out, int frames)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& kv : m_streams)
    {
        Stream& s = kv.second;

        // Smooth gain changes to avoid clicks
        float step = (s.targetGain - s.curGain) / (frames > 0 ? (float)frames : 1.0f);

        double ratio = (double)s.srcRate / (double)(m_mixRate > 0 ? m_mixRate : 48000);
        size_t have = s.fifo.size() / 2; // input frames available

        // Starved stream (under ~2 frames): nothing to hold, stay silent
        // and keep the read cursor parked instead of running ahead.
        if (have < 2)
        {
            s.curGain = s.targetGain;
            s.readPos = 0.0;
            continue;
        }

        // Clock-drift servo: CEF's audio clock and WASAPI's clock never
        // agree exactly, so the fifo would slowly drain (chronic underflow
        // crackle) or grow (periodic hard-cut clicks at the overflow cap).
        // Nudge consumption +-1% around a 250 ms target: inaudible, and it
        // keeps latency bounded without ever cutting.
        {
            double target = (double)s.srcRate * 0.25;
            double err = target > 0.0 ? ((double)have - target) / target : 0.0;
            double trim = err * 0.005;
            if (trim < -0.01) trim = -0.01;
            else if (trim > 0.02) trim = 0.02;
            ratio *= (1.0 + trim);
        }

        // Never consume past the data on hand: clamp the read cursor to
        // the last interpolatable frame. Starved output then repeats the
        // most recent sample (sample-hold) instead of inserting zeros,
        // which is what makes dropouts crackle.
        double maxPos = (double)(have - 2);
        if (s.readPos > maxPos)
            s.readPos = maxPos;

        for (int f = 0; f < frames; f++)
        {
            s.curGain += step;
            if (s.curGain == 0.0f)
                continue; // muted: advance below, mix nothing
            double pos = s.readPos + f * ratio;
            if (pos > maxPos)
                pos = maxPos;
            size_t i0 = (size_t)pos;
            double frac = pos - (double)i0;
            size_t b0 = i0 * 2;
            float l = (float)(s.fifo[b0] * (1.0 - frac) + s.fifo[b0 + 2] * frac);
            float r = (float)(s.fifo[b0 + 1] * (1.0 - frac) + s.fifo[b0 + 3] * frac);
            out[f * 2] += l * s.curGain;
            out[f * 2 + 1] += r * s.curGain;
        }
        s.curGain = s.targetGain;
        s.readPos += frames * ratio;
        if (s.readPos > maxPos)
            s.readPos = maxPos;

        // Drop consumed input (keep < 250 ms tail to bound memory)
        size_t drop = (size_t)s.readPos;
        size_t maxDrop = have > 2 ? have - 2 : 0;
        if (drop > maxDrop)
            drop = maxDrop;
        if (drop > 0)
        {
            s.fifo.erase(s.fifo.begin(), s.fifo.begin() + (std::deque<float>::difference_type)(drop * 2));
            s.readPos -= (double)drop;
        }
        // Overflow guard: drop oldest beyond ~2 s
        size_t cap = (size_t)s.srcRate * 2 * 2;
        if (s.fifo.size() > cap)
        {
            size_t cut = s.fifo.size() - cap;
            s.fifo.erase(s.fifo.begin(), s.fifo.begin() + (std::deque<float>::difference_type)cut);
            if (s.readPos > (double)cut / 2.0)
                s.readPos -= (double)cut / 2.0;
            else
                s.readPos = 0.0;
        }
    }

    // Soft-ish clip to [-1, 1] with a master trim
    for (int i = 0; i < frames * 2; i++)
    {
        float v = out[i] * 0.9f;
        if (v > 1.0f) v = 1.0f;
        else if (v < -1.0f) v = -1.0f;
        out[i] = v;
    }
}

bool AudioMixer::AddStream(int key, int srcRate)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_enabled)
        return false;
    Stream s;
    s.id = key;
    s.srcRate = srcRate > 0 ? srcRate : 48000;
    m_streams[key] = std::move(s);
    return true;
}

void AudioMixer::RemoveStream(int key)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_streams.erase(key);
}

void AudioMixer::Push(int key, const float* stereo, int frames)
{
    if (!stereo || frames <= 0)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_streams.find(key);
    if (it == m_streams.end())
        return;
    it->second.fifo.insert(it->second.fifo.end(), stereo, stereo + (size_t)frames * 2);
}

void AudioMixer::SetSource(int key, float gain01, bool muted)
{
    if (gain01 < 0.0f) gain01 = 0.0f;
    if (gain01 > 1.0f) gain01 = 1.0f;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_streams.find(key);
    if (it == m_streams.end())
        return;
    it->second.targetGain = muted ? 0.0f : gain01;
}

