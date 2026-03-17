///////////////////////////////////////////////////////////////////////////////
// DarkMode.h  —  Win32 TaskDialog dark-mode support (public API)
///////////////////////////////////////////////////////////////////////////////
#pragma once

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <vssym32.h>
#include <shellapi.h>
#include <uiautomation.h>
#include <atlbase.h>
#include <string>
#include <vector>
#include <unordered_map>

#ifndef TDLG_PRIMARYPANEL
#  define TDLG_PRIMARYPANEL          1
#  define TDLG_MAININSTRUCTIONPANE   2
#  define TDLG_CONTENTPANE           4   // UIFILE TaskDialogStyle part 4
#  define TDLG_EXPINFOPANE           6   // UIFILE TaskDialogStyle part 6
#  define TDLG_SECONDARYPANEL        8   // UIFILE TaskDialog part 8
#  define TDLG_EXPANDOTEXT          12   // UIFILE TaskDialogStyle part 12
#  define TDLG_EXPANDOBUTTON        13   // UIFILE TaskDialog part 13
#  define TDLG_VERIFICATIONTEXT     14   // UIFILE TaskDialogStyle part 14
#  define TDLG_FOOTNOTEPANE         15   // UIFILE TaskDialogStyle part 15
#  define TDLG_EXPANDEDFOOTERAREA   18   // UIFILE TaskDialogStyle part 18
#  define TDLG_RADIOBUTTONPANE      21   // UIFILE TaskDialogStyle part 21
#endif

// ExpandoButton glyph states — TaskDialog part 13, states 1–6
#ifndef TDLGEBS_NORMAL
#  define TDLGEBS_NORMAL          1
#  define TDLGEBS_HOVER           2
#  define TDLGEBS_PRESSED         3
#  define TDLGEBS_EXPANDEDNORMAL  4
#  define TDLGEBS_EXPANDEDHOVER   5
#  define TDLGEBS_EXPANDEDPRESSED 6
#endif

// ─── UIAElementInfo ───────────────────────────────────────────────────────────
// Cached per-element data queried once per TDN_CREATED / TDN_NAVIGATED.

struct UIAElementInfo
{
    std::wstring automationId;
    std::wstring name;
    RECT         rect = {};
    LONG         legacyState = 0; // for VerificationCheckBox only
};

// ─── DarkColors ───────────────────────────────────────────────────────────────
// Matches Win11 25H2 DarkMode_DarkTheme resolved values.

namespace DarkColors
{
    inline constexpr COLORREF kPrimary = RGB(32, 32, 32);
    inline constexpr COLORREF kSecondary = RGB(44, 44, 44);
    inline constexpr COLORREF kFootnote = RGB(44, 44, 44);
    inline constexpr COLORREF kSeparator = RGB(77, 77, 77);

    inline constexpr COLORREF kTextNormal = RGB(255, 255, 255);
    inline constexpr COLORREF kTextInstruct = RGB(153, 235, 255); // MainInstruction accent
    inline constexpr COLORREF kTextContent = RGB(255, 255, 255);
    inline constexpr COLORREF kTextExpInfo = RGB(255, 255, 255);
    inline constexpr COLORREF kTextExpando = RGB(255, 255, 255);
    inline constexpr COLORREF kTextVerify = RGB(255, 255, 255);
    inline constexpr COLORREF kTextFootnote = RGB(224, 224, 224);
    inline constexpr COLORREF kTextFtrExp = RGB(224, 224, 224);
    inline constexpr COLORREF kTextRadio = RGB(255, 255, 255);
}

// ─── DarkMode API ─────────────────────────────────────────────────────────────

namespace DarkMode
{
    // Per-dialog theme override passed to AllowForTaskDialog.
    enum class Theme
    {
        System, // follow OS dark-mode preference  (default — existing behaviour)
        Dark,   // force dark regardless of OS setting
        Light,  // force light regardless of OS setting
    };

   
    // True when dark mode is active.
     bool IsActive();

    // True on Win11 builds where DarkMode_TaskDialog UxTheme classes exist.
    bool HasNativeTaskDialogTheme();

    // Sets DWMWA_USE_IMMERSIVE_DARK_MODE and DarkMode_Explorer window theme
    // on a top-level window (dark title bar + chrome).
    void EnableForTLW(HWND hwnd, bool dark = true);

    // Applies SetWindowTheme to any child control.
    // Called only from the dark path — no IsActive() guard.
    void AllowForWindow(HWND hwnd, const wchar_t* themeClass = nullptr);

    // Main entry point. Call from TDN_CREATED and TDN_NAVIGATED.
    //
    // theme defaults to System (fully backward-compatible).
    //
    //   Theme::System  — resolves to dark/light via IsActive() at call time.
    //   Theme::Dark    — forces dark for this dialog regardless of OS setting.
    //   Theme::Light   — forces light: removes all dark subclasses so the dialog
    //                    uses native light rendering.
    //
    // Subclass presence encodes the dark/light state for this dialog:
    //   subclasses attached    → dialog is dark
    //   subclasses not present → dialog is light
    //
    // TDN_NAVIGATED: call again with the same theme to re-apply after page nav.
    // Dark→light transition: call with Theme::Light (or Theme::System when OS
    //   is light) — all dark subclasses are removed automatically.
    void AllowForTaskDialog(HWND hwndTaskDialog, TASKDIALOGCONFIG* pConfig, Theme theme = Theme::System);

    // Call from TDN_DESTROYED to free per-dialog state.
    // Also called automatically from TaskDialogMainSubclassProc WM_DESTROY.
    void RemoveFromTaskDialog(HWND hwnd);
}