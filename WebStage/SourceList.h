#pragma once

// ============================================================================
// WebStage - Source List
// Owner-drawn dark list of sources (name, geometry, hotkey). Click selects,
// click on the eye toggles visibility, click on the padlock toggles the
// canvas lock. Up/Down/Space/Delete/L keyboard support. Vertical scrollbar
// appears when rows overflow; the list follows the selection.
// ============================================================================

#include <Windows.h>

class App; // forward (implemented in App.h)

class SourceList
{
public:
    bool Create(HWND parent, App* app);
    HWND Window() const { return m_hwnd; }
    void Refresh();
    void SetPosition(int x, int y, int w, int h);

private:
    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnPaint();
    int RowHeight() const;
    int RowsTop() const;    // header band height (SOURCES + count)
    int RowsHeight() const; // rows band height (client minus header/footer)
    int FooterHeight() const;
    int RowAt(int clientY) const;
    int EyeX() const;  // visibility glyph center x (left edge)
    int LockX() const; // padlock glyph center x (right of the eye)
    int MuteX() const; // mute badge center x (right of the padlock)
    bool EyeHit(int index, int clientX, int clientY) const;
    bool LockHit(int index, int clientX, int clientY) const;
    void DrawEye(HDC dc, int x, int y, bool visible) const;
    void DrawLock(HDC dc, int x, int y, bool locked) const;
    void DrawMutedIcon(HDC dc, int x, int y) const; // red speaker+slash
    void UpdateScroll();              // scrollbar range/pos + clamp offset
    int MaxScroll() const;            // max pixel offset for current content
    void EnsureVisible(int index);    // scroll just enough to show the row

    HWND m_hwnd = nullptr;
    App* m_app = nullptr;
    int m_scroll = 0;    // vertical pixel offset
    int m_lastSel = -1;  // selection last scrolled into view

    // Row drag-reorder state
    bool m_rowArmed = false; // button down on a plain row, may start a drag
    bool m_rowDrag = false;  // reorder drag in progress (owns capture)
    int m_dragRow = -1;      // source index being moved
    int m_dropTarget = -1;   // insertion index (gap above this row, or the
                             // gap below the last row when m_dropBelow)
    bool m_dropBelow = false;
    POINT m_downPt = {};     // button-down position (drag threshold)

    // Custom dark scrollbar state (native bar is off: WS_VSCROLL removed)
    bool m_thumbDrag = false;
    int m_thumbGrab = 0; // cursor offset inside the thumb when grabbed
    bool m_barHover = false;
    bool m_trackMouse = false;
    bool HasOverflow() const; // content taller than the client area
    int RightPad() const;     // right text margin (room for the scrollbar)
    RECT BarRect() const;     // full-height scrollbar strip
    RECT ThumbRect() const;   // draggable thumb rect (empty when no overflow)
};

