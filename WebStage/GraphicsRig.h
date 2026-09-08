#pragma once

// ============================================================================
// WebStage - Graphics Rig
// One shared D3D11 device/context for everything (WGC frame pool, DComp,
// scene processing, preview swapchain, SpoutDX sender).
// Owns the persistent scene texture and the fullscreen-quad shader passes:
// raw copy, premultiplied->straight alpha, checkerboard, scene-over-checker.
// ============================================================================

#include <Windows.h>
#include <d3d11.h>
#include <wil/com.h>

class GraphicsRig
{
public:
    bool Initialize();
    void Shutdown();

    ID3D11Device* Device() const { return m_device.get(); }
    ID3D11DeviceContext* Context() const { return m_context.get(); }

    // Scene texture (persistent, BGRA8, RTV+SRV)
    bool CreateSceneTexture(int width, int height);
    ID3D11ShaderResourceView* SceneSrv() const { return m_sceneSrv.get(); }
    ID3D11Texture2D* SceneTexture() const { return m_scene.get(); }
    int SceneWidth() const { return m_sceneW; }
    int SceneHeight() const { return m_sceneH; }

    void ClearScene();

    // Scene compositor: clear, then draw source quads bottom-to-top
    // (straight alpha, per-source opacity, blended by state).
    void BeginComposite();
    // srcSrvOrNull: cached view for the texture (see WebSource::LatestSrv);
    // null falls back to a throwaway view.
    void DrawSourceQuad(ID3D11Texture2D* src,
        ID3D11ShaderResourceView* srcSrvOrNull,
        int dstX, int dstY, int dstW, int dstH, float opacity);
    void EndComposite();

    // Debug: save the current scene texture to a PNG file (WIC)
    bool SaveScenePng(const wchar_t* path);

    // Preview swapchain (GDI-compatible, so selection overlays can be drawn
    // with GDI on top of the D3D scene) + draw
    bool HasPreviewSwapchain() const { return (bool)m_swapchain; }
    // Destroy + recreate at the window's current client size (recovery
    // path when the GDI surface stops handing out DCs after a resize).
    bool RecreatePreviewSwapchain(HWND hwnd);
    // DXGI occlusion latch: set when Present reports OCCLUDED (DWM drops
    // the frame), cleared on the first S_OK. Static scenes would otherwise
    // stay frozen on their last composed frame until some repaint happens
    // to clear it, so the app retries while latched (see TIMER_STATUS).
    bool IsOccluded() const { return m_occluded; }
    bool CreatePreviewSwapchain(HWND hwnd, int width, int height);
    void DestroyPreviewSwapchain();
    void ResizePreview(int width, int height);
    // Draw the scene letterboxed into (dstX,dstY,dstW,dstH) of the backbuffer
    // (bars cleared to the app background first), no present.
    void DrawPreviewScene(int dstX, int dstY, int dstW, int dstH);
    void PresentPreview();          // Present, no vsync wait

    // GDI-free overlay path: straight-alpha BGRA bits (magenta = transparent
    // key, resolved on upload) drawn full-client over the preview image.
    // Immune to the DXGI GetDC flakiness that killed the old overlay.
    bool UploadOverlay(const void* bgra, int width, int height, int strideBytes);
    void DrawOverlayQuad(int clientW, int clientH);
    // Editor tint wash: flat translucent rect in client pixels over the
    // preview image (under the outline overlay). Masked by scene alpha
    // (mode 6): the hue shows only on transparent scene pixels, i.e. as
    // the background of the square, never over page content. sceneSrv is
    // the scene texture; the quad UVs are mapped into scene space from the
    // letterbox origin (ox,oy) and size (dw,dh) - clip the rect to the
    // letterbox first. Alpha-blended in D3D so it composites correctly.
    void DrawTintQuad(int x, int y, int w, int h, int clientW, int clientH,
        COLORREF color, float alpha, ID3D11ShaderResourceView* sceneSrv,
        int ox, int oy, int dw, int dh);

private:
    bool CompileShaders();

    wil::com_ptr<ID3D11Device> m_device;
    wil::com_ptr<ID3D11DeviceContext> m_context;

    wil::com_ptr<ID3D11VertexShader> m_vs;
    wil::com_ptr<ID3D11VertexShader> m_vsQuad;
    wil::com_ptr<ID3D11InputLayout> m_quadLayout;
    wil::com_ptr<ID3D11PixelShader> m_ps;
    wil::com_ptr<ID3D11Buffer> m_psConstants;
    wil::com_ptr<ID3D11SamplerState> m_samplerPoint;
    wil::com_ptr<ID3D11SamplerState> m_samplerLinear;

    wil::com_ptr<ID3D11Texture2D> m_scene;
    wil::com_ptr<ID3D11RenderTargetView> m_sceneRtv;
    wil::com_ptr<ID3D11ShaderResourceView> m_sceneSrv;
    int m_sceneW = 0;
    int m_sceneH = 0;

    wil::com_ptr<ID3D11Buffer> m_quadVb;
    wil::com_ptr<ID3D11BlendState> m_blendAlpha;
    wil::com_ptr<ID3D11RasterizerState> m_rsNoCull;

    wil::com_ptr<IDXGISwapChain> m_swapchain;
    wil::com_ptr<ID3D11RenderTargetView> m_previewRtv;
    bool m_occluded = false;

    wil::com_ptr<ID3D11Texture2D> m_overlayTex;
    wil::com_ptr<ID3D11ShaderResourceView> m_overlaySrv;
    int m_overlayW = 0;
    int m_overlayH = 0;
    HWND m_previewHwnd = nullptr;
};

