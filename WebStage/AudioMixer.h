#pragma once

// ============================================================================
// WebStage - Audio Mixer
// One WASAPI shared-mode float32 stereo output (48 kHz, 44.1 kHz fallback).
// CEF audio packets (any rate, planar->stereo converted by the caller) are
// queued per stream, linearly resampled, gained (per-source volume/mute)
// and mixed on a dedicated thread. Silent when idle.
// ============================================================================

#include <Windows.h>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <vector>

class AudioMixer
{
public:
    AudioMixer();
    ~AudioMixer();

    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;

    bool Initialize(); // once; false = no audio device (mixer disabled)
    void Shutdown();
    bool Enabled() const { return m_enabled; }

    // Audio stream management (called from CEF audio thread)
    bool AddStream(int key, int srcRate); // one stream per key, false if disabled
    void RemoveStream(int key);
    // Interleaved stereo float at the stream's rate
    void Push(int key, const float* stereo, int frames);
    // UI thread: volume 0..1, muted flag
    void SetSource(int key, float gain01, bool muted);

private:
    struct Stream
    {
        int id = -1;
        int srcRate = 48000;
        std::deque<float> fifo; // interleaved stereo @ srcRate
        double readPos = 0.0;   // consumed input frames (fractional)
        float curGain = 1.0f;
        float targetGain = 1.0f;
    };

    static DWORD WINAPI MixThread(void* param);
    void MixLoop();
    void MixBlock(float* out, int frames);

    std::mutex m_mutex;
    std::map<int, Stream> m_streams; // keyed by source index

    // WASAPI (mixer thread owns these)
    struct Wasapi;
    Wasapi* m_wasapi = nullptr;

    HANDLE m_thread = nullptr;
    HANDLE m_stopEvent = nullptr;
    bool m_enabled = false;
    int m_mixRate = 48000;
};

