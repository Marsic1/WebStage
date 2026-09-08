#pragma once

// ============================================================================
// WebStage - Shared UI Helpers
// Dark theme palette, DPI helpers, string conversions, modal dialogs.
// ============================================================================

#include <Windows.h>
#include <string>
#include <vector>

#include "Scene.h" // HotkeyCombo

namespace Ui
{

// ============================================================================
// Dark palette (matches Spout2OverlayHUD menu colors)
// ============================================================================
inline constexpr COLORREF Bg() { return RGB(0x0E, 0x11, 0x13); }
inline constexpr COLORREF BgPanel() { return RGB(0x15, 0x1A, 0x1D); }
inline constexpr COLORREF BgEdit() { return RGB(0x1C, 0x23, 0x27); }
inline constexpr COLORREF Hover() { return RGB(0x22, 0x2B, 0x30); }
inline constexpr COLORREF Fg() { return RGB(0xE8, 0xEC, 0xEC); }
inline constexpr COLORREF Gray() { return RGB(0x8B, 0x96, 0x98); }
inline constexpr COLORREF Sep() { return RGB(0x2C, 0x35, 0x3A); }
inline constexpr COLORREF Accent() { return RGB(0x3D, 0xDC, 0x84); }
inline constexpr COLORREF Stop() { return RGB(0xD9, 0x64, 0x59); }
inline constexpr COLORREF EyeOff() { return RGB(0x5C, 0x66, 0x68); }
inline constexpr COLORREF ChipBg() { return RGB(0x1A, 0x33, 0x2A); }
inline constexpr COLORREF ChipBorder() { return RGB(0x24, 0x68, 0x46); }

// ============================================================================
// Fonts / DPI
// ============================================================================
HFONT Font();              // Segoe UI 9, cached for current process DPI
HFONT FontBold();          // Segoe UI 9 bold
HFONT FontMono();          // Consolas 9 (numbers, scrub chips)
void ResetFonts();         // call on WM_DPICHANGED
void SetActiveDpi(UINT dpi); // process font scale (from the main window)
UINT WindowDpi(HWND hwnd); // dynamic GetDpiForWindow, 96 fallback
int Scale(HWND hwnd, int v);

// Keyboard focus: these windows are not dialogs, so Tab would die inside
// the focused control. Tabbable children forward Tab to AdvanceFocus.
void AdvanceFocus(HWND parent, HWND from, bool back);
// Subclass any focusable child (buttons, dialog edits) so Tab/Shift+Tab
// moves focus instead of doing nothing. Idempotent per control.
void MakeTabbable(HWND ctl);
// Vertically center the text of a single-line edit (stock edits align to
// the top). Re-apply after resizing the control.
void CenterEditVertically(HWND edit);
// Rounded input ring painted INSIDE the edit's own client area (never
// clipped by siblings, unlike parent-painted borders). Accent when focused.
// squareLeft keeps the left edge straight (geometry edits abutting their
// letter chip, like the mockup's chip+field units).
void PaintEditRing(HWND edit, bool squareLeft = false);

// ============================================================================
// Frame chrome
// ============================================================================
void EnableDarkFrame(HWND hwnd); // dark title bar + explorer dark theme
void ForceDarkAppMode();         // process-once preferred app mode (dark buttons)

// ============================================================================
// String conversions (UTF-8 <-> UTF-16)
// ============================================================================
std::string ToUtf8(const std::wstring& w);
std::wstring FromUtf8(const std::string& s);
std::wstring TrimW(const std::wstring& s);

// ============================================================================
// Diagnostics log (%TEMP%\WebStage.log, appended)
// Per-frame spam (composite, paint probes) stays behind IsVerbose, off by
// default: each line is a file open/write/close, which costs real CPU at
// 30+ fps. Lifecycle and errors always log.
// ============================================================================
void Log(const wchar_t* fmt, ...);
void SetVerbose(bool on);
bool IsVerbose();

// ============================================================================
// Dark control painting - call from parent WM_CTLCOLOREDIT / WM_CTLCOLORSTATIC
// ============================================================================
HBRUSH DarkBgBrush();
HBRUSH DarkEditBrush();

// Draw an owner-drawn (BS_OWNERDRAW) push button in dark style.
// Call from the parent's WM_DRAWITEM; returns true when handled.
bool DrawDarkButton(const DRAWITEMSTRUCT* dis);

// Optional toolbar icons for DrawDarkButton (attach via SetButtonIcon).
enum BtnIcon
{
    IconNone = 0,
    IconPlus,
    IconTrash,
    IconUp,
    IconDown,
    IconSliders,
    IconCode, // mockup i-code (<>) for the CSS button
};

// Attach an icon to an owner-drawn button; iconOnly centers the glyph and
// hides the label (compact toolbar buttons like Up/Down).
void SetButtonIcon(HWND button, int icon, bool iconOnly = false);

// Draw an owner-drawn checkbox (BS_OWNERDRAW; the parent owns the check
// state via BM_SETCHECK): dark box + white label. Pass the checked state.
bool DrawDarkCheckBox(const DRAWITEMSTRUCT* dis, bool checked);

// Draw the Send live-state pill: green LED + "Active", or red LED +
// "Stopped". The caption is drawn from state (window text is ignored).
bool DrawSendToggle(const DRAWITEMSTRUCT* dis, bool sending);

// ============================================================================
// Slider - Minimal owner-drawn 0..100 slider (opacity, volume).
// Notifies the parent with WM_COMMAND, id, code SLN_CHANGED on change.
// ============================================================================
constexpr int SLN_CHANGED = 0x1001;

class Slider
{
public:
    bool Create(HWND parent, int id);
    HWND Window() const { return m_hwnd; }
    void SetValue(int v); // 0..100, no notify
    int Value() const { return m_value; }
    void SetPosition(int x, int y, int w, int h);

private:
    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnPaint();
    void SetFromX(int cx);
    void Notify();

    HWND m_hwnd = nullptr;
    int m_value = 100;
    bool m_drag = false;
};

// ============================================================================
// InputDialog - Modal multi-field text dialog (OK/Cancel).
// Returns true when confirmed; Field::value holds the result.
// ============================================================================
struct Field
{
    std::wstring label;
    std::wstring value;
    bool multiline = false;
    int editHeightPx = 0; // 0 = single line height
};

bool InputDialog(HWND owner, const wchar_t* title, std::vector<Field>& fields, int widthPx = 540);

// ============================================================================
// ConfirmDialog - Modal dark Yes/No question (MessageBox replacement).
// Returns IDYES or IDNO. Enter = Yes, Esc/X = No.
// ============================================================================
int ConfirmDialog(HWND owner, const wchar_t* title, const wchar_t* text);

// ============================================================================
// HotkeyDialog - Press-a-combo capture.
// Requires Ctrl/Alt/Win modifier. Esc cancels (returns false, combo untouched).
// Backspace clears the binding (returns true, combo unset).
// ============================================================================
bool HotkeyDialog(HWND owner, const wchar_t* targetName, HotkeyCombo& combo);

// ============================================================================
// DarkMenu - Owner-drawn dark popup menus (tray, context).
// WM_MEASUREITEM / WM_DRAWITEM go to the TrackPopupMenu owner window:
// forward them to MeasureDarkMenuItem / DrawDarkMenuItem.
// ============================================================================
HMENU CreateDarkMenu();
void AddDarkMenuItem(HMENU menu, UINT id, const std::wstring& text, bool checked = false);
void AddDarkMenuHeader(HMENU menu, const std::wstring& text);
void AddDarkMenuSeparator(HMENU menu);
int TrackDarkMenu(HMENU menu, HWND owner, int x, int y);
bool MeasureDarkMenuItem(HWND hwnd, LPMEASUREITEMSTRUCT mis);
bool DrawDarkMenuItem(HWND hwnd, const DRAWITEMSTRUCT* dis);

} // namespace Ui


