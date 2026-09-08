#pragma once

// ============================================================================
// WebStage - Canvas (scene compositor)
// No window of its own: each source owns a host window captured via WGC.
// This class composites the latest frame of every visible source into the
// scene texture (straight alpha, no chroma key) and sends it via SpoutDX,
// honoring the global send-rate cap. Frame counting stays on (helps the
// receiver's smooth mode).
// ============================================================================

#include <Windows.h>
#include <d3d11.h>
#include <memory>
#include <string>
#include <vector>

#include "Scene.h"

class GraphicsRig;
class WebSource;

class Canvas
{
public:
    explicit Canvas(GraphicsRig* rig);
    ~Canvas();

    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    bool Initialize(const std::string& senderName, int sendFps);
    void Shutdown();

    // Composite all visible sources + send. Always redraws the scene from
    // the latest source textures; the Spout send itself is rate-gated.
    // (Gating must never skip the redraw: for static sources a skipped
    // redraw loses content permanently.)
    void Composite(const SceneFile& model,
        const std::vector<std::unique_ptr<WebSource>>& sources);

    // Runtime settings
    void ApplySettings(const std::string& senderName, int sendFps);
    void SetSending(bool sending);
    bool Sending() const;

    // Clear the scene and push one final empty frame, bypassing the
    // sending flag (so switching the sender off wipes the receiver).
    void SendEmptyFrame();

    // Stats for the status bar
    double SendFps() const;
    unsigned long long FramesSent() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

