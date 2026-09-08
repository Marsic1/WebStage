// ============================================================================
// WebStage - Shared UI Helpers Implementation
// ============================================================================

#include "Ui.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <cmath>
#include <map>
#include <stdarg.h>
#include <uxtheme.h>
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace Ui
{

// ============================================================================
// Fonts
// ============================================================================
static HFONT g_font = nullptr;
static HFONT g_fontBold = nullptr;
static UINT g_activeDpi = 0; // SetActiveDpi; 0 = resolve from the system

static UINT ResolveDpi()
{
    if (g_activeDpi)
        return g_activeDpi;
    static UINT(WINAPI * pGetDpiForSystem)() = nullptr;
    static bool resolved = false;
    if (!resolved)
    {
        resolved = true;
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32)
            pGetDpiForSystem = (UINT(WINAPI*)())GetProcAddress(user32, "GetDpiForSystem");
    }
    if (pGetDpiForSystem)
    {
        UINT dpi = pGetDpiForSystem();
        if (dpi)
            return dpi;
    }
    return 96;
}

void SetActiveDpi(UINT dpi)
{
    if (dpi)
        g_activeDpi = dpi;
}

static HFONT MakeFont(bool bold)
{
    // Scale the message font to the active dpi (NOT GetDC(LOGPIXELSX):
    // that always reports 96 on modern Windows, shrinking every font).
    int px = -MulDiv(9, (int)ResolveDpi(), 72);
    HFONT f = CreateFontW(px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    return f ? f : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

HFONT Font()
{
    if (!g_font)
        g_font = MakeFont(false);
    return g_font;
}

HFONT FontBold()
{
    if (!g_fontBold)
        g_fontBold = MakeFont(true);
    return g_fontBold;
}

static HFONT g_fontMono = nullptr;

HFONT FontMono()
{
    if (!g_fontMono)
    {
        int px = -MulDiv(9, (int)ResolveDpi(), 72);
        g_fontMono = CreateFontW(px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Cascadia Mono");
        if (!g_fontMono)
            g_fontMono = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    return g_fontMono;
}

void ResetFonts()
{
    if (g_font) { DeleteObject(g_font); g_font = nullptr; }
    if (g_fontBold) { DeleteObject(g_fontBold); g_fontBold = nullptr; }
    if (g_fontMono) { DeleteObject(g_fontMono); g_fontMono = nullptr; }
}

UINT WindowDpi(HWND hwnd)
{
    if (hwnd)
    {
        static UINT(WINAPI * pGetDpiForWindow)(HWND) = nullptr;
        static bool resolved = false;
        if (!resolved)
        {
            resolved = true;
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            if (user32)
                pGetDpiForWindow = (UINT(WINAPI*)(HWND))GetProcAddress(user32, "GetDpiForWindow");
        }
        if (pGetDpiForWindow)
        {
            UINT dpi = pGetDpiForWindow(hwnd);
            if (dpi) return dpi;
        }
    }
    HDC dc = GetDC(nullptr);
    UINT dpi = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return dpi ? dpi : 96;
}

int Scale(HWND hwnd, int v)
{
    return MulDiv(v, (int)WindowDpi(hwnd), 96);
}

void AdvanceFocus(HWND parent, HWND from, bool back)
{
    if (!parent || !from)
        return;
    HWND next = GetNextDlgTabItem(parent, from, back ? TRUE : FALSE);
    if (next)
        SetFocus(next);
}

static LRESULT CALLBACK TabSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR)
{
    if (msg == WM_KEYDOWN && wParam == VK_TAB)
    {
        bool back = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        AdvanceFocus(GetParent(hwnd), hwnd, back);
        return 0;
    }
    LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
    if (msg == WM_PAINT)
    {
        // Dialog inputs get the same inner ring (buttons excluded).
        wchar_t cls[16] = {};
        GetClassNameW(hwnd, cls, 16);
        if (wcscmp(cls, L"EDIT") == 0)
            PaintEditRing(hwnd, false);
    }
    return r;
}

void MakeTabbable(HWND ctl)
{
    if (!ctl)
        return;
    // Comctl subclassing is refcount-safe and needs no per-control storage
    SetWindowSubclass(ctl, TabSubclassProc, (UINT_PTR)TabSubclassProc, 0);
}

void CenterEditVertically(HWND edit)
{
    if (!edit)
        return;
    RECT rc = {};
    GetClientRect(edit, &rc);
    int h = rc.bottom - rc.top;
    if (h <= 0)
        return;
    HFONT f = (HFONT)SendMessageW(edit, WM_GETFONT, 0, 0);
    HDC dc = GetDC(edit);
    if (!dc)
        return;
    HGDIOBJ old = f ? SelectObject(dc, f) : nullptr;
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    if (old)
        SelectObject(dc, old);
    ReleaseDC(edit, dc);
    int dpi = (int)WindowDpi(edit);
    int top = (h - tm.tmHeight) / 2;
    if (top < 0)
        top = 0;
    int side = MulDiv(8, dpi, 96); // clear the inner ring + mockup padding
    RECT fmt = { side, top, rc.right - side, top + tm.tmHeight };
    if (fmt.right < fmt.left)
        fmt.right = fmt.left;
    SendMessageW(edit, EM_SETRECT, 0, (LPARAM)&fmt);
}

void PaintEditRing(HWND edit, bool squareLeft)
{
    if (!edit)
        return;
    RECT rc = {};
    GetClientRect(edit, &rc);
    int iw = rc.right - rc.left;
    int ih = rc.bottom - rc.top;
    if (iw <= 6 || ih <= 6)
        return;
    HDC dc = GetDC(edit);
    if (!dc)
        return;
    int dpi = (int)WindowDpi(edit);
    int R = MulDiv(4, dpi, 96); // corner radius
    if (R < 1)
        R = 1;
    HPEN pen = CreatePen(PS_SOLID, 1,
        GetFocus() == edit ? Accent() : Sep());
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBr = SelectObject(dc, GetStockObject(NULL_BRUSH));
    // 1px inset so the ring never touches the client edge
    int l = 1, t = 1, r = iw - 1, b = ih - 1;
    if (!squareLeft)
    {
        RoundRect(dc, l, t, r, b, R * 2, R * 2);
    }
    else
    {
        // Straight left seam (abuts the letter chip), rounded right.
        int k = R * 55 / 100;
        MoveToEx(dc, l, b, nullptr);
        LineTo(dc, l, t);
        LineTo(dc, r - R, t);
        POINT trc[3] = { { r - R + k, t }, { r, t + R - k }, { r, t + R } };
        PolyBezierTo(dc, trc, 3);
        LineTo(dc, r, b - R);
        POINT brc[3] = { { r, b - R + k }, { r - R + k, b }, { r - R, b } };
        PolyBezierTo(dc, brc, 3);
        LineTo(dc, l, b);
    }
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBr);
    DeleteObject(pen);
    ReleaseDC(edit, dc);
}

// ============================================================================
// Frame chrome
// ============================================================================
void EnableDarkFrame(HWND hwnd)
{
    BOOL dark = TRUE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (19 on older builds)
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    // Title bar in the app theme (Win11 22000+; ignored downlevel).
    // DWMWA_CAPTION_COLOR = 35, DWMWA_TEXT_COLOR = 36.
    COLORREF caption = Bg();
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    COLORREF captionText = Fg();
    DwmSetWindowAttribute(hwnd, 36, &captionText, sizeof(captionText));
    SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
}

void ForceDarkAppMode()
{
    static bool done = false;
    if (done) return;
    done = true;
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    if (!ux) ux = LoadLibraryW(L"uxtheme.dll");
    if (ux)
    {
        typedef HRESULT(WINAPI * PFN_SetPreferredAppMode)(int);
        auto p = (PFN_SetPreferredAppMode)GetProcAddress(ux, (LPCSTR)135);
        if (p)
            p(2); // ForceDark
    }
}

// ============================================================================
// String conversions
// ============================================================================
std::string ToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w((size_t)n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::wstring TrimW(const std::wstring& s)
{
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ============================================================================
// Diagnostics log
// ============================================================================
static bool g_verbose = false;

void SetVerbose(bool on)
{
    g_verbose = on;
}

bool IsVerbose()
{
    return g_verbose;
}

void Log(const wchar_t* fmt, ...)
{
    static CRITICAL_SECTION cs;
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once, [](PINIT_ONCE, PVOID, PVOID*) -> BOOL {
        InitializeCriticalSection(&cs);
        return TRUE;
    }, nullptr, nullptr);
    EnterCriticalSection(&cs);

    wchar_t path[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, path))
    {
        std::wstring file = std::wstring(path) + L"WebStage.log";

        wchar_t msg[1024] = {};
        va_list ap;
        va_start(ap, fmt);
        _vsnwprintf_s(msg, _TRUNCATE, fmt, ap);
        va_end(ap);

        SYSTEMTIME st = {};
        GetLocalTime(&st);
        wchar_t line[1200] = {};
        swprintf_s(line, L"[%02d:%02d:%02d.%03d] %s\r\n",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);

        HANDLE h = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            std::string utf8 = ToUtf8(line);
            WriteFile(h, utf8.c_str(), (DWORD)utf8.size(), &written, nullptr);
            CloseHandle(h);
        }
    }

    LeaveCriticalSection(&cs);
}

// ============================================================================
// Dark brushes + owner-drawn buttons
// ============================================================================
HBRUSH DarkBgBrush()
{
    static HBRUSH b = nullptr;
    if (!b) b = CreateSolidBrush(Bg());
    return b;
}

HBRUSH DarkEditBrush()
{
    static HBRUSH b = nullptr;
    if (!b) b = CreateSolidBrush(BgEdit());
    return b;
}

void SetButtonIcon(HWND button, int icon, bool iconOnly)
{
    if (button)
        SetWindowLongPtrW(button, GWLP_USERDATA,
            iconOnly ? (icon | 0x100) : icon);
}

// Mockup icon set (V2 Direction A, feather-style 24-unit strokes) mapped
// onto the button box: stroke 1.7 units, round caps/joins, currentColor.
// bg = the button face behind the icon (reserved for negative details).
static void DrawBtnIcon(HDC dc, int icon, RECT box, COLORREF c, COLORREF bg)
{
    if (icon <= IconNone)
        return;
    (void)bg;
    int cx = (box.left + box.right) / 2;
    int cy = (box.top + box.bottom) / 2;
    double u = (box.right - box.left) / 24.0; // 24-unit design space
    if (u <= 0.0)
        u = 1.0;
    auto PX = [&](double v) { return cx + (int)std::lround((v - 12.0) * u); };
    auto PY = [&](double v) { return cy + (int)std::lround((v - 12.0) * u); };
    LOGBRUSH lb = {};
    lb.lbStyle = BS_SOLID;
    lb.lbColor = c;
    HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
        (std::max)(1, (int)std::lround(1.7 * u)), &lb, 0, nullptr);
    HGDIOBJ oldPen = SelectObject(dc, pen ? pen : (HPEN)GetStockObject(DC_PEN));
    HGDIOBJ oldBr = SelectObject(dc, GetStockObject(NULL_BRUSH));
    switch (icon)
    {
    case IconPlus: // M12 5v14M5 12h14
        MoveToEx(dc, PX(12), PY(5), nullptr); LineTo(dc, PX(12), PY(19));
        MoveToEx(dc, PX(5), PY(12), nullptr); LineTo(dc, PX(19), PY(12));
        break;
    case IconTrash:
    {
        // M4 7h16 / handle M9.5 7V5.5 + corner cubics + M11 4h2 /
        // tapered body M6.5 7l.9 12 ... l.9-12 / slats M10 11v6 M14 11v6.
        // The 1.5-unit handle arcs are sub-pixel here: cubics keep the
        // bend, round joins close the corners.
        MoveToEx(dc, PX(4), PY(7), nullptr); LineTo(dc, PX(20), PY(7));
        MoveToEx(dc, PX(9.5), PY(7), nullptr); LineTo(dc, PX(9.5), PY(5.5));
        POINT ha[3] = { { PX(9.5), PY(4.672) }, { PX(10.172), PY(4) }, { PX(11), PY(4) } };
        PolyBezierTo(dc, ha, 3);
        MoveToEx(dc, PX(11), PY(4), nullptr); LineTo(dc, PX(13), PY(4));
        MoveToEx(dc, PX(14.5), PY(5.5), nullptr);
        POINT hb[3] = { { PX(14.5), PY(4.672) }, { PX(13.828), PY(4) }, { PX(13), PY(4) } };
        PolyBezierTo(dc, hb, 3);
        MoveToEx(dc, PX(14.5), PY(7), nullptr); LineTo(dc, PX(14.5), PY(5.5));
        MoveToEx(dc, PX(6.5), PY(7), nullptr); LineTo(dc, PX(7.4), PY(19));
        LineTo(dc, PX(16.6), PY(19)); LineTo(dc, PX(17.5), PY(7));
        MoveToEx(dc, PX(10), PY(11), nullptr); LineTo(dc, PX(10), PY(17));
        MoveToEx(dc, PX(14), PY(11), nullptr); LineTo(dc, PX(14), PY(17));
        break;
    }
    case IconUp: // M6 14.5 12 8.5l6 6
        MoveToEx(dc, PX(6), PY(14.5), nullptr);
        LineTo(dc, PX(12), PY(8.5)); LineTo(dc, PX(18), PY(14.5));
        break;
    case IconDown: // M6 9.5 12 15.5l6-6
        MoveToEx(dc, PX(6), PY(9.5), nullptr);
        LineTo(dc, PX(12), PY(15.5)); LineTo(dc, PX(18), PY(9.5));
        break;
    case IconSliders:
    {
        // M4 7.5h9 M17.5 7.5H20 M4 16.5h3 M11.5 16.5H20 + knobs
        // (15,7.5,r2.2) (9,16.5,r2.2), stroked like the mockup.
        MoveToEx(dc, PX(4), PY(7.5), nullptr); LineTo(dc, PX(13), PY(7.5));
        MoveToEx(dc, PX(17.5), PY(7.5), nullptr); LineTo(dc, PX(20), PY(7.5));
        MoveToEx(dc, PX(4), PY(16.5), nullptr); LineTo(dc, PX(7), PY(16.5));
        MoveToEx(dc, PX(11.5), PY(16.5), nullptr); LineTo(dc, PX(20), PY(16.5));
        Ellipse(dc, PX(12.8), PY(5.3), PX(17.2), PY(9.7));
        Ellipse(dc, PX(6.8), PY(14.3), PX(11.2), PY(18.7));
        break;
    }
    case IconCode: // M9 8.5 5 12l4 3.5M15 8.5 19 12l-4 3.5
        MoveToEx(dc, PX(9), PY(8.5), nullptr);
        LineTo(dc, PX(5), PY(12)); LineTo(dc, PX(9), PY(15.5));
        MoveToEx(dc, PX(15), PY(8.5), nullptr);
        LineTo(dc, PX(19), PY(12)); LineTo(dc, PX(15), PY(15.5));
        break;
    default:
        break;
    }
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

bool DrawDarkButton(const DRAWITEMSTRUCT* dis)
{
    if (!dis || dis->CtlType != ODT_BUTTON)
        return false;

    wchar_t text[256] = {};
    GetWindowTextW(dis->hwndItem, text, ARRAYSIZE(text));

    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool focused = (dis->itemState & ODS_FOCUS) != 0;
    bool isDefault = (dis->itemState & ODS_DEFAULT) != 0;

    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    int rad = 12;

    // Fill the full item rect first: RoundRect leaves the corners
    // unpainted, which otherwise shows the control's default (white)
    // background as an ugly square border.
    {
        HBRUSH full = CreateSolidBrush(Bg());
        FillRect(dc, &rc, full);
        DeleteObject(full);
    }

    int dpi = (int)WindowDpi(dis->hwndItem);
    rad = MulDiv(8, dpi, 96);

    HBRUSH bg = CreateSolidBrush(disabled ? Bg() : (pressed ? Hover() : BgEdit()));
    HPEN border = CreatePen(PS_SOLID, 1, (focused || isDefault) ? Accent() : Sep());
    HGDIOBJ oldBr = SelectObject(dc, bg);
    HGDIOBJ oldPen = SelectObject(dc, border);
    RoundRect(dc, rc.left, rc.top, rc.right - 1, rc.bottom - 1, rad, rad);
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(bg);
    DeleteObject(border);

    // Optional icon (attached via Ui::SetButtonIcon; bit 0x100 = icon-only)
    int icon = (int)GetWindowLongPtrW(dis->hwndItem, GWLP_USERDATA);
    int ic = icon & 0xFF;
    bool iconOnly = (icon & 0x100) != 0;
    int isz = MulDiv(15, dpi, 96);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? Gray() : Fg());
    HGDIOBJ oldFont = SelectObject(dc, Font());
    RECT tr = rc;
    if (ic && iconOnly)
    {
        RECT box = { rc.left + (rc.right - rc.left - isz) / 2,
                     rc.top + (rc.bottom - rc.top - isz) / 2, 0, 0 };
        box.right = box.left + isz;
        box.bottom = box.top + isz;
        DrawBtnIcon(dc, ic, box, disabled ? EyeOff() : Gray(),
            disabled ? Bg() : (pressed ? Hover() : BgEdit()));
    }
    else if (ic)
    {
        RECT box = { rc.left + MulDiv(9, dpi, 96),
                     rc.top + (rc.bottom - rc.top - isz) / 2, 0, 0 };
        box.right = box.left + isz;
        box.bottom = box.top + isz;
        DrawBtnIcon(dc, ic, box, disabled ? EyeOff() : Gray(),
            disabled ? Bg() : (pressed ? Hover() : BgEdit()));
        tr.left = box.right + MulDiv(6, dpi, 96);
        DrawTextW(dc, text, -1, &tr,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    else
    {
        tr.left += 8;
        tr.right -= 8;
        DrawTextW(dc, text, -1, &tr,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    SelectObject(dc, oldFont);
    return true;
}

bool DrawDarkCheckBox(const DRAWITEMSTRUCT* dis, bool checked)
{
    if (!dis || dis->CtlType != ODT_BUTTON)
        return false;

    wchar_t text[256] = {};
    GetWindowTextW(dis->hwndItem, text, ARRAYSIZE(text));

    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool focused = (dis->itemState & ODS_FOCUS) != 0;

    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;

    // Full-rect fill so no default white shows around the content.
    HBRUSH full = CreateSolidBrush(Bg());
    FillRect(dc, &rc, full);
    DeleteObject(full);

    int dpi = (int)WindowDpi(dis->hwndItem);
    int cy = (rc.top + rc.bottom) / 2;
    int pillW = MulDiv(30, dpi, 96);
    int pillH = MulDiv(14, dpi, 96);
    RECT pill = { rc.left, cy - pillH / 2, rc.left + pillW, cy + pillH / 2 };

    HBRUSH bg = CreateSolidBrush(checked ? RGB(0x16, 0x25, 0x21)
                                         : (pressed ? Hover() : BgEdit()));
    // Green is reserved for the ON state: keyboard focus gets a neutral
    // lighter border instead, so an OFF toggle never looks half-active.
    HPEN border = CreatePen(PS_SOLID, 1,
        checked ? RGB(0x2A, 0x9A, 0x5C)
                : (focused ? RGB(0x4A, 0x55, 0x59)
                           : (pressed ? Hover() : Sep())));
    HGDIOBJ oldBr = SelectObject(dc, bg);
    HGDIOBJ oldPen = SelectObject(dc, border);
    int rad = pillH / 2;
    RoundRect(dc, pill.left, pill.top, pill.right, pill.bottom, rad, rad);

    // Knob
    int knobD = pillH - MulDiv(6, dpi, 96);
    int kx = checked ? pill.right - MulDiv(4, dpi, 96) - knobD
                     : pill.left + MulDiv(4, dpi, 96);
    HBRUSH knob = CreateSolidBrush(checked ? Accent() : Gray());
    HGDIOBJ oldKnob = SelectObject(dc, knob);
    Ellipse(dc, kx, cy - knobD / 2, kx + knobD, cy + knobD / 2);
    SelectObject(dc, oldKnob);
    DeleteObject(knob);
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(bg);
    DeleteObject(border);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? Gray() : Fg());
    HGDIOBJ oldFont = SelectObject(dc, Font());
    RECT tr = rc;
    tr.left = pill.right + MulDiv(8, dpi, 96);
    DrawTextW(dc, text, -1, &tr,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
    return true;
}

bool DrawSendToggle(const DRAWITEMSTRUCT* dis, bool sending)
{
    if (!dis || dis->CtlType != ODT_BUTTON)
        return false;

    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool focused = (dis->itemState & ODS_FOCUS) != 0;

    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;

    // Same as DrawDarkButton: paint the full rect so rounded corners
    // never expose the control's default white background.
    {
        HBRUSH full = CreateSolidBrush(Bg());
        FillRect(dc, &rc, full);
        DeleteObject(full);
    }

    int dpi = (int)WindowDpi(dis->hwndItem);
    // Pill shape (fully rounded ends, like the mockup send control)
    int rad = (rc.bottom - rc.top);

    COLORREF stateCol = sending ? Accent() : Stop();
    HBRUSH bg = CreateSolidBrush(pressed ? Hover()
        : (sending ? RGB(0x16, 0x25, 0x21) : BgEdit()));
    HPEN border = CreatePen(PS_SOLID, 1,
        sending ? RGB(0x2A, 0x9A, 0x5C) : stateCol);
    HGDIOBJ oldBr = SelectObject(dc, bg);
    HGDIOBJ oldPen = SelectObject(dc, border);
    RoundRect(dc, rc.left, rc.top, rc.right - 1, rc.bottom - 1, rad, rad);
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(bg);
    DeleteObject(border);

    // LED dot
    int cy = (rc.top + rc.bottom) / 2;
    int ledR = MulDiv(4, dpi, 96);
    int ledX = rc.left + MulDiv(13, dpi, 96);
    HBRUSH led = CreateSolidBrush(stateCol);
    HPEN ledPen = CreatePen(PS_SOLID, 1, stateCol);
    HGDIOBJ oldLed = SelectObject(dc, led);
    HGDIOBJ oldLedPen = SelectObject(dc, ledPen);
    Ellipse(dc, ledX - ledR, cy - ledR, ledX + ledR, cy + ledR);
    SelectObject(dc, oldLed);
    SelectObject(dc, oldLedPen);
    DeleteObject(led);
    DeleteObject(ledPen);
    if (focused)
    {
        // subtle focus ring inside, in the live-state color (a green ring
        // lingering around a red STOPPED pill reads as still-active)
        HPEN fp = CreatePen(PS_SOLID, 1, stateCol);
        HGDIOBJ oldFp = SelectObject(dc, fp);
        HGDIOBJ oldNull = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, rc.left + 2, rc.top + 2, rc.right - 3, rc.bottom - 3, rad, rad);
        SelectObject(dc, oldFp);
        SelectObject(dc, oldNull);
        DeleteObject(fp);
    }

    SetBkMode(dc, TRANSPARENT);
    // The control IS the live state: "Active" green / "Stopped" red.
    // (Window text is ignored so the caption never disagrees with the LED.)
    SetTextColor(dc, sending ? Accent() : Stop());
    HGDIOBJ oldFont = SelectObject(dc, FontBold());
    RECT tr = rc;
    tr.left = ledX + ledR + MulDiv(7, dpi, 96);
    DrawTextW(dc, sending ? L"ACTIVE" : L"STOPPED", -1, &tr,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
    return true;
}

// ============================================================================
// InputDialog
// ============================================================================
struct DlgData
{
    std::wstring title;
    std::vector<Field>* fields = nullptr;
    std::vector<HWND> edits;
    bool confirmed = false;
    bool closed = false;
};

static LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    DlgData* d = reinterpret_cast<DlgData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_CREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        d = static_cast<DlgData*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        return 0;
    }
    if (!d)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_CTLCOLOREDIT:
    {
        // OPAQUE (not TRANSPARENT): multiline edits reflow/repaint heavily
        // while typing, and transparent mode can leave trails of old glyphs.
        HDC dc = (HDC)wParam;
        SetBkMode(dc, OPAQUE);
        SetTextColor(dc, Fg());
        SetBkColor(dc, BgEdit());
        return (LRESULT)DarkEditBrush();
    }
    case WM_CTLCOLORSTATIC:
    {
        HDC dc = (HDC)wParam;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, Fg());
        SetBkColor(dc, Bg());
        return (LRESULT)DarkBgBrush();
    }
    case WM_DRAWITEM:
    {
        auto dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis && DrawDarkButton(dis))
            return TRUE;
        break;
    }
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == IDOK)
        {
            for (size_t i = 0; i < d->edits.size() && i < d->fields->size(); i++)
            {
                int len = GetWindowTextLengthW(d->edits[i]);
                std::wstring v;
                if (len > 0)
                {
                    v.resize((size_t)len);
                    GetWindowTextW(d->edits[i], v.data(), len + 1);
                }
                (*d->fields)[i].value = v;
            }
            d->confirmed = true;
            d->closed = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDCANCEL)
        {
            d->confirmed = false;
            d->closed = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        d->confirmed = false;
        d->closed = true;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool InputDialog(HWND owner, const wchar_t* title, std::vector<Field>& fields, int widthPx)
{
    static bool registered = false;
    const wchar_t* cls = L"WS_InputDlg";
    if (!registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = DlgProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = DarkBgBrush();
        wc.lpszClassName = cls;
        if (!RegisterClassW(&wc))
            return false;
        registered = true;
    }

    DlgData data;
    data.title = title ? title : L"";
    data.fields = &fields;

    UINT dpi = WindowDpi(owner);
    auto S = [&](int v) { return MulDiv(v, (int)dpi, 96); };

    int clientW = S(widthPx);
    int y = S(12);
    int labelH = S(20);
    int singleH = S(28);
    int gap = S(10);

    // Tight single-line boxes that hug the text (same trick as the main
    // panel): text reads centered even if an edit ignores EM_SETRECT.
    // At high DPI the UI font can outgrow the base row, so grow the rows
    // (and labels) to fit instead of squeezing the text.
    int tightH = singleH;
    {
        HDC dc = GetDC(nullptr);
        if (dc)
        {
            HGDIOBJ old = SelectObject(dc, Font());
            TEXTMETRICW tm = {};
            GetTextMetricsW(dc, &tm);
            SelectObject(dc, old);
            ReleaseDC(nullptr, dc);
            int want = tm.tmHeight + MulDiv(10, (int)dpi, 96);
            if (want < S(20))
                want = S(20);
            if (want > singleH)
            {
                singleH = want;
                int needLab = tm.tmHeight + MulDiv(4, (int)dpi, 96);
                if (needLab > labelH)
                    labelH = needLab;
            }
            if (want < singleH)
                tightH = want;
            else
                tightH = singleH;
        }
    }

    // First pass: compute total height
    int totalH = S(12);
    for (const auto& f : fields)
    {
        totalH += labelH + S(4);
        totalH += f.multiline ? (f.editHeightPx > 0 ? MulDiv(f.editHeightPx, (int)dpi, 96) : S(120)) : tightH;
        totalH += gap;
    }
    totalH += S(40);

    RECT wr = { 0, 0, clientW, totalH };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    AdjustWindowRect(&wr, style, FALSE);
    int winW = wr.right - wr.left;
    int winH = wr.bottom - wr.top;

    // Center on owner (or primary screen)
    RECT orc = {};
    if (owner && GetWindowRect(owner, &orc))
    {
        int cx = (orc.left + orc.right) / 2;
        int cy = (orc.top + orc.bottom) / 2;
        int x = cx - winW / 2;
        int x2 = cy - winH / 2;
        HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, cls, title,
            style, x, x2, winW, winH, owner, nullptr, GetModuleHandleW(nullptr), &data);
        if (!dlg)
            return false;

        EnableDarkFrame(dlg);

        // Build fields
        HFONT font = Font();
        int ex = S(12);
        int ew = clientW - S(24);
        for (size_t i = 0; i < fields.size(); i++)
        {
            HWND lbl = CreateWindowExW(0, L"STATIC", fields[i].label.c_str(),
                WS_CHILD | WS_VISIBLE, ex, y, ew, labelH, dlg, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(lbl, WM_SETFONT, (WPARAM)font, 0);
            y += labelH + S(4);

            // Borderless: each edit paints its own inner input ring after
            // its text (see TabSubclassProc); a stock WS_BORDER edge would
            // stay square and light no matter the client colors.
            // NOTE: ES_MULTILINE even for one-liners: tall single-line
            // edits pin text to the top (EM_SETRECT ignored), multiline
            // honors the CenterEditVertically rect. Enter is eaten by the
            // dialog loop/subclass, so behavior stays single-line.
            DWORD es = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_AUTOHSCROLL | ES_MULTILINE;
            int eh = tightH;
            if (fields[i].multiline)
            {
                es = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_MULTILINE |
                    ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;
                eh = fields[i].editHeightPx > 0 ? MulDiv(fields[i].editHeightPx, (int)dpi, 96) : S(120);
            }
            HWND edit = CreateWindowExW(0, L"EDIT", fields[i].value.c_str(),
                es, ex, y, ew, eh, dlg, (HMENU)(1001 + i), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(edit, WM_SETFONT, (WPARAM)font, 0);
            // Dark edge/bars; single-line text is vertically centered below.
            SetWindowTheme(edit, L"DarkMode_Explorer", nullptr);
            if (!fields[i].multiline)
                CenterEditVertically(edit);
            MakeTabbable(edit);
            data.edits.push_back(edit);
            y += eh + gap;
        }

        HWND ok = CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            clientW - S(12) - S(170), y, S(80), S(28), dlg, (HMENU)IDOK, GetModuleHandleW(nullptr), nullptr);
        HWND cancel = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            clientW - S(12) - S(80), y, S(80), S(28), dlg, (HMENU)IDCANCEL, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(ok, WM_SETFONT, (WPARAM)font, 0);
        SendMessageW(cancel, WM_SETFONT, (WPARAM)font, 0);
        MakeTabbable(ok);
        MakeTabbable(cancel);

        ShowWindow(dlg, SW_SHOW);
        UpdateWindow(dlg);
        if (!data.edits.empty())
            SetFocus(data.edits[0]);

        MSG msg;
        while (!data.closed && GetMessageW(&msg, nullptr, 0, 0))
        {
            bool eat = false;
            if (msg.message == WM_KEYDOWN)
            {
                if (msg.wParam == VK_ESCAPE)
                {
                    SendMessageW(dlg, WM_COMMAND, IDCANCEL, 0);
                    eat = true;
                }
                else if (msg.wParam == VK_RETURN)
                {
                    // In a multiline edit, Return inserts a newline
                    HWND focus = GetFocus();
                    bool inMulti = false;
                    for (HWND e : data.edits)
                    {
                        if (e == focus)
                        {
                            LONG st = GetWindowLongW(e, GWL_STYLE);
                            if (st & ES_MULTILINE)
                                inMulti = true;
                            break;
                        }
                    }
                    if (!inMulti)
                    {
                        SendMessageW(dlg, WM_COMMAND, IDOK, 0);
                        eat = true;
                    }
                }
            }
            if (!eat)
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        return data.confirmed;
    }
    return false;
}

// ============================================================================
// ConfirmDialog - Modal dark Yes/No question (MessageBox replacement)
// ============================================================================
struct CfData
{
    bool yes = false;
    bool closed = false;
};

static LRESULT CALLBACK CfDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CfData* d = reinterpret_cast<CfData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_CREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        d = static_cast<CfData*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        return 0;
    }
    if (!d)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_CTLCOLORSTATIC:
    {
        HDC dc = (HDC)wParam;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, Fg());
        SetBkColor(dc, Bg());
        return (LRESULT)DarkBgBrush();
    }
    case WM_DRAWITEM:
    {
        auto dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis && DrawDarkButton(dis))
            return TRUE;
        break;
    }
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == IDYES)
        {
            d->yes = true;
            d->closed = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDNO)
        {
            d->yes = false;
            d->closed = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        d->yes = false;
        d->closed = true;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int ConfirmDialog(HWND owner, const wchar_t* title, const wchar_t* text)
{
    static bool registered = false;
    const wchar_t* cls = L"WS_ConfirmDlg";
    if (!registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = CfDlgProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = DarkBgBrush();
        wc.lpszClassName = cls;
        if (!RegisterClassW(&wc))
            return IDNO;
        registered = true;
    }

    UINT dpi = WindowDpi(owner);
    auto S = [&](int v) { return MulDiv(v, (int)dpi, 96); };

    int clientW = S(340);
    int pad = S(12);
    int btnW = S(80), btnH = S(28), gap = S(8);

    // Measure the message (word-wrapped) so the dialog hugs the text.
    int msgH = S(20);
    {
        HDC dc = GetDC(nullptr);
        if (dc)
        {
            HGDIOBJ old = SelectObject(dc, Font());
            RECT cr = { 0, 0, clientW - pad * 2, 0 };
            DrawTextW(dc, text ? text : L"", -1, &cr,
                DT_LEFT | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
            SelectObject(dc, old);
            ReleaseDC(nullptr, dc);
            msgH = cr.bottom - cr.top;
            if (msgH < S(20))
                msgH = S(20);
        }
    }
    int totalH = S(12) + msgH + S(12) + btnH + S(12);

    RECT wr = { 0, 0, clientW, totalH };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    AdjustWindowRect(&wr, style, FALSE);
    int winW = wr.right - wr.left;
    int winH = wr.bottom - wr.top;

    RECT orc = {};
    int x = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    int y2 = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;
    if (owner && GetWindowRect(owner, &orc))
    {
        x = (orc.left + orc.right) / 2 - winW / 2;
        y2 = (orc.top + orc.bottom) / 2 - winH / 2;
    }
    CfData data;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, cls,
        title ? title : L"", style, x, y2, winW, winH,
        owner, nullptr, GetModuleHandleW(nullptr), &data);
    if (!dlg)
        return IDNO;

    EnableDarkFrame(dlg);

    HFONT font = Font();
    HWND msg = CreateWindowExW(0, L"STATIC", text ? text : L"",
        WS_CHILD | WS_VISIBLE, pad, S(12), clientW - pad * 2, msgH,
        dlg, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(msg, WM_SETFONT, (WPARAM)font, 0);

    int by = S(12) + msgH + S(12);
    HWND yes = CreateWindowW(L"BUTTON", L"Yes",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        clientW - pad - btnW * 2 - gap, by, btnW, btnH,
        dlg, (HMENU)IDYES, GetModuleHandleW(nullptr), nullptr);
    HWND no = CreateWindowW(L"BUTTON", L"No",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        clientW - pad - btnW, by, btnW, btnH,
        dlg, (HMENU)IDNO, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(yes, WM_SETFONT, (WPARAM)font, 0);
    SendMessageW(no, WM_SETFONT, (WPARAM)font, 0);
    MakeTabbable(yes);
    MakeTabbable(no);

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    SetFocus(yes);

    MSG m;
    while (!data.closed && GetMessageW(&m, nullptr, 0, 0))
    {
        bool eat = false;
        if (m.message == WM_KEYDOWN)
        {
            if (m.wParam == VK_ESCAPE)
            {
                SendMessageW(dlg, WM_COMMAND, IDNO, 0);
                eat = true;
            }
            else if (m.wParam == VK_RETURN)
            {
                SendMessageW(dlg, WM_COMMAND, IDYES, 0);
                eat = true;
            }
        }
        if (!eat)
        {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    return data.yes ? IDYES : IDNO;
}

// ============================================================================
// HotkeyDialog (adapted from Spout2OverlayHUD capture dialog)
// ============================================================================
struct HkData
{
    HotkeyCombo combo;
    HotkeyCombo result;
    bool confirmed = false;
    bool closed = false;
    std::wstring targetName;
};

static LRESULT CALLBACK HkDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    HkData* d = reinterpret_cast<HkData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_CREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        d = static_cast<HkData*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        return 0;
    }
    if (!d)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(Bg());
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        SetBkMode(dc, TRANSPARENT);
        UINT dpi = WindowDpi(hwnd);
        HGDIOBJ old = SelectObject(dc, Font());
        int lh = MulDiv(24, (int)dpi, 96);
        RECT l1 = { MulDiv(16, (int)dpi, 96), MulDiv(12, (int)dpi, 96), rc.right, rc.bottom };
        RECT l2 = l1; l2.top += lh; l2.bottom += lh;
        RECT l3 = l1; l3.top += lh * 2; l3.bottom += lh * 2;
        RECT l4 = l1; l4.top += lh * 3; l4.bottom += lh * 3;
        SetTextColor(dc, Fg());
        DrawTextW(dc, (L"Set hotkey for: " + d->targetName).c_str(), -1, &l1, DT_SINGLELINE | DT_NOPREFIX);
        DrawTextW(dc, L"Press a key combination", -1, &l2, DT_SINGLELINE | DT_NOPREFIX);
        SetTextColor(dc, Gray());
        DrawTextW(dc, L"Must include Ctrl, Alt or Win", -1, &l3, DT_SINGLELINE | DT_NOPREFIX);
        DrawTextW(dc, L"Esc = cancel      Backspace = clear binding", -1, &l4, DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        UINT vk = (UINT)wParam;
        if (vk == VK_ESCAPE)
        {
            d->confirmed = false;
            d->closed = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (vk == VK_BACK)
        {
            d->result = HotkeyCombo(); // cleared
            d->confirmed = true;
            d->closed = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN)
            return 0;
        UINT mods = 0;
        if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
        if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
        if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
        if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) mods |= MOD_WIN;
        if (!(mods & (MOD_CONTROL | MOD_ALT | MOD_WIN)))
        {
            MessageBeep(MB_ICONASTERISK);
            return 0;
        }
        d->result.modifiers = mods;
        d->result.vk = vk;
        d->confirmed = true;
        d->closed = true;
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_CLOSE:
        d->confirmed = false;
        d->closed = true;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool HotkeyDialog(HWND owner, const wchar_t* targetName, HotkeyCombo& combo)
{
    static bool registered = false;
    const wchar_t* cls = L"WS_HotkeyDlg";
    if (!registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = HkDlgProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = cls;
        if (!RegisterClassW(&wc))
            return false;
        registered = true;
    }

    HkData data;
    data.combo = combo;
    data.targetName = targetName ? targetName : L"";

    UINT dpi = WindowDpi(owner);
    RECT wr = { 0, 0, MulDiv(380, (int)dpi, 96), MulDiv(120, (int)dpi, 96) };
    AdjustWindowRect(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    int winW = wr.right - wr.left;
    int winH = wr.bottom - wr.top;

    int x = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;
    if (owner)
    {
        RECT orc = {};
        if (GetWindowRect(owner, &orc))
        {
            x = (orc.left + orc.right - winW) / 2;
            y = (orc.top + orc.bottom - winH) / 2;
        }
    }

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, cls,
        L"Set Hotkey - WebStage",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, winW, winH, owner, nullptr, GetModuleHandleW(nullptr), &data);
    if (!dlg)
        return false;

    EnableDarkFrame(dlg);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    SetForegroundWindow(dlg);
    SetFocus(dlg);

    MSG msg;
    while (!data.closed && GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (data.confirmed)
        combo = data.result;
    return data.confirmed;
}

// ============================================================================
// Slider
// ============================================================================
bool Slider::Create(HWND parent, int id)
{
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = Slider::Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"WS_Slider";
        if (!RegisterClassW(&wc))
            return false;
        registered = true;
    }

    m_hwnd = CreateWindowExW(0, L"WS_Slider", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 100, 24, parent, (HMENU)(INT_PTR)id,
        GetModuleHandleW(nullptr), this);
    return m_hwnd != nullptr;
}

void Slider::SetValue(int v)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    if (v == m_value)
        return;
    m_value = v;
    if (m_hwnd)
        InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Slider::SetPosition(int x, int y, int w, int h)
{
    if (m_hwnd)
    {
        SetWindowPos(m_hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        // Same-size moves don't invalidate: repaint or resizes leave trails.
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

void Slider::Notify()
{
    HWND parent = GetParent(m_hwnd);
    if (parent)
        SendMessageW(parent, WM_COMMAND,
            MAKEWPARAM(GetDlgCtrlID(m_hwnd), SLN_CHANGED), (LPARAM)m_hwnd);
}

void Slider::SetFromX(int cx)
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int pad = 9;
    int w = rc.right - rc.left - pad * 2;
    if (w <= 0)
        return;
    int v = (cx - pad) * 100 / w;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    if (v != m_value)
    {
        m_value = v;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        Notify();
    }
}

void Slider::OnPaint()
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

    HBRUSH bg = CreateSolidBrush(Bg());
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    int pad = 9;
    int cy = H / 2;
    int x0 = pad;
    int x1 = W - pad;

    // Track
    HBRUSH track = CreateSolidBrush(Sep());
    RECT tr = { x0, cy - 2, x1, cy + 2 };
    FillRect(mem, &tr, track);
    DeleteObject(track);

    // Fill
    int fx = x0 + (x1 - x0) * m_value / 100;
    HBRUSH fill = CreateSolidBrush(Accent());
    RECT fr = { x0, cy - 2, fx, cy + 2 };
    FillRect(mem, &fr, fill);
    DeleteObject(fill);

    // Knob
    HBRUSH knob = CreateSolidBrush(IsWindowEnabled(m_hwnd) ? RGB(0xFF, 0xFF, 0xFF) : Gray());
    HGDIOBJ oldBr = SelectObject(mem, knob);
    HPEN pen = CreatePen(PS_SOLID, 1, IsWindowEnabled(m_hwnd) ? Accent() : Sep());
    HGDIOBJ oldPen = SelectObject(mem, pen);
    Ellipse(mem, fx - 7, cy - 7, fx + 7, cy + 7);
    SelectObject(mem, oldPen);
    SelectObject(mem, oldBr);
    DeleteObject(pen);
    DeleteObject(knob);

    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(m_hwnd, &ps);
}

LRESULT CALLBACK Slider::Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Slider* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Slider*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<Slider*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        SetCapture(hwnd);
        self->m_drag = true;
        self->SetFromX(GET_X_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        if (self->m_drag && (wParam & MK_LBUTTON))
            self->SetFromX(GET_X_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        if (self->m_drag)
        {
            self->m_drag = false;
            ReleaseCapture();
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_TAB)
        {
            bool back = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            AdvanceFocus(GetParent(hwnd), hwnd, back);
            return 0;
        }
        if (wParam == VK_LEFT || wParam == VK_DOWN)
        {
            self->SetValue(self->m_value - (GetKeyState(VK_SHIFT) & 0x8000 ? 10 : 1));
            self->Notify();
            return 0;
        }
        if (wParam == VK_RIGHT || wParam == VK_UP)
        {
            self->SetValue(self->m_value + (GetKeyState(VK_SHIFT) & 0x8000 ? 10 : 1));
            self->Notify();
            return 0;
        }
        break;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// DarkMenu (adapted from Spout2OverlayHUD owner-drawn menus)
// ============================================================================
namespace
{

struct DarkItem
{
    std::wstring text;
    bool checked = false;
    bool separator = false;
    bool header = false; // non-selectable gray title row
};

std::map<UINT_PTR, DarkItem>& DarkItems()
{
    static std::map<UINT_PTR, DarkItem> items;
    return items;
}

UINT_PTR g_darkSpecialId = 9000;
HHOOK g_darkMenuHook = nullptr;
HWINEVENTHOOK g_darkMenuWinEvent = nullptr;

// Borderless rounded menus (technique adapted from Spout2OverlayHUD):
// disable DWM non-client rendering (kills the light flyout chrome),
// clear the DWM border color, clip the frame band away with a round-rect
// window region, and neutralize the SysShadow companion windows.
static void ApplyMenuDwmAttrs(HWND h);
static void ApplyMenuRegion(HWND h);
static void KillThreadShadows();

static void ApplyMenuDwmAttrs(HWND h)
{
    HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
    if (!dwm)
        dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm)
        return;
    typedef HRESULT(WINAPI * PFNDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
    auto pDwm = (PFNDwmSetWindowAttribute)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (!pDwm)
        return;
    BOOL nc = FALSE;
    pDwm(h, 1 /* DWMWA_NCRENDERING_ENABLED */, &nc, sizeof(nc));
    BOOL dark = TRUE;
    if (FAILED(pDwm(h, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark))))
        pDwm(h, 19, &dark, sizeof(dark));
    COLORREF border = 0xFFFFFFFE; // DWMWA_COLOR_NONE
    pDwm(h, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));
}

static void ApplyMenuRegion(HWND h)
{
    RECT wr, cr;
    if (!GetWindowRect(h, &wr) || !GetClientRect(h, &cr))
        return;
    int W = wr.right - wr.left;
    int H = wr.bottom - wr.top;
    if (W <= 0 || H <= 0)
        return;
    // Window-relative client origin: clips the non-client frame band away
    POINT org = { 0, 0 };
    ClientToScreen(h, &org);
    int x0 = org.x - wr.left;
    int y0 = org.y - wr.top;
    int cw = (int)(cr.right - cr.left);
    int ch = (int)(cr.bottom - cr.top);
    if (cw <= 0 || ch <= 0)
        return;
    UINT dpi = WindowDpi(h);
    int rad = MulDiv(12, (int)dpi, 96);
    if (rad * 2 > cw) rad = cw / 2;
    if (rad * 2 > ch) rad = ch / 2;
    HRGN rgn = CreateRoundRectRgn(x0, y0, x0 + cw + 1, y0 + ch + 1, rad, rad);
    // FALSE: no forced redraw (a region change with redraw fires into the
    // menu show animation and can swallow the first item paint).
    if (rgn)
    {
        if (SetWindowRgn(h, rgn, FALSE) == 0)
            DeleteObject(rgn);
    }
}

static void KillMenuShadow(HWND h)
{
    SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
    HRGN empty = CreateRectRgn(0, 0, 0, 0);
    if (empty)
    {
        if (SetWindowRgn(h, empty, FALSE) == 0)
            DeleteObject(empty);
    }
    ShowWindow(h, SW_HIDE);
}

static BOOL CALLBACK KillShadowEnumProc(HWND h, LPARAM)
{
    wchar_t cls[16] = {};
    if (GetClassNameW(h, cls, 16) && wcscmp(cls, L"SysShadow") == 0)
    {
        DWORD pid = 0;
        DWORD tid = GetWindowThreadProcessId(h, &pid);
        if (tid == GetCurrentThreadId())
            KillMenuShadow(h);
    }
    return TRUE;
}

static void KillThreadShadows()
{
    EnumWindows(KillShadowEnumProc, 0);
}

#define WS_MENU_REPAINT_TIMER 0x5217

static LRESULT CALLBACK DarkMenuWndProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WNDPROC orig = (WNDPROC)GetPropW(h, L"H2SMenuProc");
    switch (msg)
    {
    case WM_SHOWWINDOW:
        if (wParam)
        {
            ApplyMenuDwmAttrs(h);
            ApplyMenuRegion(h);
            KillThreadShadows();
            // The show/fade animation can leave a blank surface: repaint
            // once right after it.
            SetTimer(h, WS_MENU_REPAINT_TIMER, 150, NULL);
        }
        break;
    case WM_TIMER:
        if (wParam == WS_MENU_REPAINT_TIMER)
        {
            KillTimer(h, WS_MENU_REPAINT_TIMER);
            RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
            return 0;
        }
        break;
    case WM_WINDOWPOSCHANGED:
    {
        auto wp = reinterpret_cast<WINDOWPOS*>(lParam);
        if (wp && !(wp->flags & SWP_NOSIZE))
            ApplyMenuRegion(h);
        break;
    }
    case WM_ERASEBKGND:
    {
        HDC dc = (HDC)wParam;
        RECT rc;
        GetClientRect(h, &rc);
        HBRUSH br = CreateSolidBrush(Bg());
        FillRect(dc, &rc, br);
        DeleteObject(br);
        return 1;
    }
    case WM_PRINT:
    case WM_PRINTCLIENT:
        // Menu fade animation captures via print: pre-fill dark so the
        // light class brush never shows, then let the system draw items.
    {
        HDC dc = (HDC)wParam;
        if (dc)
        {
            RECT rc;
            GetClipBox(dc, &rc);
            HBRUSH br = CreateSolidBrush(Bg());
            FillRect(dc, &rc, br);
            DeleteObject(br);
        }
        break;
    }
    case WM_NCDESTROY:
        if (orig)
            SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)orig);
        RemovePropW(h, L"H2SMenuProc");
        break;
    }
    // Paint messages pass through untouched: menus draw items themselves.
    return orig ? CallWindowProcW(orig, h, msg, wParam, lParam)
                : DefWindowProcW(h, msg, wParam, lParam);
}

void StyleDarkMenuWindow(HWND h);

LRESULT CALLBACK DarkMenuCbtProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HCBT_CREATEWND)
    {
        wchar_t cls[16] = {};
        GetClassNameW((HWND)wParam, cls, 16);
        if (wcscmp(cls, L"#32768") == 0)
            StyleDarkMenuWindow((HWND)wParam);
        else if (wcscmp(cls, L"SysShadow") == 0)
            KillMenuShadow((HWND)wParam);
    }
    return CallNextHookEx(g_darkMenuHook, nCode, wParam, lParam);
}

// Safety net in case the CBT hook misses a popup start
static void CALLBACK DarkMenuWinEventProc(HWINEVENTHOOK, DWORD event, HWND h,
    LONG, LONG, DWORD, DWORD)
{
    if (event == EVENT_SYSTEM_MENUPOPUPSTART && h)
    {
        wchar_t cls[16] = {};
        if (GetClassNameW(h, cls, 16) && wcscmp(cls, L"#32768") == 0)
            StyleDarkMenuWindow(h);
    }
}

void StyleDarkMenuWindow(HWND h)
{
    if (GetPropW(h, L"H2SMenuProc"))
        return; // already styled
    ApplyMenuDwmAttrs(h);

    // Dark class brush (per-process, lives forever)
    static HBRUSH darkBrush = nullptr;
    if (!darkBrush)
    {
        darkBrush = CreateSolidBrush(Bg());
        SetClassLongPtrW(h, GCLP_HBRBACKGROUND, (LONG_PTR)darkBrush);
    }

    WNDPROC orig = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)DarkMenuWndProc);
    if (orig)
        SetPropW(h, L"H2SMenuProc", (HANDLE)orig);
}

} // namespace

HMENU CreateDarkMenu()
{
    DarkItems().clear();
    g_darkSpecialId = 9000;
    return CreatePopupMenu();
}

void AddDarkMenuItem(HMENU menu, UINT id, const std::wstring& text, bool checked)
{
    DarkItem d;
    d.text = text;
    d.checked = checked;
    DarkItems()[id] = d;
    AppendMenuW(menu, MF_OWNERDRAW, id, (LPCWSTR)(UINT_PTR)id);
}

void AddDarkMenuSeparator(HMENU menu)
{
    UINT_PTR key = g_darkSpecialId++;
    DarkItem d;
    d.separator = true;
    DarkItems()[key] = d;
    AppendMenuW(menu, MF_OWNERDRAW | MF_DISABLED, (UINT)key, (LPCWSTR)key);
}

void AddDarkMenuHeader(HMENU menu, const std::wstring& text)
{
    UINT_PTR key = g_darkSpecialId++;
    DarkItem d;
    d.text = text;
    d.header = true;
    DarkItems()[key] = d;
    AppendMenuW(menu, MF_OWNERDRAW | MF_DISABLED, (UINT)key, (LPCWSTR)key);
}

bool MeasureDarkMenuItem(HWND hwnd, LPMEASUREITEMSTRUCT mis)
{
    if (!mis || mis->CtlType != ODT_MENU)
        return false;
    auto it = DarkItems().find((UINT_PTR)mis->itemData);
    if (it == DarkItems().end())
        return false;
    const DarkItem& item = it->second;
    UINT dpi = WindowDpi(hwnd);

    if (item.separator)
    {
        mis->itemHeight = MulDiv(9, (int)dpi, 96);
        mis->itemWidth = MulDiv(10, (int)dpi, 96);
        return true;
    }

    HDC dc = GetDC(hwnd);
    HGDIOBJ oldFont = SelectObject(dc, Font());
    SIZE sz = { 0, 0 };
    GetTextExtentPoint32W(dc, item.text.c_str(), (int)item.text.size(), &sz);
    SelectObject(dc, oldFont);
    ReleaseDC(hwnd, dc);

    mis->itemHeight = sz.cy + MulDiv(10, (int)dpi, 96);
    int width = MulDiv(40, (int)dpi, 96) + sz.cx;
    int minW = MulDiv(210, (int)dpi, 96);
    int maxW = MulDiv(340, (int)dpi, 96);
    if (width < minW) width = minW;
    if (width > maxW) width = maxW;
    mis->itemWidth = width;
    return true;
}

bool DrawDarkMenuItem(HWND hwnd, const DRAWITEMSTRUCT* dis)
{
    if (!dis || dis->CtlType != ODT_MENU)
        return false;
    auto it = DarkItems().find((UINT_PTR)dis->itemData);
    if (it == DarkItems().end())
        return false;
    const DarkItem& item = it->second;
    UINT dpi = WindowDpi(hwnd);

    RECT rc = dis->rcItem;
    bool selected = (dis->itemState & ODS_SELECTED) != 0;

    HBRUSH bg = CreateSolidBrush(selected ? Hover() : Bg());
    FillRect(dis->hDC, &rc, bg);
    DeleteObject(bg);

    if (item.separator)
    {
        int midY = (rc.top + rc.bottom) / 2;
        RECT line = { rc.left + MulDiv(12, (int)dpi, 96), midY,
            rc.right - MulDiv(12, (int)dpi, 96), midY + 1 };
        HBRUSH lb = CreateSolidBrush(Sep());
        FillRect(dis->hDC, &line, lb);
        DeleteObject(lb);
        return true;
    }

    SetBkMode(dis->hDC, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dis->hDC, Font());

    if (item.header)
    {
        // Title row: dim gray, never highlighted
        SetTextColor(dis->hDC, Gray());
        RECT tr = { rc.left + MulDiv(14, (int)dpi, 96), rc.top,
            rc.right - MulDiv(10, (int)dpi, 96), rc.bottom };
        DrawTextW(dis->hDC, item.text.c_str(), -1, &tr,
            DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dis->hDC, oldFont);
        return true;
    }

    SetTextColor(dis->hDC, Fg());

    int tx = rc.left + MulDiv(14, (int)dpi, 96);
    if (item.checked)
    {
        RECT cr = { rc.left + MulDiv(12, (int)dpi, 96), rc.top,
            rc.left + MulDiv(34, (int)dpi, 96), rc.bottom };
        SetTextColor(dis->hDC, Accent());
        DrawTextW(dis->hDC, L"\x2713", -1, &cr, DT_VCENTER | DT_SINGLELINE | DT_CENTER);
        SetTextColor(dis->hDC, Fg());
        tx = rc.left + MulDiv(36, (int)dpi, 96);
    }

    RECT tr = { tx, rc.top, rc.right - MulDiv(10, (int)dpi, 96), rc.bottom };
    DrawTextW(dis->hDC, item.text.c_str(), -1, &tr,
        DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dis->hDC, oldFont);
    return true;
}

int TrackDarkMenu(HMENU menu, HWND owner, int x, int y)
{
    SetForegroundWindow(owner);
    g_darkMenuHook = SetWindowsHookExW(WH_CBT, DarkMenuCbtProc, nullptr, GetCurrentThreadId());
    g_darkMenuWinEvent = SetWinEventHook(EVENT_SYSTEM_MENUPOPUPSTART, EVENT_SYSTEM_MENUPOPUPSTART,
        nullptr, DarkMenuWinEventProc, GetCurrentProcessId(), GetCurrentThreadId(),
        WINEVENT_OUTOFCONTEXT);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, owner, nullptr);
    if (g_darkMenuHook)
    {
        UnhookWindowsHookEx(g_darkMenuHook);
        g_darkMenuHook = nullptr;
    }
    if (g_darkMenuWinEvent)
    {
        UnhookWinEvent(g_darkMenuWinEvent);
        g_darkMenuWinEvent = nullptr;
    }
    PostMessageW(owner, WM_NULL, 0, 0);
    DarkItems().clear();
    return cmd;
}

} // namespace Ui


