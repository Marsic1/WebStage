// ============================================================================
// WebStage - Properties Panel Implementation
// ============================================================================

#include "Properties.h"
#include "App.h"
#include "Ui.h"

#include <uxtheme.h>

#include <windowsx.h>

bool Properties::Create(HWND parent, App* app)
{
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = Properties::Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = Ui::DarkBgBrush();
        wc.lpszClassName = L"WS_Props";
        if (!RegisterClassW(&wc))
            return false;
        registered = true;
    }

    m_app = app;
    m_hwnd = CreateWindowExW(0, L"WS_Props", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 100, 100, parent, nullptr, GetModuleHandleW(nullptr), this);
    if (!m_hwnd)
        return false;

    BuildControls();
    return true;
}

void Properties::SetPosition(int x, int y, int w, int h)
{
    if (m_hwnd)
        SetWindowPos(m_hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void Properties::BuildControls()
{
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, HMENU id) -> HWND {
        HWND h = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style,
            0, 0, 10, 10, m_hwnd, id, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)Ui::Font(), 0);
        // Dark-themed non-client edge (border): the stock WS_BORDER edge
        // would paint light no matter the client colors.
        if (wcscmp(cls, L"EDIT") == 0)
            SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
        return h;
    };

    // Section headers (owner-drawn: small caps + hairline)
    mk(L"STATIC", L"SOURCE", SS_OWNERDRAW, (HMENU)(INT_PTR)IdSecSource);
    mk(L"STATIC", L"GEOMETRY", SS_OWNERDRAW, (HMENU)(INT_PTR)IdSecGeometry);
    mk(L"STATIC", L"OUTPUT", SS_OWNERDRAW, (HMENU)(INT_PTR)IdSecOutput);

    // SOURCE
    // NOTE: edits are intentionally borderless (no WS_BORDER): the panel
    // draws mockup-style rounded borders around them in OnPaint, with an
    // accent ring on the focused one.
    m_nameLab = mk(L"STATIC", L"Name", SS_LEFT, nullptr);
    // NOTE: ES_MULTILINE is deliberate. A tall single-line edit pins its
    // text to the top (EM_SETRECT is ignored on single-line), while a
    // borderless multiline honors the EM_SETRECT rect from
    // CenterEditVertically, so the text sits truly centered. Enter is
    // eaten by FieldSubclass, so behavior stays single-line.
    mk(L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | ES_MULTILINE, (HMENU)IdName);

    m_hkLab = mk(L"STATIC", L"Hotkey", SS_LEFT, nullptr);
    // Owner-drawn green combo chip, same look as the source-list hotkey chip
    mk(L"STATIC", L"none", SS_OWNERDRAW, (HMENU)(INT_PTR)IdHotkeyValue);
    mk(L"BUTTON", L"Set", BS_OWNERDRAW, (HMENU)(INT_PTR)IdHotkeySet);
    mk(L"BUTTON", L"Clear", BS_OWNERDRAW, (HMENU)(INT_PTR)IdHotkeyClear);

    m_urlLab = mk(L"STATIC", L"URL", SS_LEFT, nullptr);
    m_urlEdit = mk(L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | ES_MULTILINE, (HMENU)(INT_PTR)IdUrl);
    SetWindowLongPtrW(m_urlEdit, GWLP_USERDATA, (LONG_PTR)this);
    // Subclass the URL edit so Enter applies it
    m_fieldOrigProc = (WNDPROC)SetWindowLongPtrW(m_urlEdit, GWLP_WNDPROC, (LONG_PTR)FieldSubclass);
    mk(L"BUTTON", L"CSS", BS_OWNERDRAW, (HMENU)(INT_PTR)IdCss);
    Ui::SetButtonIcon(GetDlgItem(m_hwnd, IdCss), Ui::IconCode);

    // GEOMETRY: the letter chips are drag-scrub handles for the paired edit
    const wchar_t* nums[] = { L"X", L"Y", L"W", L"H", L"RW", L"RH" };
    for (int i = 0; i < 6; i++)
    {
        m_geoLab[i] = mk(L"STATIC", nums[i], SS_OWNERDRAW, (HMENU)(INT_PTR)(IdLabX + i));
        SetWindowLongPtrW(m_geoLab[i], GWLP_USERDATA, (LONG_PTR)this);
        m_geoOrigProc = (WNDPROC)SetWindowLongPtrW(m_geoLab[i], GWLP_WNDPROC, (LONG_PTR)ScrubLabelProc);
        HWND e = mk(L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | ES_MULTILINE | ES_NUMBER, (HMENU)(INT_PTR)(IdX + i));
        SetWindowLongPtrW(e, GWLP_USERDATA, (LONG_PTR)this);
        SetWindowLongPtrW(e, GWLP_WNDPROC, (LONG_PTR)FieldSubclass);
    }

    // OUTPUT
    mk(L"BUTTON", L"Visible", BS_OWNERDRAW, (HMENU)IdVisible);
    mk(L"BUTTON", L"Muted", BS_OWNERDRAW, (HMENU)IdMuted);

    m_opLab = mk(L"STATIC", L"Opacity", SS_LEFT, nullptr);
    m_opacity.Create(m_hwnd, IdOpacity);
    mk(L"STATIC", L"100%", SS_LEFT, (HMENU)(INT_PTR)IdOpacityVal);
    m_volLab = mk(L"STATIC", L"Volume", SS_LEFT, nullptr);
    m_volume.Create(m_hwnd, IdVolume);
    mk(L"STATIC", L"100%", SS_LEFT, (HMENU)(INT_PTR)IdVolumeVal);

    // Tab order support (this window is not a dialog): mark focusables
    // WS_TABSTOP. Tab itself is forwarded by FieldSubclass (edits),
    // Slider::Proc, and MakeTabbable (buttons below).
    for (int id : { IdName, IdUrl, IdX, IdY, IdW, IdH, IdRw, IdRh,
                    IdHotkeySet, IdHotkeyClear, IdCss, IdVisible, IdMuted })
    {
        HWND c = GetDlgItem(m_hwnd, id);
        if (c)
            SetWindowLongW(c, GWL_STYLE,
                (DWORD)GetWindowLongW(c, GWL_STYLE) | WS_TABSTOP);
    }
    for (int id : { IdHotkeySet, IdHotkeyClear, IdCss, IdVisible, IdMuted })
        Ui::MakeTabbable(GetDlgItem(m_hwnd, id));
}

void Properties::Layout()
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int W = rc.right - rc.left;
    auto S = [&](int v) { return Ui::Scale(m_hwnd, v); };

    const int pad = S(8);
    const int labW = S(48);
    const int secH = S(16);
    const int gap = S(6);

    // The UI font can outgrow the base row at high DPI (its tmHeight fills
    // the whole box, so text reads glued to the top). Measure first and
    // grow the rows to fit: boxes always clear the text with padding, so
    // the text reads centered no matter how the edit lays out text.
    // (EM_SETRECT centering in CenterEditVertically stays as backup.)
    int rowH = S(26);
    int textH = S(16);
    {
        HDC dc = GetDC(m_hwnd);
        if (dc)
        {
            HGDIOBJ old = SelectObject(dc, Ui::Font());
            TEXTMETRICW tm = {};
            GetTextMetricsW(dc, &tm);
            SelectObject(dc, old);
            ReleaseDC(m_hwnd, dc);
            textH = tm.tmHeight;
            int need = textH + S(8);
            if (need > rowH)
                rowH = need;
        }
    }
    int editH = rowH, dyEdit = 0;
    int labDy = (rowH - textH) / 2; // static labels sit level with edit text
    if (labDy < S(2))
        labDy = S(2);

    int y = S(6);

    // ---- SOURCE ----
    SetWindowPos(GetDlgItem(m_hwnd, IdSecSource), nullptr, pad, y, W - pad * 2, secH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    y += secH + gap;

    // Row: Name ..... | Hotkey [chip] Set Clear (cluster right-aligned)
    {
        int setW = S(52), clearW = S(52), chipW = S(104), hkLabW = S(44);
        int rx = W - pad;
        SetWindowPos(GetDlgItem(m_hwnd, IdHotkeyClear), nullptr, rx - clearW, y, clearW, rowH,
            SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(GetDlgItem(m_hwnd, IdHotkeySet), nullptr, rx - clearW - S(4) - setW, y, setW, rowH,
            SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(GetDlgItem(m_hwnd, IdHotkeyValue), nullptr,
            rx - clearW - S(4) - setW - S(4) - chipW, y + S(4), chipW, rowH - S(6),
            SWP_NOZORDER | SWP_NOACTIVATE);
        int hkX = rx - clearW - S(4) - setW - S(4) - chipW - S(6) - hkLabW;
        if (m_hkLab)
            SetWindowPos(m_hkLab, nullptr, hkX, y + labDy, hkLabW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
        int nameW = hkX - S(8) - (pad + labW);
        if (nameW < S(80)) nameW = S(80);
        if (m_nameLab)
            SetWindowPos(m_nameLab, nullptr, pad, y + labDy, labW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(GetDlgItem(m_hwnd, IdName), nullptr, pad + labW, y + dyEdit, nameW, editH,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    y += rowH + gap;

    // Row: URL [..........] [CSS]
    {
        int cssW = S(56);
        if (m_urlLab)
            SetWindowPos(m_urlLab, nullptr, pad, y + labDy, labW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(GetDlgItem(m_hwnd, IdUrl), nullptr, pad + labW, y + dyEdit,
            W - pad - cssW - S(6) - (pad + labW), editH, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(GetDlgItem(m_hwnd, IdCss), nullptr, W - pad - cssW, y, cssW, rowH,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    y += rowH + S(10);

    // ---- GEOMETRY ----
    SetWindowPos(GetDlgItem(m_hwnd, IdSecGeometry), nullptr, pad, y, W - pad * 2, secH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    y += secH + gap;

    // One row: [X][v] [Y][v] [W][v] [H][v]  ...  [RW][v] [RH][v]
    {
        const int chipW1 = S(22), chipW2 = S(30), numW = S(56), pairGap = S(10);
        int x = pad;
        for (int i = 0; i < 6; i++)
        {
            int cw = (i < 4) ? chipW1 : chipW2;
            SetWindowPos(m_geoLab[i], nullptr, x, y + dyEdit, cw, editH, SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(GetDlgItem(m_hwnd, IdX + i), nullptr, x + cw, y + dyEdit, numW, editH,
                SWP_NOZORDER | SWP_NOACTIVATE);
            if (i == 3)
            {
                int rhX = W - pad - chipW2 - numW;
                int rwX = rhX - pairGap - chipW2 - numW;
                if (rwX < x + pairGap) rwX = x + pairGap;
                x = rwX;
            }
            else
            {
                x += cw + numW + pairGap;
            }
        }
    }
    y += rowH + S(10);

    // ---- OUTPUT ----
    SetWindowPos(GetDlgItem(m_hwnd, IdSecOutput), nullptr, pad, y, W - pad * 2, secH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    y += secH + gap;

    // Toggles row
    int tx = pad;
    SetWindowPos(GetDlgItem(m_hwnd, IdVisible), nullptr, tx, y, S(88), rowH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    tx += S(96);
    SetWindowPos(GetDlgItem(m_hwnd, IdMuted), nullptr, tx, y, S(80), rowH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    y += rowH + gap;

    // Sliders row: Opacity | Volume halves
    int half = (W - pad * 2 - S(12)) / 2;
    if (m_opLab)
        SetWindowPos(m_opLab, nullptr, pad, y + labDy, S(48), rowH, SWP_NOZORDER | SWP_NOACTIVATE);
    m_opacity.SetPosition(pad + S(52), y, half - S(52) - S(38), rowH);
    SetWindowPos(GetDlgItem(m_hwnd, IdOpacityVal), nullptr, pad + half - S(34), y + labDy, S(34), rowH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    int vx = pad + half + S(12);
    if (m_volLab)
        SetWindowPos(m_volLab, nullptr, vx, y + labDy, S(48), rowH, SWP_NOZORDER | SWP_NOACTIVATE);
    m_volume.SetPosition(vx + S(52), y, W - pad - S(34) - (vx + S(52)), rowH);
    SetWindowPos(GetDlgItem(m_hwnd, IdVolumeVal), nullptr, W - pad - S(34), y + labDy, S(34), rowH,
        SWP_NOZORDER | SWP_NOACTIVATE);

    // Multiline edits honor the EM_SETRECT rect: recenter after every
    // layout (size changes would uncenter them again).
    for (int id = IdName; id <= IdRh; id++)
        Ui::CenterEditVertically(GetDlgItem(m_hwnd, id));

    // Children don't auto-repaint on same-size moves: force one full pass
    // so resizes never leave trails. NOTE: under WS_CLIPCHILDREN the
    // parent invalidate does NOT repaint children, so every child must be
    // invalidated individually (statics, buttons, sliders, edits).
    InvalidateRect(m_hwnd, nullptr, TRUE);
    EnumChildWindows(m_hwnd, [](HWND c, LPARAM) -> BOOL {
        InvalidateRect(c, nullptr, TRUE);
        return TRUE;
    }, 0);
}

// ============================================================================
// Section headers + scrub chips (owner-drawn statics)
// ============================================================================
void Properties::DrawSectionHeader(const DRAWITEMSTRUCT* dis, bool withHint)
{
    if (!dis)
        return;
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;

    HBRUSH bg = CreateSolidBrush(Ui::Bg());
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    wchar_t text[64] = {};
    GetWindowTextW(dis->hwndItem, text, ARRAYSIZE(text));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, Ui::Gray());
    HGDIOBJ oldFont = SelectObject(dc, Ui::Font());
    RECT tr = rc;
    DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SIZE sz = {};
    GetTextExtentPoint32W(dc, text, (int)wcslen(text), &sz);

    int dpi = Ui::WindowDpi(dis->hwndItem);
    int lineY = (rc.top + rc.bottom) / 2;
    int lineX0 = rc.left + sz.cx + MulDiv(10, dpi, 96);
    int lineX1 = rc.right;

    if (withHint)
    {
        // Hint sits right next to the title (not at the window edge)
        const wchar_t* hint = L"drag a letter to scrub (Shift = x10)";
        SIZE hs = {};
        GetTextExtentPoint32W(dc, hint, (int)wcslen(hint), &hs);
        if (lineX0 + hs.cx + MulDiv(40, dpi, 96) < rc.right)
        {
            RECT hr = { lineX0, rc.top, lineX0 + hs.cx, rc.bottom };
            SetTextColor(dc, Ui::EyeOff());
            DrawTextW(dc, hint, -1, &hr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            lineX0 += hs.cx + MulDiv(10, dpi, 96);
        }
    }

    HPEN pen = CreatePen(PS_SOLID, 1, Ui::Sep());
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, lineX0, lineY, nullptr);
    LineTo(dc, lineX1, lineY);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    SelectObject(dc, oldFont);
}

void Properties::DrawScrubChip(const DRAWITEMSTRUCT* dis, bool active, bool disabled)
{
    if (!dis)
        return;
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;

    HBRUSH bgF = CreateSolidBrush(Ui::Bg());
    FillRect(dc, &rc, bgF);
    DeleteObject(bgF);

    int dpi = Ui::WindowDpi(dis->hwndItem);
    int R = MulDiv(4, dpi, 96); // corner radius
    if (R < 1)
        R = 1;
    int k = R * 55 / 100;
    // Left-rounded, straight-right path: the chip abuts its edit, forming
    // one mockup-style unit (rounded outer corners, straight seam).
    auto traceChip = [&](HDC c, int l, int t, int r, int b) {
        MoveToEx(c, r, t, nullptr);
        LineTo(c, l + R, t);
        POINT tlc[3] = { { l + R - k, t }, { l, t + R - k }, { l, t + R } };
        PolyBezierTo(c, tlc, 3);
        LineTo(c, l, b - R);
        POINT blc[3] = { { l, b - R + k }, { l + R - k, b }, { l + R, b } };
        PolyBezierTo(c, blc, 3);
        LineTo(c, r, b);
        LineTo(c, r, t);
    };
    HBRUSH bg = CreateSolidBrush(active ? Ui::Accent() : Ui::BgPanel());
    HGDIOBJ oldBr = SelectObject(dc, bg);
    BeginPath(dc);
    traceChip(dc, rc.left, rc.top, rc.right - 1, rc.bottom - 1);
    EndPath(dc);
    FillPath(dc);
    SelectObject(dc, oldBr);
    DeleteObject(bg);

    HPEN border = CreatePen(PS_SOLID, 1, active ? Ui::Accent() : Ui::Sep());
    HGDIOBJ oldPen = SelectObject(dc, border);
    // Stroke the traced outline
    {
        BeginPath(dc);
        traceChip(dc, rc.left, rc.top, rc.right - 1, rc.bottom - 1);
        EndPath(dc);
        StrokePath(dc);
    }
    SelectObject(dc, oldPen);
    DeleteObject(border);

    wchar_t text[8] = {};
    GetWindowTextW(dis->hwndItem, text, ARRAYSIZE(text));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, active ? Ui::Bg() : (disabled ? Ui::EyeOff() : Ui::Gray()));
    HGDIOBJ oldFont = SelectObject(dc, Ui::FontMono());
    DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

// Hotkey combo chip: same green look as the source-list hotkey chip
// (ChipBg fill, ChipBorder round rect, Accent mono text).
void Properties::DrawHotkeyChip(const DRAWITEMSTRUCT* dis)
{
    if (!dis)
        return;
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;

    int dpi = Ui::WindowDpi(dis->hwndItem);
    int rad = MulDiv(4, dpi, 96);
    if (rad < 1)
        rad = 1;

    HBRUSH bg = CreateSolidBrush(Ui::ChipBg());
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    HPEN border = CreatePen(PS_SOLID, 1, Ui::ChipBorder());
    HGDIOBJ oldPen = SelectObject(dc, border);
    HGDIOBJ oldBr = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, rc.left, rc.top, rc.right - 1, rc.bottom - 1, rad, rad);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBr);
    DeleteObject(border);

    wchar_t text[64] = {};
    GetWindowTextW(dis->hwndItem, text, ARRAYSIZE(text));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, Ui::Accent());
    HGDIOBJ oldFont = SelectObject(dc, Ui::FontMono());
    DrawTextW(dc, text, -1, &rc,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
}

// ============================================================================
// Geometry scrub: drag a letter chip sideways to change the value.
// Plain drag = x1, Shift = x10. Values commit live through ReadField.
// ============================================================================
void Properties::ScrubBegin(int i, int x)
{
    if (m_updating || !m_app || i < 0 || i > 5)
        return;
    int sel = m_app->Selection();
    if (sel < 0 || sel >= m_app->SourceCount())
        return;
    // Frozen with the canvas lock (the chips are disabled too; belt first)
    if (m_app->SourceAt(sel).locked)
        return;
    const SourceSettings& s = m_app->SourceAt(sel);
    switch (i)
    {
    case 0: m_scrubStartVal = s.x; break;
    case 1: m_scrubStartVal = s.y; break;
    case 2: m_scrubStartVal = s.width; break;
    case 3: m_scrubStartVal = s.height; break;
    case 4: m_scrubStartVal = s.renderWidth; break;
    default: m_scrubStartVal = s.renderHeight; break;
    }
    m_scrubIdx = i;
    m_scrubStartX = x;
    if (m_geoLab[i])
        InvalidateRect(m_geoLab[i], nullptr, FALSE);
}

void Properties::ScrubMove(int i, int x)
{
    if (m_scrubIdx != i || !m_app || i < 0 || i > 5)
        return;
    int sel = m_app->Selection();
    if (sel < 0 || sel >= m_app->SourceCount())
        return;

    int step = (GetKeyState(VK_SHIFT) & 0x8000) ? 10 : 1;
    int v = m_scrubStartVal + (x - m_scrubStartX) * step;

    const int sw = m_app->SceneWidth(), sh = m_app->SceneHeight();
    int lo = 16, hi = 7680;
    switch (i)
    {
    case 0: lo = -sw; hi = sw; break;
    case 1: lo = -sh; hi = sh; break;
    case 2: lo = 16; hi = sw * 2; break;
    case 3: lo = 16; hi = sh * 2; break;
    case 4: lo = 16; hi = 7680; break;
    default: lo = 16; hi = 4320; break;
    }
    if (v < lo) v = lo;
    if (v > hi) v = hi;

    HWND e = GetDlgItem(m_hwnd, IdX + i);
    wchar_t cur[32] = {};
    GetWindowTextW(e, cur, ARRAYSIZE(cur));
    if (_wtoi(cur) != v)
    {
        wchar_t b[32] = {};
        _itow_s(v, b, 10);
        SetWindowTextW(e, b);
        ReadField(IdX + i); // live commit: applies to the source + recomposites
    }
}

void Properties::ScrubEnd(int i)
{
    if (m_scrubIdx == i)
    {
        m_scrubIdx = -1;
        if (i >= 0 && i <= 5 && m_geoLab[i])
            InvalidateRect(m_geoLab[i], nullptr, FALSE);
    }
}

LRESULT CALLBACK Properties::ScrubLabelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR)
{
    auto self = reinterpret_cast<Properties*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    int idx = GetDlgCtrlID(hwnd) - IdLabX;
    switch (msg)
    {
    case WM_NCHITTEST:
        return HTCLIENT; // statics default to HTTRANSPARENT; we need the mouse
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        if (self)
            self->ScrubBegin(idx, GET_X_LPARAM(lParam));
        SetCapture(hwnd);
        return 0;
    case WM_MOUSEMOVE:
        if (self && GetCapture() == hwnd)
            self->ScrubMove(idx, GET_X_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        if (self)
            self->ScrubEnd(idx);
        if (GetCapture() == hwnd)
            ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        if (self)
            self->ScrubEnd(idx);
        return 0;
    }
    if (!self || !self->m_geoOrigProc)
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    return CallWindowProcW(self->m_geoOrigProc, hwnd, msg, wParam, lParam);
}

void Properties::Refresh()
{
    if (!m_hwnd)
        return;
    m_updating = true;

    int sel = m_app->Selection();
    bool has = sel >= 0 && sel < m_app->SourceCount();
    const SourceSettings* s = has ? &m_app->SourceAt(sel) : nullptr;

    auto setText = [&](int id, const std::wstring& v) {
        HWND c = GetDlgItem(m_hwnd, id);
        // Never rewrite the edit being typed in: SetWindowText resets the
        // caret (to the start), so each keystroke would yank it back.
        // The field commits on Enter/focus loss and refreshes then.
        if (c && GetFocus() == c)
            return;
        SetWindowTextW(c, v.c_str());
    };
    setText(IdName, s ? s->name : L"");
    setText(IdUrl, s ? s->url : L"");
    setText(IdX, s ? std::to_wstring(s->x) : L"");
    setText(IdY, s ? std::to_wstring(s->y) : L"");
    setText(IdW, s ? std::to_wstring(s->width) : L"");
    setText(IdH, s ? std::to_wstring(s->height) : L"");
    setText(IdRw, s ? std::to_wstring(s->renderWidth) : L"");
    setText(IdRh, s ? std::to_wstring(s->renderHeight) : L"");

    HWND visBox = GetDlgItem(m_hwnd, IdVisible);
    HWND muteBox = GetDlgItem(m_hwnd, IdMuted);
    m_visChecked = s && s->visible;
    m_muteChecked = s && s->muted;
    // Owner-drawn: repaint explicitly (see member comment)
    if (visBox)
        InvalidateRect(visBox, nullptr, TRUE);
    if (muteBox)
        InvalidateRect(muteBox, nullptr, TRUE);

    std::wstring hk = L"none";
    if (s && !s->hotkey.empty() && s->hotkey != "none")
        hk = Ui::FromUtf8(s->hotkey);
    setText(IdHotkeyValue, hk);

    wchar_t pct[16];
    swprintf_s(pct, L"%d%%", s ? s->opacity : 100);
    setText(IdOpacityVal, pct);
    swprintf_s(pct, L"%d%%", s ? s->volume : 100);
    setText(IdVolumeVal, pct);
    if (s)
    {
        m_opacity.SetValue(s->opacity);
        m_volume.SetValue(s->volume);
    }

    for (int id = IdName; id <= IdUrl; id++)
        EnableWindow(GetDlgItem(m_hwnd, id), has ? TRUE : FALSE);
    // Locked sources are fully frozen: geometry edits + scrub chips join
    // the canvas lock (positions can neither drag nor type).
    bool geo = has && !(s && s->locked);
    for (int id = IdX; id <= IdRh; id++)
        EnableWindow(GetDlgItem(m_hwnd, id), geo ? TRUE : FALSE);
    EnableWindow(GetDlgItem(m_hwnd, IdVisible), has ? TRUE : FALSE);
    EnableWindow(GetDlgItem(m_hwnd, IdMuted), has ? TRUE : FALSE);
    EnableWindow(GetDlgItem(m_hwnd, IdHotkeySet), has ? TRUE : FALSE);
    EnableWindow(GetDlgItem(m_hwnd, IdHotkeyClear), has ? TRUE : FALSE);
    EnableWindow(GetDlgItem(m_hwnd, IdCss), has ? TRUE : FALSE);
    for (int i = 0; i < 6; i++)
        if (m_geoLab[i])
            EnableWindow(m_geoLab[i], geo ? TRUE : FALSE);
    EnableWindow(m_opacity.Window(), has ? TRUE : FALSE);
    EnableWindow(m_volume.Window(), has ? TRUE : FALSE);

    m_updating = false;
}

void Properties::ReadField(int id)
{
    int sel = m_app->Selection();
    if (sel < 0 || sel >= m_app->SourceCount())
        return;

    wchar_t buf[4096] = {};
    GetWindowTextW(GetDlgItem(m_hwnd, id), buf, ARRAYSIZE(buf));
    SceneFile& model = m_app->Model();
    SourceSettings& s = model.sources[(size_t)sel];

    switch (id)
    {
    case IdName:
        s.name = Ui::TrimW(buf);
        if (s.name.empty()) s.name = L"Source";
        m_app->PushSourceChange(sel, SourceChange::Name);
        break;
    case IdUrl:
        s.url = Ui::TrimW(buf);
        m_app->PushSourceChange(sel, SourceChange::Url);
        break;
    case IdX: case IdY: case IdW: case IdH:
    {
        int v = _wtoi(buf);
        if (id == IdX) s.x = v;
        else if (id == IdY) s.y = v;
        else if (id == IdW) s.width = (std::max)(16, v);
        else s.height = (std::max)(16, v);
        m_app->PushSourceChange(sel, SourceChange::Rect);
        break;
    }
    case IdRw: case IdRh:
    {
        int v = (std::max)(16, _wtoi(buf));
        if (id == IdRw) s.renderWidth = v;
        else s.renderHeight = v;
        m_app->PushSourceChange(sel, SourceChange::RenderSize);
        break;
    }
    }
}

LRESULT CALLBACK Properties::FieldSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR ref)
{
    auto self = reinterpret_cast<Properties*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_PAINT && self)
    {
        // Paint the edit, then the inner input ring on top (geometry
        // edits keep a straight left seam against their letter chip).
        WNDPROC orig = self->m_fieldOrigProc;
        LRESULT r = orig ? CallWindowProcW(orig, hwnd, msg, wParam, lParam)
                         : DefWindowProcW(hwnd, msg, wParam, lParam);
        int id = GetDlgCtrlID(hwnd);
        Ui::PaintEditRing(hwnd, id >= IdX && id <= IdRh);
        return r;
    }
    if ((msg == WM_SETFOCUS || msg == WM_KILLFOCUS) && self)
    {
        WNDPROC orig = self->m_fieldOrigProc;
        LRESULT r = orig ? CallWindowProcW(orig, hwnd, msg, wParam, lParam)
                         : DefWindowProcW(hwnd, msg, wParam, lParam);
        InvalidateRect(hwnd, nullptr, FALSE); // ring follows the caret
        return r;
    }
    if (msg == WM_KEYDOWN && wParam == VK_RETURN)
    {
        if (self && !self->m_updating)
            self->ReadField(GetDlgCtrlID(hwnd));
        SetFocus(GetParent(hwnd)); // commit + move focus away
        return 0;
    }
    if (msg == WM_KEYDOWN && wParam == VK_TAB && self)
    {
        bool back = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        Ui::AdvanceFocus(GetParent(hwnd), hwnd, back);
        return 0;
    }
    if (!self || !self->m_fieldOrigProc)
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    return CallWindowProcW(self->m_fieldOrigProc, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK Properties::Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Properties* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Properties*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<Properties*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_SIZE:
        self->Layout();
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    {
        HDC dc = (HDC)wParam;
        HWND ctl = (HWND)lParam;
        wchar_t cls[16] = {};
        GetClassNameW(ctl, cls, 16);
        if (wcscmp(cls, L"Edit") == 0)
        {
            // Edits MUST erase opaquely: TRANSPARENT background mode leaves
            // old glyph pixels behind when multiline text scrolls/repaints
            // (the typing trails). The brush + BkColor stay BgEdit.
            SetBkMode(dc, OPAQUE);
            // URL field matches the mockup's soft-blue link tone
            SetTextColor(dc, ctl == GetDlgItem(hwnd, IdUrl)
                ? RGB(0x9F, 0xC4, 0xFF) : Ui::Fg());
            SetBkColor(dc, Ui::BgEdit());
            return (LRESULT)Ui::DarkEditBrush();
        }
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, ctl == GetDlgItem(hwnd, IdHotkeyValue) ? Ui::Fg() : Ui::Gray());
        SetBkColor(dc, Ui::Bg());
        return (LRESULT)Ui::DarkBgBrush();
    }
    case WM_DRAWITEM:
    {
        auto dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis && dis->CtlType == ODT_STATIC)
        {
            if (dis->CtlID >= (UINT)IdSecSource && dis->CtlID <= (UINT)IdSecOutput)
            {
                DrawSectionHeader(dis, dis->CtlID == (UINT)IdSecGeometry);
                return TRUE;
            }
            if (dis->CtlID >= (UINT)IdLabX && dis->CtlID < (UINT)(IdLabX + 6))
            {
                DrawScrubChip(dis,
                    self->m_scrubIdx == (int)(dis->CtlID - IdLabX),
                    (dis->itemState & ODS_DISABLED) != 0);
                return TRUE;
            }
            if (dis->CtlID == (UINT)IdHotkeyValue)
            {
                DrawHotkeyChip(dis);
                return TRUE;
            }
        }
        if (dis && (dis->CtlID == (UINT)IdVisible || dis->CtlID == (UINT)IdMuted))
        {
            bool on = dis->CtlID == (UINT)IdVisible
                ? self->m_visChecked : self->m_muteChecked;
            if (Ui::DrawDarkCheckBox(dis, on))
                return TRUE;
        }
        else if (dis && Ui::DrawDarkButton(dis))
            return TRUE;
        break;
    }
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (self->m_updating)
            break;
        if (id == IdName && code == EN_CHANGE)
        {
            // Name is harmless live; everything else commits on Enter/focus loss
            self->ReadField(id);
        }
        else if (id >= IdUrl && id <= IdRh && code == EN_KILLFOCUS)
        {
            self->ReadField(id);
        }
        else if (id >= IdX && id <= IdRh && code == EN_SETFOCUS)
        {
            // Select all for quick retype of numbers
            SendMessageW((HWND)lParam, EM_SETSEL, 0, -1);
        }
        else if (id >= IdName && id <= IdRh
            && (code == EN_SETFOCUS || code == EN_KILLFOCUS))
        {
            // Focus ring around the edit follows the caret
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        else if ((id == IdOpacity || id == IdVolume) && code == Ui::SLN_CHANGED)
        {
            int sel = self->m_app->Selection();
            if (sel >= 0)
            {
                if (id == IdOpacity)
                    self->m_app->SetOpacity(sel, self->m_opacity.Value());
                else
                    self->m_app->SetVolume(sel, self->m_volume.Value());
            }
        }
        else if (id == IdVisible && code == BN_CLICKED)
        {
            int sel = self->m_app->Selection();
            if (sel >= 0)
            {
                // Owner-drawn: the parent owns the check state (see member
                // comment); flip it, repaint, then persist through the model.
                bool on = !self->m_visChecked;
                self->m_visChecked = on;
                HWND box = GetDlgItem(self->m_hwnd, IdVisible);
                InvalidateRect(box, nullptr, TRUE);
                // Synchronous repaint: with live sources flooding the queue
                // the async paint can lag behind the click.
                UpdateWindow(box);
                self->m_app->Model().sources[(size_t)sel].visible = on;
                self->m_app->PushSourceChange(sel, SourceChange::Visibility);
            }
        }
        else if (id == IdMuted && code == BN_CLICKED)
        {
            int sel = self->m_app->Selection();
            if (sel >= 0)
            {
                bool on = !self->m_muteChecked;
                self->m_muteChecked = on;
                HWND box = GetDlgItem(self->m_hwnd, IdMuted);
                InvalidateRect(box, nullptr, TRUE);
                UpdateWindow(box);
                self->m_app->Model().sources[(size_t)sel].muted = on;
                self->m_app->PushSourceChange(sel, SourceChange::Mute);
            }
        }
        else if (id == IdHotkeySet)
            self->m_app->SetHotkeyForSelected();
        else if (id == IdHotkeyClear)
            self->m_app->ClearHotkeyForSelected();
        else if (id == IdCss)
            self->m_app->EditCssForSelected();
        break;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

