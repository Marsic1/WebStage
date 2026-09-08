#pragma once

// ============================================================================
// WebStage - Preview
// Live scene preview: draws the scene texture (D3D) then a GDI overlay with
// source outlines, selection + resize handles. Drag sources to move, drag
// handles to resize. Double-buffered scene<->client mapping with letterbox.
// ============================================================================

#include <Windows.h>

#include <cstdint>

class App; // forward (implemented in App.h)

class Preview
{
public:
    bool Create(HWND parent, App* app);
    HWND Window() const { return m_hwnd; }
    void Refresh();
    void SetPosition(int x, int y, int w, int h);

private:
    enum class DragMode
    {
        None, Move, MoveHandle,
        L, T, R, B, TL, TR, BL, BR
    };

    enum CtxCmd : int
    {
        CtxFill = 1, CtxCenterH, CtxCenterV,
    };

    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnPaint();
    void UpdateMapping();
    POINT SceneToClient(int sx, int sy) const;
    POINT ClientToScene(int cx, int cy) const;
    DragMode HitTest(int cx, int cy, int& sourceIndex) const;
    RECT HandleRect(DragMode h, const RECT& r) const;
    RECT MoveHandleRect(const RECT& r) const;
    void OnLButtonDown(int cx, int cy);
    void OnMouseMove(int cx, int cy);
    void OnLButtonUp();
    void OnRButtonUp(int cx, int cy);

    HWND m_hwnd = nullptr;
    App* m_app = nullptr;

    // Letterbox mapping
    float m_scale = 1.0f;
    int m_ox = 0;
    int m_oy = 0;

    // Drag state
    DragMode m_drag = DragMode::None;
    int m_dragIndex = -1;
    POINT m_grabOffset = {}; // scene coords: cursor - source origin (move)
    RECT m_dragStartRect = {}; // source rect at drag start (scene coords)
    POINT m_dragStartPt = {};  // cursor at drag start (scene coords)

    // Overlay upload cache: the GDI overlay (outlines/tags/handles) only
    // changes when the scene layout does. Repainting the full-client DIB
    // and re-uploading it every frame costs a FillRect + per-pixel key pass
    // + GPU upload for zero visual change, so skip both when the hash of
    // all overlay inputs matches. The D3D wash quads + overlay composite
    // still run every paint (cheap, and the scene underneath animates).
    uint64_t m_overlayHash = 0;
    bool m_overlayValid = false;
};

