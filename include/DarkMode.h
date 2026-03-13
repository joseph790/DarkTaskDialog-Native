// ─── UIAElementInfo ──────────────────────────────────────────────────────────
///////////////////////////////////////////////////////////////////////////////
// DarkMode.h  —  Win32 TaskDialog dark-mode support
// Windows 10 (pixel-swap path) + Windows 11 / 25H2 (native theme path)
//
// UIFILE source: comctl32.dll resource 4255 "UIFILE"  (Win11 26100.7965)
// Theme classes:  TaskDialog        → panel backgrounds (dtb calls)
//                 TaskDialogStyle   → text colours / fonts (gtc / gtf calls)
///////////////////////////////////////////////////////////////////////////////


#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <CommCtrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <ShlObj.h>
#include <uiautomation.h>
#include <vsstyle.h>
#include <atlcomcli.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <Windowsx.h>
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uiautomationcore.lib")
#pragma comment(lib, "msimg32.lib")

// ─── TaskDialog theme part IDs ─────────────────────────────────────────────────
//
// These come directly from the comctl32 UIFILE (resource 4255, Win11 26100).
// Two separate theme class handles are needed:
//
//   OpenThemeData(hwnd, L"TaskDialog")       → panel backgrounds, glyph drawing
//   OpenThemeData(hwnd, L"TaskDialogStyle")  → text colour, font queries
//
// class "TaskDialog"  — dtb(TaskDialog, part, state) calls:
//   Part  1  PrimaryPanel      white on light → RGB(32,32,32) dark
//   Part  8  SecondaryPanel    threedface on light → RGB(44,44,44) dark
//   Part 13  ExpandoButton     glyph; states 1-6 (see TDLGEBS_*)
//   Part 15  FootnotePanel     threedface on light → RGB(44,44,44) dark
//            (also used for ExpandedFooterArea, Separator container)
//   Part 17  SeparatorLine     graytext on light → RGB(77,77,77) dark
//
// class "TaskDialogStyle"  — gtc(TaskDialogStyle, part, 0, 3803) colour queries:
//   Part  2  MainInstruction   LIGHT=RGB(0,51,153)   DARK=RGB(153,235,255)
//   Part  4  ContentText / ContentLink / ExpandedInformationText
//                              LIGHT=RGB(0,0,0)       DARK=RGB(255,255,255)
//   Part  6  ExpandedInformationText / Link
//   Part 12  ExpandoText (expanded + collapsed labels)
//   Part 14  VerificationText
//   Part 15  FootnoteText / FootnoteTextLink
//   Part 18  ExpandedFooterText / ExpandedFooterTextLink
//   Part 21  RadioButton label (LinkArea)

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

// ─── Dark colour constants (Win11 25H2 DarkMode_* uxtheme values) ─────────────
//
// Panel backgrounds
//   Primary  (TaskDialog part  1) → RGB(32,32,32)
//   Secondary(TaskDialog part  8) → RGB(44,44,44)   button row
//   Footnote (TaskDialog part 15) → RGB(44,44,44)   footnote / expandedFooter
//   Separator(TaskDialog part 17) → RGB(77,77,77)
//
// Text colours  (TaskDialogStyle TMT_TEXTCOLOR, state 0)
//   Part  2  MainInstruction  → RGB(153,235,255)  light-blue accent
//   Part  4  ContentText      → RGB(255,255,255)
//   Part  6  ExpandedInfo     → RGB(255,255,255)
//   Part 12  ExpandoText      → RGB(255,255,255)
//   Part 14  VerifyText       → RGB(255,255,255)
//   Part 15  FootnoteText     → RGB(224,224,224)  slightly dimmer
//   Part 18  ExpandedFooter   → RGB(224,224,224)
//   Part 21  RadioButton lbl  → RGB(255,255,255)  (DarkMode_DarkTheme on 25H2)
namespace DarkColors
{
    // Panel fills
    constexpr COLORREF kPrimary = RGB(32, 32, 32);
    constexpr COLORREF kSecondary = RGB(44, 44, 44);
    constexpr COLORREF kFootnote = RGB(44, 44, 44);
    constexpr COLORREF kSeparator = RGB(77, 77, 77);

    // Text (TaskDialogStyle parts)
    constexpr COLORREF kTextInstruct = RGB(153, 235, 255); // part  2
    constexpr COLORREF kTextContent = RGB(255, 255, 255); // part  4
    constexpr COLORREF kTextExpInfo = RGB(255, 255, 255); // part  6
    constexpr COLORREF kTextExpando = RGB(255, 255, 255); // part 12
    constexpr COLORREF kTextVerify = RGB(255, 255, 255); // part 14
    constexpr COLORREF kTextFootnote = RGB(224, 224, 224); // part 15
    constexpr COLORREF kTextFtrExp = RGB(224, 224, 224); // part 18
    constexpr COLORREF kTextRadio = RGB(255, 255, 255); // part 21

    // Aliases used in WmCtColorSubclassProc
    constexpr COLORREF kTextNormal = RGB(255, 255, 255);
    constexpr COLORREF kTextMuted = RGB(224, 224, 224);
}

// ─── OS version detection ──────────────────────────────────────────────────────
namespace OsVer
{
    inline bool IsAtLeastBuild(DWORD build)
    {
        OSVERSIONINFOEXW oi = { sizeof(oi) };
        oi.dwMajorVersion = 10;
        oi.dwBuildNumber = build;
        ULONGLONG mask = VerSetConditionMask(
            VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER_EQUAL),
            VER_BUILDNUMBER, VER_GREATER_EQUAL);
        return VerifyVersionInfoW(&oi, VER_MAJORVERSION | VER_BUILDNUMBER, mask) != FALSE;
    }
    inline bool IsWindows11() { return IsAtLeastBuild(22000); }
    inline bool IsWindows11_25H2() { return IsAtLeastBuild(26200); }
}

// ─── UIA element descriptor ───────────────────────────────────────────────────
struct UIAElementInfo
{
    std::wstring automationId; // UIA AutomationId matching the UIFILE atom() names
    std::wstring name;         // display text (text elements)
    RECT         rect = {};  // client-relative rect inside the DirectUI window
    LONG         legacyState = 0; // STATE_SYSTEM_* (VerificationCheckBox only)
};

// ─── Public API ───────────────────────────────────────────────────────────────
namespace DarkMode
{
    bool Init();                   // Call once before any windows are created
    bool IsActive();               // True when system dark-mode is active
    bool HasNativeTaskDialogTheme(); // True on Win11 (DarkMode_* theme present)
    void EnableForTLW(HWND hwnd);  // Dark title bar on a top-level window
    void AllowForWindow(HWND hwnd, const wchar_t* themeClass = nullptr);
    void AllowForTaskDialog(HWND hwndTaskDialog, TASKDIALOGCONFIG* pConfig);
    void RemoveFromTaskDialog(HWND hwndTaskDialog);
}