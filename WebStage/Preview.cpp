// ============================================================================
// WebStage - Preview Implementation
// ============================================================================

#include "Preview.h"
#include "App.h"
#include "Ui.h"

#include <windowsx.h>

#pragma comment(lib, "msimg32.lib")
#include "GraphicsRig.h"

#include <windowsx.h>

// Per-source hues (mockup palette); the selection keeps the signal accent.
// Shared by the D3D tint wash and the outline overlay so fill and border
// always match.
static const COLORREF kSrcTints[8] = {
    RGB(0x9F, 0xC4, 0xFF), // blue
    RGB(0xFF, 0xCE, 0x85), // amber
    RGB(0x7E, 0xE0, 0xC3), // teal
    RGB(0xF0, 0x96, 0xAA), // rose
    RGB(0xE3, 0xD1, 0x7C), // gold
    RGB(0xB7, 0x9C, 0xF2), // violet
    RGB(0x8F, 0xDE, 0x6E), // lime
    RGB(0xF2, 0x8B, 0x6B), // coral
};

static COLORREF TintFor(int i, int sel)
{
    return (i == sel) ? Ui::Accent() : kSrcTints[i & 7];
}

// FNV-1a over everything the GDI overlay draws: repaint + re-upload the
// full-client DIB only when this changes.
static uint64_t OverlayHash(App* app, int sel, int pw, int ph)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
    mix((uint64_t)(uint32_t)pw);
    mix((uint64_t)(uint32_t)ph);
    mix((uint64_t)(int64_t)sel);
    int n = app->SourceCount();
    mix((uint64_t)(uint32_t)n);
    for (int i = 0; i < n; i++)
    {
        const SourceSettings& s = app->SourceAt(i);
        mix((uint64_t)(uint32_t)s.x);
        mix((uint64_t)(uint32_t)s.y);
        mix((uint64_t)(uint32_t)s.width);
        mix((uint64_t)(uint32_t)s.height);
        mix(s.visible ? 1ull : 0ull);
        mix(s.locked ? 1ull : 0ull);
        for (wchar_t c : s.name)
            mix((uint64_t)c);
    }
    return h;
}

bool Preview::Create(HWND parent, App* app)
{    static bool registered = false;
    if (!registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = Preview::Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"WS_Preview";
        if (!RegisterClassW(&wc))
            return false;
        registered = true;
    }

    m_app = app;
    m_hwnd = CreateWindowExW(0, L"WS_Preview", L"",
        WS_CHILD | WS_VISIBLE,
        0, 0, 100, 100, parent, nullptr, GetModuleHandleW(nullptr), this);
    return m_hwnd != nullptr;
}

void Preview::Refresh()
{
    if (m_hwnd)
        InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Preview::SetPosition(int x, int y, int w, int h)
{
    if (m_hwnd)
        SetWindowPos(m_hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void Preview::UpdateMapping()
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int cw = rc.right - rc.left;
    int ch = rc.bottom - rc.top;
    int sw = m_app->SceneWidth();
    int sh = m_app->SceneHeight();
    if (cw <= 0 || ch <= 0 || sw <= 0 || sh <= 0)
    {
        m_scale = 1.0f;
        m_ox = m_oy = 0;
        return;
    }
    m_scale = (std::min)((float)cw / (float)sw, (float)ch / (float)sh);
    m_ox = (int)((cw - sw * m_scale) / 2.0f);
    m_oy = (int)((ch - sh * m_scale) / 2.0f);
}

POINT Preview::SceneToClient(int sx, int sy) const
{
    POINT p;
    p.x = m_ox + (int)(sx * m_scale);
    p.y = m_oy + (int)(sy * m_scale);
    return p;
}

POINT Preview::ClientToScene(int cx, int cy) const
{
    POINT p;
    // Round (don't truncate) so drag/snap math is stable at small scales
    p.x = (int)floor((cx - m_ox) / m_scale + 0.5);
    p.y = (int)floor((cy - m_oy) / m_scale + 0.5);
    return p;
}

RECT Preview::HandleRect(DragMode h, const RECT& r) const
{
    int hs = Ui::Scale(m_hwnd, 9);
    int hx = 0, hy = 0;
    switch (h)
    {
    case DragMode::TL: hx = r.left; hy = r.top; break;
    case DragMode::TR: hx = r.right; hy = r.top; break;
    case DragMode::BL: hx = r.left; hy = r.bottom; break;
    case DragMode::BR: hx = r.right; hy = r.bottom; break;
    case DragMode::T: hx = (r.left + r.right) / 2; hy = r.top; break;
    case DragMode::B: hx = (r.left + r.right) / 2; hy = r.bottom; break;
    case DragMode::L: hx = r.left; hy = (r.top + r.bottom) / 2; break;
    case DragMode::R: hx = r.right; hy = (r.top + r.bottom) / 2; break;
    default: break;
    }
    return { hx - hs / 2, hy - hs / 2, hx + hs / 2, hy + hs / 2 };
}

RECT Preview::MoveHandleRect(const RECT& r) const
{
    // Circle handle above the top-center edge for dragging (move)
    int d = Ui::Scale(m_hwnd, 16);
    int cx = (r.left + r.right) / 2;
    int cy = r.top - Ui::Scale(m_hwnd, 18);
    return { cx - d / 2, cy - d / 2, cx + d / 2, cy + d / 2 };
}

Preview::DragMode Preview::HitTest(int cx, int cy, int& sourceIndex) const
{
    sourceIndex = -1;
    int sel = m_app->Selection();

    // Handles of the selected source first (resize + move handle),
    // but only when the selected source is visible AND unlocked.
    if (sel >= 0 && sel < m_app->SourceCount() && m_app->SourceAt(sel).visible
        && !m_app->SourceAt(sel).locked)
    {
        const SourceSettings& s = m_app->SourceAt(sel);
        POINT a = SceneToClient(s.x, s.y);
        POINT b = SceneToClient(s.x + s.width, s.y + s.height);
        RECT r = { a.x, a.y, b.x, b.y };

        RECT mh = MoveHandleRect(r);
        mh.left -= 3; mh.top -= 3; mh.right += 3; mh.bottom += 3;
        if (cx >= mh.left && cx < mh.right && cy >= mh.top && cy < mh.bottom)
        {
            sourceIndex = sel;
            return DragMode::MoveHandle;
        }

        static const DragMode order[] =
        { DragMode::TL, DragMode::TR, DragMode::BL, DragMode::BR,
          DragMode::T, DragMode::B, DragMode::L, DragMode::R };
        for (DragMode h : order)
        {
            RECT hr = HandleRect(h, r);
            hr.left -= 2; hr.top -= 2; hr.right += 2; hr.bottom += 2;
            if (cx >= hr.left && cx < hr.right && cy >= hr.top && cy < hr.bottom)
            {
                sourceIndex = sel;
                return h;
            }
        }
    }

    // Sources topmost-first (first in list = on top, OBS order); hidden
    // sources are neither drawn nor clickable, locked sources are
    // clickthrough (so a source below a locked one can still be grabbed).
    for (int i = 0; i < m_app->SourceCount(); i++)
    {
        const SourceSettings& s = m_app->SourceAt(i);
        if (!s.visible || s.locked)
            continue;
        POINT a = SceneToClient(s.x, s.y);
        POINT b = SceneToClient(s.x + s.width, s.y + s.height);
        if (cx >= a.x && cx < b.x && cy >= a.y && cy < b.y)
        {
            sourceIndex = i;
            return DragMode::Move;
        }
    }
    return DragMode::None;
}

void Preview::OnLButtonDown(int cx, int cy)
{
    int index = -1;
    DragMode mode = HitTest(cx, cy, index);
    if (mode == DragMode::None)
    {
        m_app->Select(-1);
        return;
    }
    m_app->Select(index);
    m_drag = mode;
    m_dragIndex = index;
    SetCapture(m_hwnd);

    const SourceSettings& s = m_app->SourceAt(index);
    m_dragStartRect = { s.x, s.y, s.x + s.width, s.y + s.height };
    POINT sc = ClientToScene(cx, cy);
    m_dragStartPt = sc;
    m_grabOffset = { sc.x - s.x, sc.y - s.y };
}

void Preview::OnMouseMove(int cx, int cy)
{
    if (m_drag == DragMode::None || m_dragIndex < 0)
        return;
    // A source locked mid-drag stops immediately (no clickable way to
    // start a drag on a locked source; this covers the L hotkey case).
    if (m_dragIndex < m_app->SourceCount() && m_app->SourceAt(m_dragIndex).locked)
    {
        m_drag = DragMode::None;
        m_dragIndex = -1;
        ReleaseCapture();
        return;
    }

    POINT sc = ClientToScene(cx, cy);
    RECT r = m_dragStartRect;

    int sw = m_app->SceneWidth();
    int sh = m_app->SceneHeight();

    // Snap tolerance in screen pixels: a fixed scene-space threshold
    // becomes sub-pixel when zoomed out (e.g. 8 scene px = ~1.6 client
    // px at 1920->400px preview), making snaps nearly impossible to hit.
    int tol = m_scale > 0.0f ? (int)(10.0f / m_scale + 0.5f) : 8;
    if (tol < 8)
        tol = 8;

    auto snapV = [&](int v) {
        const int lines[3] = { 0, sw / 2, sw };
        for (int L : lines)
            if (abs(v - L) <= tol) return L;
        // Snap to other sources' vertical edges/centers
        for (int i = 0; i < m_app->SourceCount(); i++)
        {
            if (i == m_dragIndex)
                continue;
            const SourceSettings& o = m_app->SourceAt(i);
            if (!o.visible)
                continue; // hidden sources are not snap targets
            const int lines2[3] = { o.x, o.x + o.width / 2, o.x + o.width };
            for (int L : lines2)
                if (abs(v - L) <= tol) return L;
        }
        return v;
    };
    auto snapH = [&](int v) {
        const int lines[3] = { 0, sh / 2, sh };
        for (int L : lines)
            if (abs(v - L) <= tol) return L;
        for (int i = 0; i < m_app->SourceCount(); i++)
        {
            if (i == m_dragIndex)
                continue;
            const SourceSettings& o = m_app->SourceAt(i);
            if (!o.visible)
                continue; // hidden sources are not snap targets
            const int lines2[3] = { o.y, o.y + o.height / 2, o.y + o.height };
            for (int L : lines2)
                if (abs(v - L) <= tol) return L;
        }
        return v;
    };

    if (m_drag == DragMode::Move || m_drag == DragMode::MoveHandle)
    {
        int w = r.right - r.left;
        int h = r.bottom - r.top;
        int nx = sc.x - m_grabOffset.x;
        int ny = sc.y - m_grabOffset.y;

        // Snap: left edge, right edge, horizontal center (first match wins)
        int snapped = snapV(nx);
        if (snapped == nx) snapped = snapV(nx + w) - w;
        if (snapped == nx) snapped = snapV(nx + w / 2) - w / 2;
        nx = snapped;
        snapped = snapH(ny);
        if (snapped == ny) snapped = snapH(ny + h) - h;
        if (snapped == ny) snapped = snapH(ny + h / 2) - h / 2;
        ny = snapped;

        // Keep at least 16px of the source inside the scene
        nx = (std::max)(-w + 16, (std::min)(nx, sw - 16));
        ny = (std::max)(-h + 16, (std::min)(ny, sh - 16));
        r = { nx, ny, nx + w, ny + h };
    }
    else
    {
        bool driveL = (m_drag == DragMode::L || m_drag == DragMode::TL || m_drag == DragMode::BL);
        bool driveR = (m_drag == DragMode::R || m_drag == DragMode::TR || m_drag == DragMode::BR);
        bool driveT = (m_drag == DragMode::T || m_drag == DragMode::TL || m_drag == DragMode::TR);
        bool driveB = (m_drag == DragMode::B || m_drag == DragMode::BL || m_drag == DragMode::BR);

        if (driveL) r.left = (std::min)(snapV((int)sc.x), (int)r.right - 16);
        if (driveR) r.right = (std::max)(snapV((int)sc.x), (int)r.left + 16);
        if (driveT) r.top = (std::min)(snapH((int)sc.y), (int)r.bottom - 16);
        if (driveB) r.bottom = (std::max)(snapH((int)sc.y), (int)r.top + 16);

        // Shift: preserve the drag-start aspect ratio
        if (GetKeyState(VK_SHIFT) & 0x8000)
        {
            int startW = m_dragStartRect.right - m_dragStartRect.left;
            int startH = m_dragStartRect.bottom - m_dragStartRect.top;
            if (startW > 0 && startH > 0)
            {
                double aspect = (double)startW / (double)startH;
                bool corner = (driveL || driveR) && (driveT || driveB);
                int w = r.right - r.left;
                int h = r.bottom - r.top;
                if (corner)
                {
                    // Dominant axis wins
                    double dw = (double)w / startW;
                    double dh = (double)h / startH;
                    if (dw >= dh)
                    {
                        h = (std::max)(16, (int)(w / aspect + 0.5));
                        if (driveT) r.top = r.bottom - h;
                        else r.bottom = r.top + h;
                    }
                    else
                    {
                        w = (std::max)(16, (int)(h * aspect + 0.5));
                        if (driveL) r.left = r.right - w;
                        else r.right = r.left + w;
                    }
                }
                else if (driveL || driveR)
                {
                    h = (std::max)(16, (int)(w / aspect + 0.5));
                    int cy = (m_dragStartRect.top + m_dragStartRect.bottom) / 2;
                    r.top = cy - h / 2;
                    r.bottom = r.top + h;
                }
                else
                {
                    w = (std::max)(16, (int)(h * aspect + 0.5));
                    int ccx = (m_dragStartRect.left + m_dragStartRect.right) / 2;
                    r.left = ccx - w / 2;
                    r.right = r.left + w;
                }
            }
        }
    }

    m_app->DragSourceTo(m_dragIndex, r.left, r.top,
        r.right - r.left, r.bottom - r.top);
}

void Preview::OnLButtonUp()
{
    if (m_drag != DragMode::None)
    {
        m_drag = DragMode::None;
        m_dragIndex = -1;
        ReleaseCapture();
        m_app->EndDrag();
    }
}

void Preview::OnRButtonUp(int cx, int cy)
{
    int index = -1;
    DragMode mode = HitTest(cx, cy, index);
    if (mode == DragMode::None || index < 0)
        return;
    m_app->Select(index);

    HMENU menu = Ui::CreateDarkMenu();
    Ui::AddDarkMenuItem(menu, CtxFill, L"Fill scene");
    Ui::AddDarkMenuItem(menu, CtxCenterH, L"Center horizontally");
    Ui::AddDarkMenuItem(menu, CtxCenterV, L"Center vertically");

    POINT pt = { cx, cy };
    ClientToScreen(m_hwnd, &pt);
    int cmd = Ui::TrackDarkMenu(menu, m_hwnd, pt.x, pt.y);
    DestroyMenu(menu);

    if (cmd == CtxFill)
        m_app->FillSourceToScene(index);
    else if (cmd == CtxCenterH)
        m_app->CenterSource(index, true, false);
    else if (cmd == CtxCenterV)
        m_app->CenterSource(index, false, true);
}

// Label chip inside the source square; falls back to a tag above the rect
// when the square is too small to hold it.
static void DrawLabelChip(HDC hdc, const RECT& bound, const wchar_t* text,
    COLORREF tint, UINT dpi, bool ghost)
{
    int chipH = MulDiv(20, (int)dpi, 96);
    int maxW = (bound.right - bound.left) - MulDiv(10, (int)dpi, 96);
    int chipMax = MulDiv(190, (int)dpi, 96);
    if (maxW > chipMax)
        maxW = chipMax;
    int minW = MulDiv(40, (int)dpi, 96);
    bool fits = chipH <= (bound.bottom - bound.top) - MulDiv(8, (int)dpi, 96)
        && maxW >= minW;
    if (!fits)
    {
        RECT tr = { bound.left + 4, bound.top - MulDiv(18, (int)dpi, 96), bound.right, bound.top - 2 };
        if (tr.top >= 0)
        {
            SetTextColor(hdc, ghost ? Ui::EyeOff() : tint);
            DrawTextW(hdc, text, -1, &tr, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        return;
    }
    RECT chip = { bound.left + MulDiv(5, (int)dpi, 96), bound.top + MulDiv(5, (int)dpi, 96),
        bound.left + MulDiv(5, (int)dpi, 96) + maxW, bound.top + MulDiv(5, (int)dpi, 96) + chipH };
    HBRUSH bg = CreateSolidBrush(RGB(0x14, 0x18, 0x1A));
    FillRect(hdc, &chip, bg);
    DeleteObject(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, ghost ? Ui::EyeOff() : tint);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    int rad = MulDiv(6, (int)dpi, 96);
    RoundRect(hdc, chip.left, chip.top, chip.right, chip.bottom, rad, rad);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
    DeleteObject(pen);
    SetTextColor(hdc, ghost ? Ui::EyeOff() : tint);
    RECT tr = { chip.left + MulDiv(6, (int)dpi, 96), chip.top,
        chip.right - MulDiv(4, (int)dpi, 96), chip.bottom };
    DrawTextW(hdc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void Preview::OnPaint()
{
    PAINTSTRUCT ps;
    BeginPaint(m_hwnd, &ps);

    UpdateMapping();

    if (!m_app->IsPreviewOn())
    {
        // Disabled: dark placeholder, no GPU presents
        HDC dc = ps.hdc;
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(Ui::BgPanel());
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, Ui::Gray());
        HGDIOBJ oldFont = SelectObject(dc, Ui::Font());
        DrawTextW(dc, L"Preview disabled", -1, &rc,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, oldFont);
        EndPaint(m_hwnd, &ps);
        return;
    }

    GraphicsRig* rig = m_app->Rig();

    // Letterboxed D3D draw (matches the GDI overlay mapping exactly)
    int sw = m_app->SceneWidth();
    int sh = m_app->SceneHeight();
    int dw = (int)(sw * m_scale);
    int dh = (int)(sh * m_scale);
    rig->DrawPreviewScene(m_ox, m_oy, dw, dh);

    // Tint wash (mockup): each visible square gets a slightly opaque fill
    // in its own border hue. Done as D3D quads over the scene (NOT painted
    // into the keyed overlay DIB: any blend there resolves to opaque and
    // the wash would turn solid, the old pink-fill bug).
    {
        RECT wrc;
        GetClientRect(m_hwnd, &wrc);
        int cw = wrc.right - wrc.left;
        int chh = wrc.bottom - wrc.top;
        int iw = (int)(m_app->SceneWidth() * m_scale);
        int ih = (int)(m_app->SceneHeight() * m_scale);
        if (cw > 0 && chh > 0 && iw > 0 && ih > 0 && rig->SceneSrv())
        {
            int wsel = m_app->Selection();
            for (int i = m_app->SourceCount() - 1; i >= 0; i--)
            {
                const SourceSettings& ws = m_app->SourceAt(i);
                if (!ws.visible)
                    continue; // hidden ghosts get no wash
                POINT wa = SceneToClient(ws.x, ws.y);
                POINT wb = SceneToClient(ws.x + ws.width, ws.y + ws.height);
                // Clip to the letterboxed image (mode 6 samples scene
                // space; outside has no scene pixels).
                int x0 = wa.x > m_ox ? wa.x : m_ox;
                int y0 = wa.y > m_oy ? wa.y : m_oy;
                int x1 = wb.x < m_ox + iw ? wb.x : m_ox + iw;
                int y1 = wb.y < m_oy + ih ? wb.y : m_oy + ih;
                if (x1 <= x0 || y1 <= y0)
                    continue;
                rig->DrawTintQuad(x0, y0, x1 - x0, y1 - y0, cw, chh,
                    TintFor(i, wsel), (i == wsel) ? 0.16f : 0.10f,
                    rig->SceneSrv(), m_ox, m_oy, iw, ih);
            }
        }
    }

    // Overlay without DXGI GetDC (proven flaky across resizes): paint the
    // usual GDI overlay into a memory DIB, upload as straight alpha
    // (magenta = transparent key), composite in D3D over the scene.
    RECT prc;
    GetClientRect(m_hwnd, &prc);
    int pw = prc.right - prc.left;
    int ph = prc.bottom - prc.top;
    int sel = m_app->Selection();
    // Static overlay fast path (see m_overlayHash): skip the full-client
    // GDI repaint, the per-pixel key pass and the GPU upload when nothing
    // that the overlay draws has changed.
    uint64_t ohash = OverlayHash(m_app, sel, pw, ph);
    bool overlayDirty = !m_overlayValid || ohash != m_overlayHash;
    HDC hdc = nullptr;
    HDC memDC = nullptr;
    HBITMAP dib = nullptr;
    HGDIOBJ oldBmp = nullptr;
    void* dibBits = nullptr;
    int dibStride = 0;
    if (overlayDirty && pw > 0 && ph > 0)
    {
        memDC = CreateCompatibleDC(nullptr);
        if (memDC)
        {
            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
            bi.bmiHeader.biWidth = pw;
            bi.bmiHeader.biHeight = -ph; // top-down: rows in order
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            dib = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
            if (dib && dibBits)
            {
                oldBmp = SelectObject(memDC, dib);
                dibStride = pw * 4;
                HBRUSH key = CreateSolidBrush(RGB(255, 0, 255));
                RECT full = { 0, 0, pw, ph };
                FillRect(memDC, &full, key);
                DeleteObject(key);
                hdc = memDC;
            }
            else
            {
                if (dib)
                    DeleteObject(dib);
                DeleteDC(memDC);
                memDC = nullptr;
            }
        }
    }
    if (hdc)
    {
        HGDIOBJ oldFont = SelectObject(hdc, Ui::Font());
        // NOTE: this DIB starts life as solid magenta (the transparency
        // key). Paint text ONLY over opaque fills (label chips, etc):
        // anti-aliased glyphs blend with the background, so text drawn
        // straight on magenta gets a pink fringe that survives keying.
        SetBkMode(hdc, TRANSPARENT);

        HPEN accentPen = CreatePen(PS_SOLID, 2, Ui::Accent());
        HBRUSH handleBrush = CreateSolidBrush(Ui::Accent());

        // Frame around the letterboxed scene image (scene aspect from
        // settings), so bars vs image are distinguishable while placing.
        {
            HPEN framePen = CreatePen(PS_SOLID, 1, Ui::Sep());
            HGDIOBJ oldPen = SelectObject(hdc, framePen);
            HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            int iw = (int)(m_app->SceneWidth() * m_scale);
            int ih = (int)(m_app->SceneHeight() * m_scale);
            Rectangle(hdc, m_ox, m_oy, m_ox + iw, m_oy + ih);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBr);
            DeleteObject(framePen);
        }

        // Source squares back-to-front (first in list = on top, OBS order).
        // Each source gets its own hue (see TintFor, shared with the D3D
        // wash above); the selection keeps the signal accent + handles.
        // Hidden sources draw as ghosts so their slot stays visible while
        // wiring up the scene.
        UINT dpi = Ui::WindowDpi(m_hwnd);
        for (int i = m_app->SourceCount() - 1; i >= 0; i--)
        {
            const SourceSettings& s = m_app->SourceAt(i);
            POINT a = SceneToClient(s.x, s.y);
            POINT b = SceneToClient(s.x + s.width, s.y + s.height);
            RECT r = { a.x, a.y, b.x, b.y };
            COLORREF tint = TintFor(i, sel);
            bool selected = (i == sel && !s.locked && s.visible);

            HPEN solid = CreatePen(PS_SOLID, selected ? 2 : 1, s.visible ? tint : Ui::EyeOff());
            // Locked keeps its own hue (dotted = not draggable); only
            // hidden ghosts fall back to gray.
            HPEN dashes = CreatePen(PS_DOT, 1, s.visible ? tint : Ui::EyeOff());
            HGDIOBJ oldPen = SelectObject(hdc,
                s.visible ? (s.locked ? dashes : solid) : dashes);
            HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, r.left, r.top, r.right, r.bottom);

            // Label inside the square
            wchar_t label[160];
            if (s.visible)
                swprintf_s(label, L"%s - %dx%d", s.name.c_str(), s.width, s.height);
            else
                swprintf_s(label, L"%s - hidden", s.name.c_str());
            DrawLabelChip(hdc, r, label, tint, dpi, !s.visible);

            if (selected)
            {
                SelectObject(hdc, handleBrush);
                SelectObject(hdc, accentPen);
                static const DragMode order[] =
                { DragMode::TL, DragMode::TR, DragMode::BL, DragMode::BR,
                  DragMode::T, DragMode::B, DragMode::L, DragMode::R };
                for (DragMode h : order)
                {
                    RECT hr = HandleRect(h, r);
                    Rectangle(hdc, hr.left, hr.top, hr.right, hr.bottom);
                }
                RECT mh = MoveHandleRect(r);
                Ellipse(hdc, mh.left, mh.top, mh.right, mh.bottom);
            }
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBr);
            DeleteObject(solid);
            DeleteObject(dashes);
        }

        SelectObject(hdc, oldFont);
        DeleteObject(accentPen);
        DeleteObject(handleBrush);
        GdiFlush(); // memDC batch must land in the bits before upload
        if (rig->UploadOverlay(dibBits, pw, ph, dibStride))
        {
            rig->DrawOverlayQuad(pw, ph);
            m_overlayHash = ohash;
            m_overlayValid = true;
        }
        SelectObject(memDC, oldBmp);
        DeleteObject(dib);
        DeleteDC(memDC);
    }
    else if (m_overlayValid && pw > 0 && ph > 0)
    {
        // Nothing changed: composite the already-uploaded overlay texture.
        // (pw/ph match the upload: size is part of the hash.)
        rig->DrawOverlayQuad(pw, ph);
    }

    rig->PresentPreview();
    EndPaint(m_hwnd, &ps);
}

LRESULT CALLBACK Preview::Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Preview* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Preview*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<Preview*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_MEASUREITEM:
    {
        auto mis = reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);
        if (mis && Ui::MeasureDarkMenuItem(hwnd, mis))
            return TRUE;
        break;
    }
    case WM_DRAWITEM:
    {
        auto dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis && Ui::DrawDarkMenuItem(hwnd, dis))
            return TRUE;
        break;
    }
    case WM_PAINT:
        self->OnPaint();
        return 0;
    case WM_SIZE:
    {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (w > 0 && h > 0)
        {
            // Create once, resize after: recreating the swapchain on every
            // layout stormed the GPU and flashed stale-sized frames.
            if (!self->m_app->Rig()->HasPreviewSwapchain())
                self->m_app->Rig()->CreatePreviewSwapchain(hwnd, w, h);
            else
                self->m_app->Rig()->ResizePreview(w, h);
        }
        self->Refresh();
        return 0;
    }
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        self->OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
    {
        // Coalesce: preview drags flood the queue; drop stale moves and
        // process only the latest position each pump.
        MSG peek = {};
        int lx = GET_X_LPARAM(lParam), ly = GET_Y_LPARAM(lParam);
        while (PeekMessageW(&peek, hwnd, WM_MOUSEMOVE, WM_MOUSEMOVE, PM_REMOVE))
        {
            lx = GET_X_LPARAM(peek.lParam);
            ly = GET_Y_LPARAM(peek.lParam);
        }
        self->OnMouseMove(lx, ly);
        return 0;
    }
    case WM_LBUTTONUP:
        self->OnLButtonUp();
        return 0;
    case WM_RBUTTONUP:
        self->OnRButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_SETCURSOR:
    {
        if (self->m_drag != DragMode::None || ((HWND)wParam) == hwnd)
        {
            int cx = 0, cy = 0;
            POINT pt;
            if (GetCursorPos(&pt) && ScreenToClient(hwnd, &pt))
            {
                cx = pt.x;
                cy = pt.y;
            }
            int idx = -1;
            DragMode mode = self->m_drag != DragMode::None
                ? self->m_drag : self->HitTest(cx, cy, idx);
            LPCWSTR cur = IDC_ARROW;
            switch (mode)
            {
            case DragMode::Move:
            case DragMode::MoveHandle: cur = IDC_SIZEALL; break;
            case DragMode::L:
            case DragMode::R: cur = IDC_SIZEWE; break;
            case DragMode::T:
            case DragMode::B: cur = IDC_SIZENS; break;
            case DragMode::TL:
            case DragMode::BR: cur = IDC_SIZENWSE; break;
            case DragMode::TR:
            case DragMode::BL: cur = IDC_SIZENESW; break;
            default: break;
            }
            SetCursor(LoadCursorW(nullptr, cur));
            return TRUE;
        }
        break;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

