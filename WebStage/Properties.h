#pragma once

// ============================================================================
// WebStage - Properties Panel
// Child window with property editors for the selected source:
// name, URL (applied on Enter/focus loss), geometry, render resolution,
// visible/muted checkboxes, hotkey set/clear, CSS editor button.
// ============================================================================

#include <Windows.h>

#include "Ui.h"

class App; // forward (implemented in App.h)

class Properties
{
public:
    bool Create(HWND parent, App* app);
    HWND Window() const { return m_hwnd; }
    void Refresh(); // reload fields from the selected source
    void SetPosition(int x, int y, int w, int h);

private:
    enum Ctl : int
    {
        IdName = 101, IdUrl, IdX, IdY, IdW, IdH, IdRw, IdRh,
        IdVisible = 201, IdMuted = 202,
        IdHotkeySet = 301, IdHotkeyClear = 302, IdCss = 303,
        IdHotkeyValue = 401,
        IdOpacity = 501, IdVolume = 502,
        IdOpacityVal = 503, IdVolumeVal = 504,
        IdSecSource = 601, IdSecGeometry, IdSecOutput,
        IdLabX = 701, // ..+5: scrub chips (X Y W H RW RH)
    };

    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK FieldSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
        UINT_PTR, DWORD_PTR ref);
    void BuildControls();
    void Layout();
    void ReadField(int id);

    // Geometry scrub: the letter chips are drag handles for the paired edit
    void ScrubBegin(int i, int x);
    void ScrubMove(int i, int x);
    void ScrubEnd(int i);
    static LRESULT CALLBACK ScrubLabelProc(HWND hwnd, UINT msg, WPARAM wParam,
        LPARAM lParam, UINT_PTR, DWORD_PTR ref);
    static void DrawSectionHeader(const DRAWITEMSTRUCT* dis, bool withHint);
    static void DrawScrubChip(const DRAWITEMSTRUCT* dis, bool active, bool disabled);
    static void DrawHotkeyChip(const DRAWITEMSTRUCT* dis);

    HWND m_hwnd = nullptr;
    App* m_app = nullptr;
    bool m_updating = false;

    // Owner-drawn checkbox state, mirrored from the model in Refresh().
    // (Kept here instead of BM_SETCHECK/BM_GETCHECK: check state does not
    // round-trip reliably on BS_OWNERDRAW buttons.)
    bool m_visChecked = false;
    bool m_muteChecked = false;    HWND m_urlEdit = nullptr;
    WNDPROC m_fieldOrigProc = nullptr;
    WNDPROC m_geoOrigProc = nullptr; // statics keep painting via their own proc
    HWND m_urlLab = nullptr;
    HWND m_nameLab = nullptr;
    HWND m_geoLab[6] = {};
    HWND m_hkLab = nullptr;
    HWND m_opLab = nullptr;
    HWND m_volLab = nullptr;

    // Scrub state (m_scrubIdx: 0..5 = X Y W H RW RH, -1 = idle)
    int m_scrubIdx = -1;
    int m_scrubStartX = 0;
    int m_scrubStartVal = 0;

    Ui::Slider m_opacity;
    Ui::Slider m_volume;
};

