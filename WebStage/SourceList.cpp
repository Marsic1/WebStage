// ============================================================================
// WebStage - Source List Implementation
// ============================================================================

#include "SourceList.h"
#include "App.h"
#include "Ui.h"

#include <windowsx.h>

#include <algorithm>
#include <climits>
#include <cmath>

bool SourceList::Create(HWND parent, App* app)
{
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = SourceList::Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"WS_SourceList";
        if (!RegisterClassW(&wc))
            return false;
        registered = true;
    }

    m_app = app;
    // No WS_VSCROLL: the list paints its own dark scrollbar (BarRect).
    m_hwnd = CreateWindowExW(0, L"WS_SourceList", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 100, 100, parent, nullptr, GetModuleHandleW(nullptr), this);
    return m_hwnd != nullptr;
}

void SourceList::Refresh()
{
    if (!m_hwnd)
        return;
    // Follow the selection (e.g. picked in the preview), but don't yank
    // the user's manual scroll position on unrelated refreshes.
    if (m_app)
    {
        int sel = m_app->Selection();
        if (sel != m_lastSel)
        {
            m_lastSel = sel;
            if (sel >= 0)
                EnsureVisible(sel);
        }
    }
    UpdateScroll();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SourceList::SetPosition(int x, int y, int w, int h)
{
    if (m_hwnd)
        SetWindowPos(m_hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

int SourceList::RowHeight() const
{
    return Ui::Scale(m_hwnd, 48);
}

int SourceList::RowsTop() const
{
    return Ui::Scale(m_hwnd, 34);
}

int SourceList::FooterHeight() const
{
    // Two-line cheat sheet (never clipped, even in narrow windows)
    return Ui::Scale(m_hwnd, 38);
}

int SourceList::RowsHeight() const
{
    if (!m_hwnd)
        return 0;
    RECT rc = {};
    GetClientRect(m_hwnd, &rc);
    int h = (rc.bottom - rc.top) - RowsTop() - FooterHeight();
    return h > 0 ? h : 0;
}

int SourceList::RowAt(int clientY) const
{
    int rh = RowHeight();
    int i = (clientY - RowsTop() + m_scroll) / rh;
    if (i < 0 || i >= m_app->SourceCount())
        return -1;
    if (clientY < RowsTop() || clientY >= RowsTop() + RowsHeight())
        return -1;
    return i;
}

static int RowCenterY(int index, int rh, int scroll, int top)
{
    return top + index * rh - scroll + rh / 2;
}

int SourceList::EyeX() const
{
    return Ui::Scale(m_hwnd, 22);
}

int SourceList::LockX() const
{
    return Ui::Scale(m_hwnd, 46);
}

int SourceList::MuteX() const
{
    return Ui::Scale(m_hwnd, 68);
}

bool SourceList::EyeHit(int index, int clientX, int clientY) const
{
    int cx = EyeX();
    int cy = RowCenterY(index, RowHeight(), m_scroll, RowsTop());
    int r = Ui::Scale(m_hwnd, 12);
    int dx = clientX - cx;
    int dy = clientY - cy;
    return dx * dx + dy * dy <= r * r;
}

bool SourceList::LockHit(int index, int clientX, int clientY) const
{
    int cx = LockX();
    int cy = RowCenterY(index, RowHeight(), m_scroll, RowsTop());
    int r = Ui::Scale(m_hwnd, 12);
    int dx = clientX - cx;
    int dy = clientY - cy;
    return dx * dx + dy * dy <= r * r;
}

// Mockup eye (feather i-eye, 24-unit strokes in a 16px footprint):
// almond beziers + ring pupil, stroke 1.7 units, round caps/joins.
// Hidden draws the SAME shape in the dim unlocked-lock tone (no slash).
void SourceList::DrawEye(HDC dc, int x, int y, bool visible) const
{
    double k = Ui::Scale(m_hwnd, 16) / 24.0;
    if (k <= 0.0)
        k = 1.0;
    auto X = [&](double v) { return x + (int)std::lround((v - 12.0) * k); };
    auto Y = [&](double v) { return y + (int)std::lround((v - 12.0) * k); };
    COLORREF c = visible ? Ui::Fg() : Ui::EyeOff();
    LOGBRUSH lb = {};
    lb.lbStyle = BS_SOLID;
    lb.lbColor = c;
    HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
        (std::max)(1, (int)std::lround(1.7 * k)), &lb, 0, nullptr);
    HGDIOBJ oldPen = SelectObject(dc, pen ? pen : (HPEN)GetStockObject(DC_PEN));
    HGDIOBJ oldBr = SelectObject(dc, GetStockObject(NULL_BRUSH));
    // Almond: M2.8 12 S6.2 5.8 12 5.8 S21.2 12 21.2 12
    //         S17.8 18.2 12 18.2 S2.8 12 2.8 12 Z (S = reflected cubic)
    MoveToEx(dc, X(2.8), Y(12), nullptr);
    POINT almond[12] = {
        { X(2.8), Y(12) }, { X(6.2), Y(5.8) }, { X(12), Y(5.8) },
        { X(17.8), Y(5.8) }, { X(21.2), Y(12) }, { X(21.2), Y(12) },
        { X(21.2), Y(12) }, { X(17.8), Y(18.2) }, { X(12), Y(18.2) },
        { X(6.2), Y(18.2) }, { X(2.8), Y(12) }, { X(2.8), Y(12) },
    };
    PolyBezierTo(dc, almond, 12);
    // Pupil ring (12,12,r2.9), stroked like the mockup
    Ellipse(dc, X(9.1), Y(9.1), X(14.9), Y(14.9));
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    if (pen)
        DeleteObject(pen);
}

// Mockup padlock (feather i-lock / i-unlock, 24-unit strokes in a 16px
// footprint): outline body + arch shackle, stroke 1.7 units, round caps.
// Locked keeps the Fg tint, unlocked the dim style; the open shackle stub
// ends in a round cap.
void SourceList::DrawLock(HDC dc, int x, int y, bool locked) const
{
    double k = Ui::Scale(m_hwnd, 16) / 24.0;
    if (k <= 0.0)
        k = 1.0;
    auto X = [&](double v) { return x + (int)std::lround((v - 12.0) * k); };
    auto Y = [&](double v) { return y + (int)std::lround((v - 12.0) * k); };
    COLORREF c = locked ? Ui::Fg() : Ui::EyeOff();

    LOGBRUSH lb = {};
    lb.lbStyle = BS_SOLID;
    lb.lbColor = c;
    HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
        (std::max)(1, (int)std::lround(1.7 * k)), &lb, 0, nullptr);
    HGDIOBJ oldPen = SelectObject(dc, pen ? pen : (HPEN)GetStockObject(DC_PEN));
    HGDIOBJ oldBr = SelectObject(dc, GetStockObject(NULL_BRUSH));
    // Body: rect(5.5,10.5,13,9.5,rx2)
    int cr = (std::max)(1, (int)std::lround(4.0 * k));
    RoundRect(dc, X(5.5), Y(10.5), X(18.5), Y(20), cr, cr);
    if (locked)
    {
        // Shackle: M8.5 10.5V8 + r3.5 arch over (12,8) + V10.5.
        // kappa*3.5 = 1.933.
        MoveToEx(dc, X(8.5), Y(10.5), nullptr);
        LineTo(dc, X(8.5), Y(8));
        POINT arch[6] = {
            { X(8.5), Y(6.067) }, { X(10.067), Y(4.5) }, { X(12), Y(4.5) },
            { X(13.933), Y(4.5) }, { X(15.5), Y(6.067) }, { X(15.5), Y(8) },
        };
        PolyBezierTo(dc, arch, 6);
        LineTo(dc, X(15.5), Y(10.5));
    }
    else
    {
        // Open: M8.5 10.5V8 + arch ending at (15.4,7.1), round cap stub.
        MoveToEx(dc, X(8.5), Y(10.5), nullptr);
        LineTo(dc, X(8.5), Y(8));
        POINT arch[6] = {
            { X(8.5), Y(6.067) }, { X(10.067), Y(4.5) }, { X(12), Y(4.5) },
            { X(13.6), Y(4.5) }, { X(15.0), Y(5.5) }, { X(15.4), Y(7.1) },
        };
        PolyBezierTo(dc, arch, 6);
    }
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    if (pen)
        DeleteObject(pen);
}

// Muted badge (16x16 design space, same as the eye): filled speaker +
// a round-capped X parked right of the cone. Both X arms are symmetric
// about (11.6, 8) and coordinates round (not truncate) so the cross lands
// centered at any DPI. Signal red. Shown only when muted.
void SourceList::DrawMutedIcon(HDC dc, int x, int y) const
{
    double k = Ui::Scale(m_hwnd, 16) / 16.0;
    if (k <= 0.0)
        k = 1.0;
    auto X = [&](double v) { return x + (int)std::lround((v - 8.0) * k); };
    auto Y = [&](double v) { return y + (int)std::lround((v - 8.0) * k); };
    COLORREF c = Ui::Stop();
    HPEN pen = CreatePen(PS_SOLID, (std::max)(1, (int)std::lround(k * 1.3)), c);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HBRUSH body = CreateSolidBrush(c);
    HGDIOBJ oldBr = SelectObject(dc, body);
    // Speaker back
    int bw = (std::max)(1, (int)std::lround(k * 1.2));
    RoundRect(dc, X(1.2), Y(6.0), X(4.0), Y(10.0), bw, bw);
    // Speaker cone
    POINT cone[5] = {
        { X(4.0), Y(6.0) }, { X(7.0), Y(2.8) },
        { X(7.0), Y(13.2) }, { X(4.0), Y(10.0) },
        { X(4.0), Y(6.0) },
    };
    Polygon(dc, cone, 5);
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(body);
    DeleteObject(pen);
    // Mute X: geometric pen for round caps, arms centered on (11.6, 8)
    LOGBRUSH lb = {};
    lb.lbStyle = BS_SOLID;
    lb.lbColor = c;
    HPEN xpen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
        (std::max)(1, (int)std::lround(k * 1.8)), &lb, 0, nullptr);
    HGDIOBJ oldXP = SelectObject(dc, xpen ? xpen : (HPEN)GetStockObject(DC_PEN));
    MoveToEx(dc, X(9.0), Y(5.4), nullptr);
    LineTo(dc, X(14.2), Y(10.6));
    MoveToEx(dc, X(9.0), Y(10.6), nullptr);
    LineTo(dc, X(14.2), Y(5.4));
    SelectObject(dc, oldXP);
    if (xpen)
        DeleteObject(xpen);
}

int SourceList::MaxScroll() const
{
    if (!m_hwnd || !m_app)
        return 0;
    int page = RowsHeight();
    if (page <= 0)
        return 0;
    int total = m_app->SourceCount() * RowHeight();
    return total > page ? total - page : 0;
}

void SourceList::UpdateScroll()
{
    if (!m_hwnd || !m_app)
        return;
    // Clamp only: the bar is custom-painted (BarRect/ThumbRect), the
    // native scrollbar stays off.
    int maxS = MaxScroll();
    if (m_scroll > maxS)
        m_scroll = maxS;
    if (m_scroll < 0)
        m_scroll = 0;
}

bool SourceList::HasOverflow() const
{
    if (!m_hwnd || !m_app)
        return false;
    return m_app->SourceCount() * RowHeight() > RowsHeight();
}

int SourceList::RightPad() const
{
    // Keep text clear of the custom scrollbar when it is shown.
    return Ui::Scale(m_hwnd, HasOverflow() ? 22 : 6);
}

RECT SourceList::BarRect() const
{
    RECT rc = { 0, 0, 0, 0 };
    if (!m_hwnd || !HasOverflow())
        return rc;
    GetClientRect(m_hwnd, &rc);
    int w = Ui::Scale(m_hwnd, 14);
    rc.left = rc.right - w;
    rc.top = RowsTop();
    rc.bottom -= FooterHeight();
    return rc;
}

RECT SourceList::ThumbRect() const
{
    RECT bar = BarRect();
    RECT empty = { 0, 0, 0, 0 };
    if (bar.right <= bar.left || !m_app)
        return empty;
    int page = bar.bottom - bar.top;
    int total = m_app->SourceCount() * RowHeight();
    int maxS = total > page ? total - page : 0;
    if (maxS <= 0)
        return empty;
    int trackH = page;
    int thumbH = (std::max)(Ui::Scale(m_hwnd, 30), trackH * page / total);
    if (thumbH > trackH)
        thumbH = trackH;
    int thumbY = bar.top + m_scroll * (trackH - thumbH) / maxS;
    RECT th = { bar.left + Ui::Scale(m_hwnd, 3), thumbY,
        bar.right - Ui::Scale(m_hwnd, 3), thumbY + thumbH };
    return th;
}

void SourceList::EnsureVisible(int index)
{
    if (!m_hwnd || !m_app || index < 0 || index >= m_app->SourceCount())
        return;
    int page = RowsHeight();
    int rh = RowHeight();
    int top = index * rh;
    int bottom = top + rh;
    if (top < m_scroll)
        m_scroll = top;
    else if (bottom > m_scroll + page)
        m_scroll = bottom - page;
    UpdateScroll();
}

void SourceList::OnPaint()
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(m_hwnd, &ps);

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int W = rc.right - rc.left;
    int H = rc.bottom - rc.top;

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, W > 0 ? W : 1, H > 0 ? H : 1);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(Ui::BgPanel());
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    HGDIOBJ oldFont = SelectObject(mem, Ui::Font());
    SetBkMode(mem, TRANSPARENT);

    int top = RowsTop();
    int rowsH = RowsHeight();
    int rh = RowHeight();
    int count = m_app->SourceCount();
    int sel = m_app->Selection();

    // Rows (clipped to the rows band so header/footer stay clean)
    int saved = SaveDC(mem);
    IntersectClipRect(mem, 0, top, W, top + rowsH);

    for (int i = 0; i < count; i++)
    {
        const SourceSettings& s = m_app->SourceAt(i);
        RECT row = { 0, top + i * rh - m_scroll, W, top + (i + 1) * rh - m_scroll };
        if (row.top > ps.rcPaint.bottom || row.bottom < ps.rcPaint.top)
            continue;

        if (i == sel)
        {
            HBRUSH hb = CreateSolidBrush(Ui::Hover());
            FillRect(mem, &row, hb);
            DeleteObject(hb);
            RECT bar = { 0, row.top, Ui::Scale(m_hwnd, 3), row.bottom };
            HBRUSH ab = CreateSolidBrush(Ui::Accent());
            FillRect(mem, &bar, ab);
            DeleteObject(ab);
        }

        DrawEye(mem, EyeX(), row.top + rh / 2, s.visible);
        DrawLock(mem, LockX(), row.top + rh / 2, s.locked);
        if (s.muted)
            DrawMutedIcon(mem, MuteX(), row.top + rh / 2);

        int tx = Ui::Scale(m_hwnd, 86);
        int edge = W - RightPad();
        HGDIOBJ rowFont = SelectObject(mem, Ui::FontBold());
        SetTextColor(mem, s.visible ? Ui::Fg() : Ui::Gray());
        RECT nr = { tx, row.top + Ui::Scale(m_hwnd, 5), edge, row.top + Ui::Scale(m_hwnd, 25) };
        DrawTextW(mem, s.name.c_str(), -1, &nr, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(mem, Ui::FontMono());

        bool hasHk = !s.hotkey.empty() && s.hotkey != "none";
        wchar_t sub[256];
        swprintf_s(sub, L"%dx%d @ (%d,%d)", s.width, s.height, s.x, s.y);
        std::wstring subw = sub;
        if (!s.visible)
            subw += L" - hidden";
        if (s.locked)
            subw += L" - locked";
        SetTextColor(mem, Ui::Gray());
        int chipW = 0;
        if (hasHk)
        {
            std::wstring hk = Ui::FromUtf8(s.hotkey);
            SIZE hs = {};
            GetTextExtentPoint32W(mem, hk.c_str(), (int)hk.size(), &hs);
            chipW = hs.cx + Ui::Scale(m_hwnd, 12);
            RECT sr = { tx, row.top + Ui::Scale(m_hwnd, 25), edge - chipW - Ui::Scale(m_hwnd, 6),
                row.bottom - Ui::Scale(m_hwnd, 3) };
            DrawTextW(mem, subw.c_str(), -1, &sr, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        else
        {
            RECT sr = { tx, row.top + Ui::Scale(m_hwnd, 25), edge, row.bottom - Ui::Scale(m_hwnd, 3) };
            DrawTextW(mem, subw.c_str(), -1, &sr, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }

        // Hotkey as a bordered mono chip, right-aligned
        if (hasHk)
        {
            std::wstring hk = Ui::FromUtf8(s.hotkey);
            RECT cr = { edge - chipW, row.top + Ui::Scale(m_hwnd, 26),
                edge, row.bottom - Ui::Scale(m_hwnd, 4) };
            HBRUSH cb = CreateSolidBrush(Ui::ChipBg());
            FillRect(mem, &cr, cb);
            DeleteObject(cb);
            HPEN cp = CreatePen(PS_SOLID, 1, Ui::ChipBorder());
            HGDIOBJ oldPen = SelectObject(mem, cp);
            HGDIOBJ oldBr = SelectObject(mem, GetStockObject(NULL_BRUSH));
            int rad = Ui::Scale(m_hwnd, 4);
            RoundRect(mem, cr.left, cr.top, cr.right, cr.bottom, rad, rad);
            SelectObject(mem, oldPen);
            SelectObject(mem, oldBr);
            SetTextColor(mem, Ui::Accent());
            RECT tr = { cr.left + Ui::Scale(m_hwnd, 5), cr.top, cr.right, cr.bottom };
            DrawTextW(mem, hk.c_str(), -1, &tr,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        SelectObject(mem, rowFont);

        HPEN sep = CreatePen(PS_SOLID, 1, Ui::Sep());
        HGDIOBJ oldSep = SelectObject(mem, sep);
        MoveToEx(mem, 0, row.bottom - 1, nullptr);
        LineTo(mem, W, row.bottom - 1);
        SelectObject(mem, oldSep);
        DeleteObject(sep);
    }

    // Reorder drop indicator
    if (m_rowDrag && m_dropTarget >= 0 && m_dropTarget < m_app->SourceCount()
        && (m_dropTarget != m_dragRow || m_dropBelow))
    {
        int yl = top + (m_dropBelow ? m_dropTarget + 1 : m_dropTarget) * rh - m_scroll;
        HPEN ind = CreatePen(PS_SOLID, 2, Ui::Accent());
        HGDIOBJ oldInd = SelectObject(mem, ind);
        MoveToEx(mem, 0, yl, nullptr);
        LineTo(mem, W, yl);
        SelectObject(mem, oldInd);
        DeleteObject(ind);
    }

    if (count == 0)
    {
        SetTextColor(mem, Ui::Gray());
        RECT hr = { Ui::Scale(m_hwnd, 14), top + Ui::Scale(m_hwnd, 10),
            W - Ui::Scale(m_hwnd, 14), top + rowsH };
        DrawTextW(mem, L"No sources yet.\r\nClick Add to add an HTTP overlay.",
            -1, &hr, DT_WORDBREAK | DT_NOPREFIX);
    }

    RestoreDC(mem, saved);

    // Header band: SOURCES + live count chip
    {
        SetTextColor(mem, Ui::Gray());
        RECT tr = { Ui::Scale(m_hwnd, 14), 0, W, top };
        DrawTextW(mem, L"SOURCES", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        wchar_t cnt[16];
        swprintf_s(cnt, L"%d", count);
        SelectObject(mem, Ui::FontMono());
        SIZE cs = {};
        GetTextExtentPoint32W(mem, cnt, (int)wcslen(cnt), &cs);
        int cw = cs.cx + Ui::Scale(m_hwnd, 12);
        int ch = Ui::Scale(m_hwnd, 18);
        int cy = (top - ch) / 2;
        RECT cr = { W - Ui::Scale(m_hwnd, 12) - cw, cy, W - Ui::Scale(m_hwnd, 12), cy + ch };
        HBRUSH cb = CreateSolidBrush(Ui::BgEdit());
        FillRect(mem, &cr, cb);
        DeleteObject(cb);
        HPEN cp = CreatePen(PS_SOLID, 1, Ui::Sep());
        HGDIOBJ oldPen = SelectObject(mem, cp);
        HGDIOBJ oldBr = SelectObject(mem, GetStockObject(NULL_BRUSH));
        RoundRect(mem, cr.left, cr.top, cr.right, cr.bottom, Ui::Scale(m_hwnd, 8), Ui::Scale(m_hwnd, 8));
        SelectObject(mem, oldPen);
        SelectObject(mem, oldBr);
        SetTextColor(mem, Ui::Gray());
        DrawTextW(mem, cnt, -1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(mem, Ui::Font());
        HPEN line = CreatePen(PS_SOLID, 1, Ui::Sep());
        HGDIOBJ oldLine = SelectObject(mem, line);
        MoveToEx(mem, 0, top - 1, nullptr);
        LineTo(mem, W, top - 1);
        SelectObject(mem, oldLine);
        DeleteObject(line);
    }

    // Footer band: drag + keyboard cheat sheet, always two full rows
    {
        int fy = top + rowsH;
        HPEN line = CreatePen(PS_SOLID, 1, Ui::Sep());
        HGDIOBJ oldLine = SelectObject(mem, line);
        MoveToEx(mem, 0, fy, nullptr);
        LineTo(mem, W, fy);
        SelectObject(mem, oldLine);
        DeleteObject(line);
        SelectObject(mem, Ui::FontMono());
        SetTextColor(mem, Ui::Gray());
        int lx = Ui::Scale(m_hwnd, 14);
        int lh = Ui::Scale(m_hwnd, 15);
        RECT fr1 = { lx, fy + Ui::Scale(m_hwnd, 3), W - Ui::Scale(m_hwnd, 8), fy + Ui::Scale(m_hwnd, 3) + lh };
        RECT fr2 = { lx, fy + Ui::Scale(m_hwnd, 3) + lh, W - Ui::Scale(m_hwnd, 8), fy + Ui::Scale(m_hwnd, 3) + lh * 2 };
        DrawTextW(mem, L"Drag to reorder, Space = eye,",
            -1, &fr1, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        DrawTextW(mem, L"L = lock, Del = remove",
            -1, &fr2, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(mem, Ui::Font());
    }

    // Custom dark scrollbar (native bar stays off for theming)
    if (HasOverflow())
    {
        RECT bar = BarRect();
        HBRUSH track = CreateSolidBrush(Ui::Bg());
        FillRect(mem, &bar, track);
        DeleteObject(track);

        RECT th = ThumbRect();
        if (th.bottom > th.top)
        {
            bool hot = m_barHover || m_thumbDrag;
            HBRUSH tb = CreateSolidBrush(hot ? Ui::Hover() : Ui::Sep());
            int rad = Ui::Scale(m_hwnd, 6);
            HPEN tp = CreatePen(PS_SOLID, 1, hot ? Ui::Hover() : Ui::Sep());
            HGDIOBJ oldPen = SelectObject(mem, tp);
            HGDIOBJ oldBr = SelectObject(mem, tb);
            RoundRect(mem, th.left, th.top, th.right, th.bottom, rad, rad);
            SelectObject(mem, oldPen);
            SelectObject(mem, oldBr);
            DeleteObject(tp);
            DeleteObject(tb);
        }
    }

    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    SelectObject(mem, oldFont);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(m_hwnd, &ps);
}

LRESULT CALLBACK SourceList::Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SourceList* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SourceList*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<SourceList*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        self->OnPaint();
        return 0;
    case WM_SIZE:
        self->UpdateScroll();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_MOUSEWHEEL:
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int rows = delta / WHEEL_DELTA;
        if (rows == 0)
            rows = (delta > 0 ? 1 : -1);
        int pos = self->m_scroll - rows * self->RowHeight();
        int maxS = self->MaxScroll();
        if (pos > maxS)
            pos = maxS;
        if (pos < 0)
            pos = 0;
        if (pos != self->m_scroll)
        {
            self->m_scroll = pos;
            self->UpdateScroll();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        SetFocus(hwnd);
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        // Custom scrollbar first
        if (self->HasOverflow())
        {
            RECT bar = self->BarRect();
            if (x >= bar.left && x < bar.right && y >= bar.top && y < bar.bottom)
            {
                RECT th = self->ThumbRect();
                if (y >= th.top && y < th.bottom)
                {
                    self->m_thumbDrag = true;
                    self->m_thumbGrab = y - th.top;
                    SetCapture(hwnd);
                }
                else
                {
                    // Page jump
                    int page = self->RowsHeight();
                    int pos = self->m_scroll + (y < th.top ? -page : page);
                    int maxS = self->MaxScroll();
                    if (pos > maxS)
                        pos = maxS;
                    if (pos < 0)
                        pos = 0;
                    if (pos != self->m_scroll)
                    {
                        self->m_scroll = pos;
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                }
                return 0;
            }
        }
        int i = self->RowAt(y);
        if (i >= 0)
        {
            if (self->EyeHit(i, x, y))
                self->m_app->ToggleVisible(i);
            else if (self->LockHit(i, x, y))
                self->m_app->ToggleLocked(i);
            else
            {
                self->m_app->Select(i);
                // Arm a possible reorder drag (fires past the drag threshold)
                self->m_rowArmed = true;
                self->m_dragRow = i;
                self->m_downPt = { x, y };
            }
        }
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        if (self->m_thumbDrag)
        {
            RECT bar = self->BarRect();
            RECT th = self->ThumbRect();
            int trackH = bar.bottom - bar.top;
            int thumbH = th.bottom - th.top;
            int maxS = self->MaxScroll();
            int pos = self->m_scroll;
            if (maxS > 0 && trackH > thumbH)
                pos = (y - bar.top - self->m_thumbGrab) * maxS / (trackH - thumbH);
            if (pos > maxS)
                pos = maxS;
            if (pos < 0)
                pos = 0;
            if (pos != self->m_scroll)
            {
                self->m_scroll = pos;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        // Row reorder drag (button still held from a plain-row press)
        if ((self->m_rowArmed || self->m_rowDrag) && (wParam & MK_LBUTTON))
        {
            if (!self->m_rowDrag)
            {
                int dx = abs(x - self->m_downPt.x);
                int dy = abs(y - self->m_downPt.y);
                if (dx > GetSystemMetrics(SM_CXDRAG) || dy > GetSystemMetrics(SM_CYDRAG))
                {
                    self->m_rowDrag = true;
                    self->m_dropTarget = self->m_dragRow;
                    self->m_dropBelow = false;
                    SetCapture(hwnd);
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                }
                else
                    return 0;
            }
            // Edge auto-scroll while dragging
            RECT rc = {};
            GetClientRect(hwnd, &rc);
            int H = self->RowsTop() + self->RowsHeight();
            int edge = Ui::Scale(hwnd, 24);
            int step = (std::max)(12, self->RowHeight() / 4);
            int count = self->m_app ? self->m_app->SourceCount() : 0;
            if (count > 0)
            {
                if (y < edge)
                    self->m_scroll -= step;
                else if (y > H - edge)
                    self->m_scroll += step;
                int maxS = self->MaxScroll();
                if (self->m_scroll > maxS)
                    self->m_scroll = maxS;
                if (self->m_scroll < 0)
                    self->m_scroll = 0;
                int target = (y - self->RowsTop() + self->m_scroll) / self->RowHeight();
                bool below = (y - self->RowsTop() + self->m_scroll) >= count * self->RowHeight();
                if (target < 0)
                    target = 0;
                if (target > count - 1)
                    target = count - 1;
                if (target != self->m_dropTarget || below != self->m_dropBelow)
                {
                    self->m_dropTarget = target;
                    self->m_dropBelow = below;
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        // Thumb hover highlight
        if (self->HasOverflow())
        {
            if (!self->m_trackMouse)
            {
                self->m_trackMouse = true;
                TRACKMOUSEEVENT tme = { sizeof(tme) };
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
            }
            RECT th = self->ThumbRect();
            bool hot = x >= th.left && x < th.right && y >= th.top && y < th.bottom;
            if (hot != self->m_barHover)
            {
                self->m_barHover = hot;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (self->m_thumbDrag)
        {
            self->m_thumbDrag = false;
            ReleaseCapture();
            return 0;
        }
        if (self->m_rowDrag)
        {
            int from = self->m_dragRow;
            int to = self->m_dropTarget;
            self->m_rowDrag = false;
            self->m_rowArmed = false;
            self->m_dragRow = -1;
            self->m_dropTarget = -1;
            self->m_dropBelow = false;
            ReleaseCapture();
            if (to >= 0 && from != to)
                self->m_app->MoveSourceTo(from, to);
            else
                InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        self->m_rowArmed = false;
        break;
    case WM_SETCURSOR:
        if (self->m_rowDrag)
        {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return TRUE;
        }
        break;
    case WM_CAPTURECHANGED:
        // Capture stolen (e.g. Alt+Tab mid-drag): drop all drag state.
        self->m_thumbDrag = false;
        self->m_rowDrag = false;
        self->m_rowArmed = false;
        self->m_dragRow = -1;
        self->m_dropTarget = -1;
        self->m_dropBelow = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    case WM_MOUSELEAVE:
        self->m_trackMouse = false;
        if (self->m_barHover)
        {
            self->m_barHover = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
    {
        if (wParam == VK_ESCAPE && self->m_rowDrag)
        {
            // Cancel the reorder drag, keep order and selection
            self->m_rowDrag = false;
            self->m_rowArmed = false;
            self->m_dragRow = -1;
            self->m_dropTarget = -1;
            self->m_dropBelow = false;
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        int sel = self->m_app->Selection();
        int count = self->m_app->SourceCount();
        if (wParam == VK_UP && sel > 0)
            self->m_app->Select(sel - 1);
        else if (wParam == VK_DOWN && sel + 1 < count)
            self->m_app->Select(sel + 1);
        else if (wParam == VK_SPACE && sel >= 0)
            self->m_app->ToggleVisible(sel);
        else if ((wParam == 'L' || wParam == 'l') && sel >= 0)
            self->m_app->ToggleLocked(sel);
        else if ((wParam == VK_DELETE || wParam == VK_BACK) && sel >= 0)
            self->m_app->DeleteSource(sel);
        return 0;
    }
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

