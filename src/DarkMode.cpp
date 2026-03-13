///////////////////////////////////////////////////////////////////////////////
// DarkMode.cpp  —  Win32 TaskDialog dark-mode support 
//
// Rendering strategy derived from comctl32.dll UIFILE (resource 4255, 26100)
//
// Win10 path  (g_hasNativeTheme == false):
//   WM_PRINTCLIENT → BPBF_TOPDOWNDIB → pixel-swap panel colours →
//   FillRect + DrawIconEx        for MainIcon / FootnoteIcon
//   FillRect + DrawThemeBackground for ExpandoButton glyph (all states)
//   FillRect + DrawThemeBackground for Checkbox glyph    (all states)
//   FillRect + DrawThemeTextEx   for all text elements
//
// Win11 path  (g_hasNativeTheme == true):
// no pixel-swap
//   
//
// Child-window theming (AllowForTaskDialog → EnumChildWindows):
//   "TaskDialog" DirectUI window      → SetWindowTheme("DarkMode_Explorer") + DirectUISubclassProc
//   "CCSysLink"  footnote link        → GetParent → WmCtColorSubclassProc(kFootnote brush)
//   CommandLink_ button               → SetWindowTheme("DarkMode_Explorer")
//                                       GetParent → WmCtColorSubclassProc(kPrimary brush)
//                                       (CommandArea is transparent, sits on PrimaryPanel)
//   CommandButton_ button             → SetWindowTheme("DarkMode_Explorer")
//                                       GetParent → WmCtColorSubclassProc(kSecondary brush)
//   RadioButton_ button               → SetWindowTheme("DarkMode_DarkTheme" or "DarkMode_Explorer")
//                                       GetParent → WmCtColorSubclassProc(kPrimary brush)
//                                       (RadioButtonArea sits on PrimaryPanel)
//   ProgressBar                       → SetWindowTheme("DarkMode_CopyEngine" or "DarkMode_Explorer")
// 
//   DrawThemeText  for all text elements
///////////////////////////////////////////////////////////////////////////////

#include "DarkMode.h"
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")

// ─── Undocumented uxtheme ordinal exports ─────────────────────────────────────

typedef bool  (WINAPI* pfnShouldAppsUseDarkMode)();
typedef bool  (WINAPI* pfnAllowDarkModeForWindow)(HWND, bool);
typedef DWORD(WINAPI* pfnSetPreferredAppMode)(DWORD);

static pfnShouldAppsUseDarkMode  g_fnShouldUseDark = nullptr;
static pfnAllowDarkModeForWindow g_fnAllowForWindow = nullptr;
static pfnSetPreferredAppMode    g_fnSetAppMode = nullptr;
static bool                      g_darkEnabled = false;
static bool                      g_hasNativeTheme = false;

// Subclass IDs
static constexpr UINT_PTR kMainSubclassId = 0xDEADBEEFul;
static constexpr UINT_PTR kDirectUISubclassId = 0xBADF00Dul;
static constexpr UINT_PTR kCtlColorId = 0xC0FFEE01ul;

// Forward declarations
static bool IsDarkThemeActive(const wchar_t* dark, const wchar_t* base);
static LRESULT CALLBACK TaskDialogMainSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
static LRESULT CALLBACK DirectUISubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
static LRESULT CALLBACK WmCtColorSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

// ─── DirectUIState ────────────────────────────────────────────────────────────

struct DirectUIState
{
    // ── Two separate theme handles (see UIFILE analysis) ─────────────────────
    // "TaskDialog"      → panel bg, ExpandoButton glyph drawing
    // "TaskDialogStyle" → text colour / font queries for all text parts
    HTHEME hTD = nullptr; // OpenThemeData("TaskDialog")
    HTHEME hTDS = nullptr; // OpenThemeData("TaskDialogStyle")
    HTHEME hButton = nullptr; // OpenThemeData("Button") for checkbox glyph
    bool   isDarkTheme = false;   // a DarkMode_* TDS was resolved
    bool   themesOk = false;

    // ── Cached per-session brushes ────────────────────────────────────────────
    HBRUSH brPrimary = nullptr; // kPrimary   = RGB(32,32,32)
    HBRUSH brSecondary = nullptr; // kSecondary = RGB(44,44,44)
    HBRUSH brFootnote = nullptr; // kFootnote  = RGB(44,44,44)  same value, separate object

    // ── UIA element cache (filled once per TDN_CREATED / TDN_NAVIGATED) ──────
    std::vector<UIAElementInfo> elements;
    bool   elemsOk = false;

    // ── Mouse state (populated by WM_MOUSE* messages, not GetAsyncKeyState) ──
    bool   tracking = false;
    bool   pressing = false;
    int    hotIdx = -1;

    // ── Logical UI state (synced from window props set in the callback) ───────
    bool   isExpanded = false;
    bool   isChecked = false;
    bool   defExpanded = false;
    bool   defChecked = false;

    TASKDIALOGCONFIG* pCfg = nullptr;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void CloseThemes()
    {
        if (hTD) { CloseThemeData(hTD);     hTD = nullptr; }
        if (hTDS) { CloseThemeData(hTDS);    hTDS = nullptr; }
        if (hButton) { CloseThemeData(hButton); hButton = nullptr; }
        themesOk = false;
    }
    void DestroyBrushes()
    {
        if (brPrimary) { DeleteObject(brPrimary);   brPrimary = nullptr; }
        if (brSecondary) { DeleteObject(brSecondary); brSecondary = nullptr; }
        if (brFootnote) { DeleteObject(brFootnote);  brFootnote = nullptr; }
    }
    void Destroy() { CloseThemes(); DestroyBrushes(); elements.clear(); elemsOk = false; }
};

static thread_local std::unordered_map<HWND, DirectUIState> gs_states;

static DirectUIState& GetState(HWND h) { return gs_states[h]; }
//static bool hasExplorer = IsDarkThemeActive(L"DarkMode_Explorer111::TaskDialog", L"TaskDialog");

static void DestroyState(HWND h)
{
    auto it = gs_states.find(h);
    if (it != gs_states.end()) { it->second.Destroy(); gs_states.erase(it); }
}

// ─── Theme helpers ─────────────────────────────────────────────────────────────

static bool IsDarkThemeActive(const wchar_t* dark, const wchar_t* base)
{
    HTHEME hD = OpenThemeData(nullptr, dark);
    HTHEME hB = OpenThemeData(nullptr, base);
    bool a = (hD && hD != hB);
    if (hD) CloseThemeData(hD);
    if (hB) CloseThemeData(hB);
    return a;
}

static void RefreshThemes(HWND hwnd, DirectUIState& s)
{
    s.CloseThemes();
    const UINT dpi = GetDpiForWindow(hwnd);

    auto Open = [&](const wchar_t* cls) -> HTHEME {
        HTHEME h = OpenThemeDataForDpi(hwnd, cls, dpi);
        return h ? h : OpenThemeData(hwnd, cls);
        };

    // Detect available dark-theme variants (Win11 25H2 vs older builds)
    const bool hasDarkTheme = IsDarkThemeActive(L"DarkMode_DarkTheme::TaskDialog", L"TaskDialog");

    s.isDarkTheme = hasDarkTheme || g_hasNativeTheme;

    // "TaskDialog" panel/glyph handle
    if (hasDarkTheme)       s.hTD = Open(L"DarkMode_DarkTheme::TaskDialog");
    if (!s.hTD && g_hasNativeTheme) s.hTD = Open(L"DarkMode_Explorer::TaskDialog");
    if (!s.hTD)             s.hTD = Open(L"TaskDialog"); // light fallback

    // "TaskDialogStyle" colour/font handle
   // if (hasDarkTheme)       s.hTDS = Open(L"DarkMode_DarkTheme::TaskDialogStyle");
    if ( g_hasNativeTheme) s.hTDS = Open(L"DarkMode_Explorer::TaskDialog");
    if (!s.hTDS)            s.hTDS = Open(L"TaskDialog"); // light fallback

    // Button theme for checkbox glyph
    s.hButton = Open(L"Button");
    s.themesOk = true;
}

static void EnsureBrushes(DirectUIState& s)
{
    if (!s.brPrimary)   s.brPrimary = CreateSolidBrush(DarkColors::kPrimary);
    if (!s.brSecondary) s.brSecondary = CreateSolidBrush(DarkColors::kSecondary);
    if (!s.brFootnote)  s.brFootnote = CreateSolidBrush(DarkColors::kFootnote);
}

// ─── Text colour lookup ────────────────────────────────────────────────────────
// Query TMT_TEXTCOLOR from hTDS (TaskDialogStyle) for the given UIFILE part.
// Falls back to the DarkColors constants if the dark theme handle is not available
// or GetThemeColor fails (Win10 without DarkMode_* TaskDialogStyle).

static COLORREF GetTextColor(const DirectUIState& s, int uifilePart)
{
    if (s.isDarkTheme && s.hTDS)
    {
        COLORREF c = RGB(255, 255, 255);
        if (SUCCEEDED(GetThemeColor(s.hTDS, uifilePart, 0, TMT_TEXTCOLOR, &c)))
            return c;
    }
    // Win10 fallback — constants matching Win11 25H2 DarkMode_DarkTheme values
    switch (uifilePart)
    {
    case TDLG_MAININSTRUCTIONPANE: return DarkColors::kTextInstruct;
    case TDLG_CONTENTPANE:         return DarkColors::kTextContent;
    case TDLG_EXPINFOPANE:         return DarkColors::kTextExpInfo;
    case TDLG_EXPANDOTEXT:         return DarkColors::kTextExpando;
    case TDLG_VERIFICATIONTEXT:    return DarkColors::kTextVerify;
    case TDLG_FOOTNOTEPANE:        return DarkColors::kTextFootnote;
    case TDLG_EXPANDEDFOOTERAREA:  return DarkColors::kTextFtrExp;
    case TDLG_RADIOBUTTONPANE:     return DarkColors::kTextRadio;
    default:                       return DarkColors::kTextNormal;
    }
}

// ─── UIA element cache ────────────────────────────────────────────────────────
// Only called from AllowForTaskDialog (TDN_CREATED / TDN_NAVIGATED), never in WM_PAINT.

static bool QueryElements(HWND hwnd, std::vector<UIAElementInfo>& out)
{
    out.clear();
    CComPtr<IUIAutomation> pAuto;
    if (FAILED(pAuto.CoCreateInstance(__uuidof(CUIAutomation)))) return false;

    CComPtr<IUIAutomationElement> pRoot;
    if (FAILED(pAuto->ElementFromHandle(hwnd, &pRoot))) return false;

    CComPtr<IUIAutomationTreeWalker> pWalker;
    pAuto->get_ContentViewWalker(&pWalker);

    CComPtr<IUIAutomationElement> pChild;
    pWalker->GetFirstChildElement(pRoot, &pChild);

    while (pChild)
    {
        UIAElementInfo info{};
        pChild->get_CurrentBoundingRectangle(&info.rect);
        ScreenToClient(hwnd, reinterpret_cast<POINT*>(&info.rect.left));
        ScreenToClient(hwnd, reinterpret_cast<POINT*>(&info.rect.right));

        { CComBSTR b; pChild->get_CurrentAutomationId(&b); if (b) info.automationId = (LPCWSTR)b; }
        { CComBSTR b; pChild->get_CurrentName(&b);         if (b) info.name = (LPCWSTR)b; }

        if (info.automationId == L"VerificationCheckBox")
        {
            VARIANT v; VariantInit(&v);
            if (SUCCEEDED(pChild->GetCurrentPropertyValue(UIA_LegacyIAccessibleStatePropertyId, &v))
                && v.vt == VT_I4)
                info.legacyState = v.lVal;
            VariantClear(&v);
        }

        if (!info.automationId.empty() && !IsRectEmpty(&info.rect))
        {
            // Collect every accessible element ID that the UIFILE defines
            const std::wstring& id = info.automationId;
            if (id == L"MainIcon" || id == L"MainInstruction" ||
                id == L"ContentText" || id == L"ExpandedFooterText" ||
                id == L"ExpandoButton" || id == L"VerificationCheckBox" ||
                id == L"FootnoteText" || id == L"FootnoteIcon" ||
                id.find(L"RadioButton_") == 0 ||
                id.find(L"CommandLink_") == 0 ||
                id.find(L"CommandButton_") == 0)
            {
                out.push_back(std::move(info));
            }
        }

        CComPtr<IUIAutomationElement> pNext;
        pWalker->GetNextSiblingElement(pChild, &pNext);
        pChild = pNext;
    }
    return !out.empty();
}

static void RefreshElements(HWND hwnd, DirectUIState& s)
{
    s.elements.clear();
    QueryElements(hwnd, s.elements);

    // Sync checked state from freshly-queried UIA legacy state
    for (const auto& el : s.elements)
        if (el.automationId == L"VerificationCheckBox")
            s.isChecked = (el.legacyState & STATE_SYSTEM_CHECKED) != 0;
    if (s.defChecked) s.isChecked = true; // honour TDF_VERIFICATION_FLAG_CHECKED

    s.elemsOk = true;
}

static int HitTest(const std::vector<UIAElementInfo>& els, POINT pt)
{
    for (int i = 0; i < (int)els.size(); ++i)
        if (PtInRect(&els[i].rect, pt)) return i;
    return -1;
}

// ─── Pixel-swap pass ──────────────────────────────────────────────────────────
// Replace exact light-mode background colours with dark equivalents in the
// BPBF_TOPDOWNDIB surface produced by WM_PRINTCLIENT.
//
// We query the actual light-theme fill colours so the swap is exact even if
// the system ever changes the precise RGB values.
//
// On Win10: this is the primary background mechanism (no DarkMode_* theme available).
// On Win11: cleans up residual light pixels left by the native theme path.

struct SwapRule { BYTE sR, sG, sB, dR, dG, dB; };

static void PixelSwap(RGBQUAD* px, int rw, int w, int h,
    const SwapRule* rules, int n)
{
    for (int y = 0; y < h; ++y)
    {
        RGBQUAD* row = px + (y * rw);
        for (int x = 0; x < w; ++x)
        {
            RGBQUAD& p = row[x];
            for (int r = 0; r < n; ++r)
            {
                if (p.rgbRed == rules[r].sR && p.rgbGreen == rules[r].sG && p.rgbBlue == rules[r].sB)
                {
                    p.rgbRed = rules[r].dR; p.rgbGreen = rules[r].dG; p.rgbBlue = rules[r].dB; break;
                }
            }
        }
    }
}

// ─── Icon helper ───────────────────────────────────────────────────────────────
static HICON ResolveIcon(const TASKDIALOGCONFIG* cfg, bool isMain)
{
    // If TDF_USE_HICON_MAIN / TDF_USE_HICON_FOOTER is set, the union member
    // holds a real HICON — return it directly.
    // TODO: Fix Icon not draw After Navgation.
    if (isMain)
    {
        if (cfg->dwFlags & TDF_USE_HICON_MAIN)
            return cfg->hMainIcon;
    }
    else
    {
        if (cfg->dwFlags & TDF_USE_HICON_FOOTER)
            return cfg->hFooterIcon;
    }

    // Otherwise the union member holds an INTRESOURCE (TD_*_ICON / MAKEINTRESOURCEW).
    LPCWSTR res = isMain ? cfg->pszMainIcon : cfg->pszFooterIcon;
    if (!res || !IS_INTRESOURCE(res)) return nullptr;

    auto Stock = [](SHSTOCKICONID id) -> HICON {
        SHSTOCKICONINFO sii = { sizeof(sii) };
        return SUCCEEDED(SHGetStockIconInfo(id, SHGSI_ICON | SHGSI_LARGEICON, &sii)) ? sii.hIcon : nullptr;
        };
    switch ((INT_PTR)res) {
    case (INT_PTR)TD_WARNING_ICON:     return Stock(SIID_WARNING);
    case (INT_PTR)TD_ERROR_ICON:       return Stock(SIID_ERROR);
    case (INT_PTR)TD_INFORMATION_ICON: return Stock(SIID_INFO);
    case (INT_PTR)TD_SHIELD_ICON:      return Stock(SIID_SHIELD);
    default: return nullptr;
    }
}

// ─── PaintDirectUI — the single, unified WM_PAINT pipeline ───────────────────
//
// Layer order:
//   1. Native render    — WM_PRINTCLIENT into BPBF_TOPDOWNDIB
//   2. Pixel swap       — replace light panel fills with dark equivalents
//   3. Icon overdraw    — FillRect(dark panel bg) + DrawIconEx
//   4. Glyph overdraw   — FillRect(dark panel bg) + DrawThemeBackground
//                         always for every state (Win10 and Win11 both need it:
//                         native DarkMode_* theme still renders light glyphs
//                         in the normal state)
//   5. Text overdraw    — FillRect(dark panel bg) + DrawThemeTextEx
//
// FillRect clears any light halo before each
// native draw call — clean, exact, no artefacts.

static void PaintDirectUI(HWND hwnd, HDC hdcWin, DirectUIState& s)
{
    if (!s.themesOk) RefreshThemes(hwnd, s);
    EnsureBrushes(s);

    RECT rc; GetClientRect(hwnd, &rc);

    HDC hdcBuf = hdcWin;
    HPAINTBUFFER hbp = BeginBufferedPaint(hdcWin, &rc, BPBF_TOPDOWNDIB, nullptr, &hdcBuf);
    if (!hbp)
    {
        DefSubclassProc(hwnd, WM_PRINTCLIENT, (WPARAM)hdcWin, PRF_CLIENT);
        return;
    }

    // ── 1. Native render ─────────────────────────────────────────────────────
    DefSubclassProc(hwnd, WM_PRINTCLIENT, (WPARAM)hdcBuf, PRF_CLIENT);

    // ── 2. Pixel swap (panel backgrounds) ────────────────────────────────────
    // Query actual light-theme fill colours from "TaskDialog" so the swap is
    // exact regardless of system build. These are also used in steps 3-5 to
    // know which brush to fill before each native-draw call.
    COLORREF bgPri = RGB(255, 255, 255);  // TaskDialog part  1  (PrimaryPanel)
    COLORREF bgSec = RGB(240, 240, 240);  // TaskDialog part  8  (SecondaryPanel)
    COLORREF bgFtn = RGB(240, 240, 240);  // TaskDialog part 15  (FootnotePanel)
    {
        HTHEME hL = OpenThemeData(nullptr, L"TaskDialog");
        if (hL)
        {
            GetThemeColor(hL, TDLG_PRIMARYPANEL, 0, TMT_FILLCOLOR, &bgPri);
            GetThemeColor(hL, TDLG_SECONDARYPANEL, 0, TMT_FILLCOLOR, &bgSec);
            GetThemeColor(hL, TDLG_FOOTNOTEPANE, 0, TMT_FILLCOLOR, &bgFtn);
            CloseThemeData(hL);
        }
    }
    if (!g_hasNativeTheme)
    {
        RGBQUAD* pPx = nullptr; int rw = 0;
        if (SUCCEEDED(GetBufferedPaintBits(hbp, &pPx, &rw)))
        {
            RECT rcBuf = {};
            GetBufferedPaintTargetRect(hbp, &rcBuf);
            int w = rcBuf.right - rcBuf.left, h = rcBuf.bottom - rcBuf.top;

            // Rules: swap known light panel fills → dark equivalents.
            // Separator graytext ≈ 128,128,128 on Win10; 223,223,223 on some builds.
            const SwapRule rules[] = {
                { GetRValue(bgPri), GetGValue(bgPri), GetBValue(bgPri),
                  GetRValue(DarkColors::kPrimary),   GetGValue(DarkColors::kPrimary),   GetBValue(DarkColors::kPrimary)   },
                { GetRValue(bgSec), GetGValue(bgSec), GetBValue(bgSec),
                  GetRValue(DarkColors::kSecondary), GetGValue(DarkColors::kSecondary), GetBValue(DarkColors::kSecondary) },
                { 128, 128, 128,
                  GetRValue(DarkColors::kSeparator), GetGValue(DarkColors::kSeparator), GetBValue(DarkColors::kSeparator) },
                { 223, 223, 223,
                  GetRValue(DarkColors::kSeparator), GetGValue(DarkColors::kSeparator), GetBValue(DarkColors::kSeparator) },
            };
            PixelSwap(pPx, rw, w, h, rules, ARRAYSIZE(rules));
        }
    }

    // ── 3. Icon overdraw ─────────────────────────────────────────────────────
    // FillRect with the correct panel colour wipes out any light-background
    // halo left by step 1, then DrawIconEx renders the icon cleanly on dark.
    //
    // UIFILE:  atom(MainIcon)     height=sysmetric(11) width=sysmetric(12)  → PrimaryPanel bg
    //          atom(FootnoteIcon) height=sysmetric(49) width=sysmetric(50)  → FootnotePanel bg
    if (s.pCfg)
    {
        for (const auto& el : s.elements)
        {
            if (IsRectEmpty(&el.rect)) continue;

            HICON    hIcon = nullptr;
            HBRUSH   brBg = nullptr;

            if (el.automationId == L"MainIcon")
            {
                hIcon = ResolveIcon(s.pCfg, true);
                brBg = s.brPrimary;
            }
            else if (el.automationId == L"FootnoteIcon")
            {
                hIcon = ResolveIcon(s.pCfg, false);
                brBg = s.brFootnote;
            }

            if (!hIcon || !brBg) continue;

            // Erase the light bg completely, then draw icon over pure dark.
            if (!g_hasNativeTheme)
            {
                FillRect(hdcBuf, &el.rect, brBg);
                DrawIconEx(hdcBuf,
                    el.rect.left, el.rect.top,
                    hIcon,
                    el.rect.right - el.rect.left,
                    el.rect.bottom - el.rect.top,
                    0, nullptr, DI_NORMAL);
            }
        }
    }

    // ── 4. Glyph overdraw ────────────────────────────────────────────────────
    // Same pattern: FillRect(dark bg) → DrawThemeBackground.
    // No alpha math, no CorrectAlpha — the fill erases whatever step 1 left.
    //
    // ExpandoButton  → DrawThemeBackground(hTD, TDLG_EXPANDOBUTTON=13, state 1-6)
    //                  UIFILE: atom(ExpandoButtonImage) on transparent CommandArea
    //                          → sits on PrimaryPanel → brPrimary fill
    // Checkbox glyph → DrawThemeBackground(hButton, BP_CHECKBOX, CBS_*)
    //                  UIFILE: atom(VerificationCheckBox) checkboxglyph
    //                          → sits in SecondaryPanel area → brSecondary fill
    //
    // Win10: always draw (no DarkMode_* theme handles any state).
    // Win11: draw for ALL states — the native theme still renders light glyphs
    //        for the normal state even when DarkMode_* is active, so we must
    //        always overdraw, not just on hover/press.
    if (s.hTD || s.hButton)
    {
        for (int i = 0; i < (int)s.elements.size(); ++i)
        {
            const UIAElementInfo& el = s.elements[i];
            if (IsRectEmpty(&el.rect)) continue;

            const bool hot = (i == s.hotIdx);
            const bool press = hot && s.pressing;

            if (el.automationId == L"ExpandoButton" && s.hTD)
            {
                // Glyph box: left portion of the ExpandoButton element rect.
                SIZE sz = {};
                GetThemePartSize(s.hTD, hdcBuf, TDLG_EXPANDOBUTTON,
                    TDLGEBS_NORMAL, nullptr, TS_TRUE, &sz);
                RECT rcGlyph = el.rect;
                rcGlyph.right = el.rect.left + sz.cx + 3;

                int st;
                if (press && s.isExpanded) st = TDLGEBS_EXPANDEDPRESSED;
                else if (press)                 st = TDLGEBS_PRESSED;
                else if (hot && s.isExpanded) st = TDLGEBS_EXPANDEDHOVER;
                else if (hot)                   st = TDLGEBS_HOVER;
                else if (s.isExpanded)          st = TDLGEBS_EXPANDEDNORMAL;
                else                            st = TDLGEBS_NORMAL;
                if (!g_hasNativeTheme)
                {
                    FillRect(hdcBuf, &rcGlyph, s.brSecondary);
                    DrawThemeBackground(s.hTD, hdcBuf, TDLG_EXPANDOBUTTON,
                        st, &rcGlyph, &el.rect);
                }
            }
            else if (el.automationId == L"VerificationCheckBox" && s.hButton)
            {
                // Glyph box: left portion of the VerificationCheckBox element rect.
                SIZE cs = {};
                GetThemePartSize(s.hButton, hdcBuf, BP_CHECKBOX,
                    CBS_UNCHECKEDNORMAL, nullptr, TS_DRAW, &cs);
                const int mg = (el.rect.bottom - el.rect.top - cs.cy) / 3;
                RECT rcGlyph = {
                    el.rect.left + mg + 1,
                    el.rect.top + mg +1,
                    el.rect.left + mg + 1 + cs.cx,
                    el.rect.bottom
                };

                int st;
                if (press && s.isChecked) st = CBS_CHECKEDPRESSED;
                else if (press)                st = CBS_UNCHECKEDPRESSED;
                else if (hot && s.isChecked) st = CBS_CHECKEDHOT;
                else if (hot)                  st = CBS_UNCHECKEDHOT;
                else if (s.isChecked)          st = CBS_CHECKEDNORMAL;
                else                           st = CBS_UNCHECKEDNORMAL;

                if (!g_hasNativeTheme)
                {
                    FillRect(hdcBuf, &rcGlyph, s.brSecondary);
                    DrawThemeBackground(s.hButton, hdcBuf, BP_CHECKBOX,
                        st, &rcGlyph, nullptr);
                }
               
            }
        }
    }

    // ── 5. Text overdraw via TaskDialogStyle theme ────────────────────────────
    // For each text element: FillRect(panel bg) → DrawThemeTextEx.
    // Uses the exact UIFILE TaskDialogStyle part number so GetThemeColor /
    // DrawThemeTextEx query the right colour entry.
    //
    // Note: ExpandoButton and VerificationCheckBox are composite — the glyph
    // box was already drawn in step 4; here we draw the text-label portion only.
    if (s.hTDS || s.hTD)
    {
        HTHEME hThm = s.hTDS ? s.hTDS : s.hTD;

        for (const auto& el : s.elements)
        {
            if (IsRectEmpty(&el.rect)) continue;

            RECT   rcText = el.rect;
            int    uiPart = 0;
            HBRUSH brBg = s.brPrimary;
            DWORD  dtFlags = DT_LEFT | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX;

            if (el.automationId == L"MainInstruction")
            {
                // UIFILE: gtc(TaskDialogStyle, 2, 0, 3803) — light-blue accent on dark
                uiPart = TDLG_MAININSTRUCTIONPANE;
                brBg = s.brPrimary;
            }
            else if (el.automationId == L"ContentText")
            {
                // UIFILE: gtc(TaskDialogStyle, 4, 0, 3803)
                uiPart = TDLG_CONTENTPANE;
                brBg = s.brPrimary;
            }
            else if (el.automationId == L"ExpandedFooterText")
            {
                // UIFILE: gtc(TaskDialogStyle, 18, 0, 3803) — ExpandedFooterArea on FootnotePanel
                uiPart = TDLG_EXPANDEDFOOTERAREA;
                brBg = s.brFootnote;
              
            }
            else if (el.automationId == L"FootnoteText")
            {
                // UIFILE: gtc(TaskDialogStyle, 15, 0, 3803)
                uiPart = TDLG_FOOTNOTEPANE;
                brBg = s.brFootnote;
            }
            else if (el.automationId == L"ExpandoButton" && s.hTD)
            {
                // Text label sits to the right of the glyph box drawn in step 4.
                SIZE sz = {};
                GetThemePartSize(s.hTD, hdcBuf, TDLG_EXPANDOBUTTON,
                    TDLGEBS_NORMAL, nullptr, TS_TRUE, &sz);
                MARGINS textMargins = {};
                GetThemeMargins(s.hTD, hdcBuf, TDLG_VERIFICATIONTEXT, 0, TMT_CONTENTMARGINS, &el.rect, &textMargins);
                rcText.left += sz.cx + textMargins.cxLeftWidth -1;
                rcText.top += 1;
                uiPart = TDLG_EXPANDOTEXT; // part 12
                brBg = s.brSecondary;
                dtFlags = DT_LEFT | DT_VCENTER | DT_NOPREFIX;
            }
            else if (el.automationId == L"VerificationCheckBox" && s.hButton &&s.hTD)
            {
                // Text label sits to the right of the checkbox glyph drawn in step 4.
                SIZE cs = {};
                GetThemePartSize(s.hButton, hdcBuf, BP_CHECKBOX,
                    CBS_UNCHECKEDNORMAL, nullptr, TS_DRAW, &cs);
                MARGINS textMargins = {};
                GetThemeMargins(s.hTD, hdcBuf, TDLG_VERIFICATIONTEXT, 0, TMT_CONTENTMARGINS, &el.rect, &textMargins);
                rcText.left = el.rect.left  + cs.cx + textMargins.cxLeftWidth + 3; // 3px gap after glyph
                rcText.top += 5;
                uiPart = TDLG_VERIFICATIONTEXT; // part 14
                brBg = s.brSecondary;
                dtFlags = DT_LEFT | DT_VCENTER | DT_NOPREFIX;
            }

            if (uiPart == 0) continue;

            DTTOPTS opts = { sizeof(opts) };
            opts.dwFlags = DTT_COMPOSITED | DTT_TEXTCOLOR;
            opts.crText = GetTextColor(s, uiPart);
            if (!g_hasNativeTheme)
            {
                FillRect(hdcBuf, &rcText, brBg);
                DrawThemeTextEx(hThm, hdcBuf, uiPart, 0,
                    el.name.c_str(), -1, dtFlags,
                    &rcText, &opts);
            }

            else
            {
                DrawThemeText(hThm, hdcBuf, uiPart, 0,
                    el.name.c_str(), -1, dtFlags,NULL,
                    &rcText);
            }
          
        }
    }

    EndBufferedPaint(hbp, TRUE);
}

// ─── DirectUI subclass ────────────────────────────────────────────────────────

static LRESULT CALLBACK DirectUISubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uId, DWORD_PTR refData)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1; // WM_PAINT owns the full surface; suppress flicker

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        DirectUIState& s = GetState(hwnd);
        //TODO: need UIA element cache instead of RefreshElements in every paint 
        s.isExpanded = (bool)(GetProp(GetParent(hwnd), L"IsExpanded"));
        RefreshElements(hwnd, s);
        PaintDirectUI(hwnd, hdc, s);
        EndPaint(hwnd, &ps);
        return 0;
    }

    // ── Mouse tracking ────────────────────────────────────────────────────────
    // not need it keep for later after add  UIA element cache
    case WM_MOUSEMOVE:
    {
        DirectUIState& s = GetState(hwnd);
        if (!s.tracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            s.tracking = true;
        }
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int newHot = HitTest(s.elements, pt);
        if (newHot != s.hotIdx) { s.hotIdx = newHot; InvalidateRect(hwnd, nullptr, FALSE); }
        break;
    }
    case WM_MOUSELEAVE:
    {
        DirectUIState& s = GetState(hwnd);
        s.tracking = false; s.pressing = false;
        if (s.hotIdx != -1) { s.hotIdx = -1; InvalidateRect(hwnd, nullptr, FALSE); }
        break;
    }
    case WM_LBUTTONDOWN:
    {
        DirectUIState& s = GetState(hwnd);
        s.pressing = true;
        RefreshElements(hwnd, s);
       // InvalidateRect(hwnd, nullptr, TRUE);
        break;
    }
    case WM_LBUTTONUP:
    {
        DirectUIState& s = GetState(hwnd);

        if (s.pressing)
        {
            s.pressing = false;
        }
        RefreshElements(hwnd, s);
      //  InvalidateRect(hwnd, nullptr, TRUE);
    }
        break;

        // ── Theme / settings changes ──────────────────────────────────────────────
    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    {
        DirectUIState& s = GetState(hwnd);
        s.CloseThemes();
        s.elemsOk = false;
        InvalidateRect(hwnd, nullptr, TRUE);
        break;
    }

    case WM_DESTROY:
        DestroyState(hwnd);
        RemoveWindowSubclass(hwnd, DirectUISubclassProc, uId);
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ─── WmCtColorSubclassProc ────────────────────────────────────────────────────
// Applied to the unnamed CtrlNotifySink parents of:
//   CCSysLink (FootnoteTextLink)  — pass kFootnote brush
//   CommandLink_*                 — pass kPrimary  brush  (CommandArea is transparent on Primary)
//   CommandButton_*               — pass kSecondary brush (SecondaryPanel / ButtonArea)
//   RadioButton_*                 — pass kPrimary  brush  (RadioButtonArea on Primary)
//
// dwRefData: HBRUSH owned by this subclass instance; freed in WM_DESTROY.
//            Pass 0 to use the window's class brush (no owned resource).

static LRESULT CALLBACK WmCtColorSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uId, DWORD_PTR dwRef)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
    {
        if (!dwRef) break;
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)dwRef);
        SetTextColor(hdc, DarkColors::kTextNormal);
        return 1;
    }
    case WM_CTLCOLORMSGBOX:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        // Background colour for this control's parent panel
        COLORREF bg = DarkColors::kSecondary;
        if (dwRef) {
            // Infer from the brush colour stored as dwRef
            // (not ideal — just set bk colour to match the brush we own)
            LOGBRUSH lb = {};
            GetObject((HBRUSH)dwRef, sizeof(lb), &lb);
            if (lb.lbStyle == BS_SOLID) bg = lb.lbColor;
        }
        SetBkColor(hdc, bg);
        SetTextColor(hdc, DarkColors::kTextNormal);
        HBRUSH br = dwRef ? (HBRUSH)dwRef
            : (HBRUSH)GetClassLongPtr(hwnd, GCLP_HBRBACKGROUND);
        return (LRESULT)br;
    }
    case WM_DESTROY:
        if (dwRef) DeleteObject((HBRUSH)dwRef);
        RemoveWindowSubclass(hwnd, WmCtColorSubclassProc, uId);
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ─── Outer dialog subclass ────────────────────────────────────────────────────

static LRESULT CALLBACK TaskDialogMainSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uId, DWORD_PTR dwRef)
{
    switch (msg)
    {
    case WM_CTLCOLORDLG:
        if (DarkMode::IsActive())
        {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, DarkColors::kSecondary);
            SetTextColor(hdc, DarkColors::kTextNormal);
            static HBRUSH sBr = CreateSolidBrush(DarkColors::kSecondary);
            return (LRESULT)sBr;
        }
        break;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        // Do NOT call AllowForTaskDialog here — AllowForTaskDialog itself sends
        // WM_THEMECHANGED at the end, which would recurse back into this handler
        // infinitely. AllowForTaskDialog is called directly from TDN_CREATED and
        // TDN_NAVIGATED in the TaskDialogCallbackProc; that is the only call site.
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        break;
    case WM_DESTROY:
        DarkMode::RemoveFromTaskDialog(hwnd);
        RemoveWindowSubclass(hwnd, TaskDialogMainSubclassProc, uId);
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ─── DarkMode namespace ───────────────────────────────────────────────────────

namespace DarkMode
{

    bool Init()
    {
        HMODULE hUx = GetModuleHandleW(L"uxtheme.dll");
        if (!hUx) hUx = LoadLibraryW(L"uxtheme.dll");
        if (!hUx) return false;

        g_fnShouldUseDark = (pfnShouldAppsUseDarkMode)GetProcAddress(hUx, MAKEINTRESOURCEA(132));
        g_fnAllowForWindow = (pfnAllowDarkModeForWindow)GetProcAddress(hUx, MAKEINTRESOURCEA(133));
        g_fnSetAppMode = (pfnSetPreferredAppMode)GetProcAddress(hUx, MAKEINTRESOURCEA(135));

        if (!g_fnShouldUseDark || !g_fnAllowForWindow || !g_fnSetAppMode) return false;

        g_fnSetAppMode(2); // AllowDark / ForceDark
        g_darkEnabled = true;

        // Detect native dark-theme support (Win11+)
        g_hasNativeTheme =
            IsDarkThemeActive(L"DarkMode_Explorer::TaskDialog", L"TaskDialog") ||
            IsDarkThemeActive(L"DarkMode_DarkTheme::TaskDialog", L"TaskDialog");

        return true;
    }

    bool IsActive()
    {
        return g_darkEnabled && g_fnShouldUseDark && g_fnShouldUseDark();
    }

    bool HasNativeTaskDialogTheme() { return g_hasNativeTheme; }

    void EnableForTLW(HWND hwnd)
    {
        if (!IsActive()) return;
        static HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
        if (!hDwm) return;
        using pfnDwmSWA = HRESULT(WINAPI*)(HWND, DWORD, const void*, DWORD);
        static auto fn = (pfnDwmSWA)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (fn) {
            BOOL use = TRUE;
            if (FAILED(fn(hwnd, 20, &use, sizeof(use)))) // DWMWA_USE_IMMERSIVE_DARK_MODE
                fn(hwnd, 19, &use, sizeof(use));          // pre-release attribute
        }
        if (g_fnAllowForWindow) g_fnAllowForWindow(hwnd, true);
    }

    void AllowForWindow(HWND hwnd, const wchar_t* theme)
    {
        if (!IsActive()) return;
        if (theme) SetWindowTheme(hwnd, theme, nullptr);
    }

    // ─── AllowForTaskDialog ────────────────────────────────────────────────────────
    //
    // Walks the TaskDialog's child window tree (once per TDN_CREATED / TDN_NAVIGATED):
    //
    //   "TaskDialog" class  (DirectUI TaskPage)
    //     → SetWindowTheme("DarkMode_Explorer")
    //     → Attach DirectUISubclassProc
    //     → Walk TaskPage children for buttons/radio/progress
    //
    //   "CCSysLink" class  (footnote hyperlink, atom(FootnoteTextLink) and atom(ContentLink))
    //     → GetParent (CtrlNotifySink) → WmCtColorSubclassProc with kFootnote brush
    //
    // Per the UIFILE:
    //   CommandLink_*  → CommandArea (transparent on PrimaryPanel)  → kPrimary brush
    //   RadioButton_*  → RadioButtonArea (on PrimaryPanel)          → kPrimary brush
    //   CommandButton_ → ButtonArea / SecondaryPanel                → kSecondary brush
    //   ProgressBar    → ProgressArea (on PrimaryPanel)             → kPrimary brush
    //     theme: DarkMode_CopyEngine (Win11 25H2) or DarkMode_Explorer

    void AllowForTaskDialog(HWND hwndTD, TASKDIALOGCONFIG* pCfg)
    {
        // Attach main subclass (handles WM_CTLCOLORDLG, WM_SETTINGCHANGE)
        {
            DWORD_PTR existing = 0;
            if (!GetWindowSubclass(hwndTD, TaskDialogMainSubclassProc, kMainSubclassId, &existing))
                SetWindowSubclass(hwndTD, TaskDialogMainSubclassProc,
                    kMainSubclassId, (DWORD_PTR)pCfg);
        }
        if (!IsActive()) return;

        struct EnumData { HWND hwndTD; bool found; TASKDIALOGCONFIG* pCfg; };
        EnumData data = { hwndTD, false, pCfg };

        EnumChildWindows(hwndTD,
            [](HWND hwndChild, LPARAM lp) -> BOOL
            {
                EnumData* d = reinterpret_cast<EnumData*>(lp);

                HRESULT hr =  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                bool comInit = SUCCEEDED(hr);

                CComPtr<IUIAutomation> pAuto;
                hr = pAuto.CoCreateInstance(__uuidof(CUIAutomation));
                if (FAILED(hr)) { if (comInit) CoUninitialize(); return TRUE; }

                CComPtr<IUIAutomationElement> pEl;
                if (FAILED(pAuto->ElementFromHandle(hwndChild, &pEl)))
                {
                    if (comInit) CoUninitialize(); return TRUE;
                }

                CComBSTR cls;
                pEl->get_CurrentClassName(&cls);

                // ── CCSysLink — footnote / content hyperlinks ─────────────────────
                if (cls == L"CCSysLink")
                {
                    HWND hLink = nullptr;
                    if (SUCCEEDED(pEl->get_CurrentNativeWindowHandle((UIA_HWND*)&hLink)) && hLink)
                    {
                        HWND hParent = GetParent(hLink);
                        if (hParent)
                        {
                            CComBSTR bId;
                            pEl->get_CurrentAutomationId(&bId);
                            const std::wstring id = bId ? (LPCWSTR)bId : L"";

                            const bool isFootnote = (id == L"FootnoteTextLink" ||
                                id == L"ExpandedFooterTextLink" ||
                                id.find(L"Footnote") != std::wstring::npos ||
                                id.find(L"ExpandedFooter") != std::wstring::npos);
                             COLORREF bg = isFootnote && !g_hasNativeTheme ? DarkColors::kFootnote : DarkColors::kPrimary;

                            if (id == L"ContentLink")
                            {
                                bg = g_hasNativeTheme ? DarkColors::kFootnote : DarkColors::kPrimary;
                            }
                            HBRUSH newBr = CreateSolidBrush(bg);
                            HBRUSH oldBr = (HBRUSH)SetClassLongPtr(hParent, GCLP_HBRBACKGROUND, (LONG_PTR)newBr);
                            if (oldBr && oldBr != GetSysColorBrush(COLOR_WINDOW) &&
                                oldBr != GetSysColorBrush(COLOR_BTNFACE))
                                DeleteObject(oldBr);

                            DWORD_PTR ex = 0;
                            if (!GetWindowSubclass(hParent, WmCtColorSubclassProc, kCtlColorId, &ex))
                                SetWindowSubclass(hParent, WmCtColorSubclassProc,
                                    kCtlColorId, (DWORD_PTR)CreateSolidBrush(bg));
                        }
                    }
                    if (comInit) CoUninitialize();
                    return TRUE;
                }

                // ── "TaskDialog" — the DirectUI TaskPage window ───────────────────
                if (cls != L"TaskDialog")
                {
                    if (comInit) CoUninitialize(); return TRUE;
                }

                HWND hDUI = nullptr;
                if (FAILED(pEl->get_CurrentNativeWindowHandle((UIA_HWND*)&hDUI)) || !hDUI)
                {
                    if (comInit) CoUninitialize(); return TRUE;
                }

                // Class background brush
                {
                    HBRUSH nb = CreateSolidBrush(DarkColors::kSecondary);
                    HBRUSH ob = (HBRUSH)SetClassLongPtr(hDUI, GCLP_HBRBACKGROUND, (LONG_PTR)nb);
                    if (ob && ob != GetSysColorBrush(COLOR_WINDOW) &&
                        ob != GetSysColorBrush(COLOR_BTNFACE))
                        DeleteObject(ob);
                }
               
                // ── Walk TaskPage UIA children ────────────────────────────────────
                CComPtr<IUIAutomationTreeWalker> pWalker;
                pAuto->get_ContentViewWalker(&pWalker);
                CComPtr<IUIAutomationElement> pChild;
                pWalker->GetFirstChildElement(pEl, &pChild);

                while (pChild)
                {
                    CONTROLTYPEID ct = 0;
                    pChild->get_CurrentControlType(&ct);

                    if (ct == UIA_ButtonControlTypeId ||
                        ct == UIA_RadioButtonControlTypeId ||
                        ct == UIA_ProgressBarControlTypeId || 
                        ct == UIA_HyperlinkControlTypeId ||
                        ct == UIA_ScrollBarControlTypeId)
                    {
                        HWND hBtn = nullptr;
                        if (SUCCEEDED(pChild->get_CurrentNativeWindowHandle((UIA_HWND*)&hBtn)) && hBtn)
                        {
                            BSTR bstrClass = nullptr, bstrId = nullptr, bstrName = nullptr;
                            pChild->get_CurrentClassName(&bstrClass);
                            pChild->get_CurrentAutomationId(&bstrId);
                            pChild->get_CurrentName(&bstrName);

                            const std::wstring id = bstrId ? bstrId : L"";
                            const std::wstring name = bstrName ? bstrName : L"";

#ifdef _DEBUG
                            {
                                wchar_t dbg[512];
                                swprintf_s(dbg, L"[DarkMode] ct=%lu id=%-24s name=%s cls=%s\n",
                                    ct, id.c_str(), name.c_str(),
                                    bstrClass ? bstrClass : L"");
                                OutputDebugStringW(dbg);
                            }
#endif

                            HWND hP = GetParent(hBtn);

                            if (ct == UIA_ProgressBarControlTypeId)
                            {
                                const bool hasCopyEngine =
                                    IsDarkThemeActive(L"DarkMode_CopyEngine::Progress", L"Progress");
                                AllowForWindow(hBtn, hasCopyEngine
                                    ? L"DarkMode_CopyEngine" : L"DarkMode_Explorer");
                            }
                            else if (ct == UIA_RadioButtonControlTypeId || id.find(L"RadioButton_") == 0 || ct == UIA_HyperlinkControlTypeId)
                            {
                                //TODO : need Subclass for draw RadioButton Text with DarkMode Forground color if DarkMode_DarkTheme Not Found
                                const bool hasDarkTheme =
                                    IsDarkThemeActive(L"DarkMode_DarkTheme::TaskDialog", L"TaskDialog");
                                AllowForWindow(hBtn, hasDarkTheme
                                    ? L"DarkMode_DarkTheme" : L"DarkMode_Explorer");
                                DWORD_PTR ex = 0;
                                if (hP && !GetWindowSubclass(hP, WmCtColorSubclassProc, kCtlColorId, &ex))
                                    SetWindowSubclass(hP, WmCtColorSubclassProc, kCtlColorId,
                                        (DWORD_PTR)CreateSolidBrush(g_hasNativeTheme ? DarkColors::kSecondary : DarkColors::kPrimary));
                            }
                            else if (id.find(L"CommandLink_") == 0)
                            {
                                AllowForWindow(hBtn, L"DarkMode_Explorer");
                                DWORD_PTR ex = 0;
                                if (hP && !GetWindowSubclass(hP, WmCtColorSubclassProc, kCtlColorId, &ex))
                                    SetWindowSubclass(hP, WmCtColorSubclassProc, kCtlColorId,
                                        (DWORD_PTR)CreateSolidBrush(DarkColors::kPrimary));
                            }
                            else if (id.find(L"CommandButton_") == 0)
                            {
                                AllowForWindow(hBtn, L"DarkMode_Explorer");
                                DWORD_PTR ex = 0;
                                if (hP && !GetWindowSubclass(hP, WmCtColorSubclassProc, kCtlColorId, &ex))
                                    SetWindowSubclass(hP, WmCtColorSubclassProc, kCtlColorId,
                                        (DWORD_PTR)CreateSolidBrush(DarkColors::kSecondary));
                            }
                            else
                            {
                                AllowForWindow(hBtn, L"DarkMode_Explorer");
                            }

                            if (bstrClass) SysFreeString(bstrClass);
                            if (bstrId)    SysFreeString(bstrId);
                            if (bstrName)  SysFreeString(bstrName);
                        }
                    }

                    CComPtr<IUIAutomationElement> pNext;
                    pWalker->GetNextSiblingElement(pChild, &pNext);
                    pChild = pNext;
                }

                // Window theme for TaskPage need to be after Children.
                 DarkMode::AllowForWindow(hDUI, L"DarkMode_Explorer");

                // ── Store state and attach DirectUI subclass (idempotent) ─────────
                {
                    DirectUIState& s = GetState(hDUI);
                    s.pCfg = d->pCfg;
                    s.defExpanded = d->pCfg
                        ? (d->pCfg->dwFlags & TDF_EXPANDED_BY_DEFAULT) != 0 : false;
                    s.defChecked = d->pCfg
                        ? (d->pCfg->dwFlags & TDF_VERIFICATION_FLAG_CHECKED) != 0 : false;

                    s.isExpanded = (bool)(LONG_PTR)GetProp(d->hwndTD, L"IsExpanded");
                    s.isChecked = (bool)(LONG_PTR)GetProp(d->hwndTD, L"IsChecked");
                    if (s.defChecked) s.isChecked = true;

                    s.elemsOk = false;
                    RefreshElements(hDUI, s);

                    DWORD_PTR ex = 0;
                    if (!GetWindowSubclass(hDUI, DirectUISubclassProc, kDirectUISubclassId, &ex))
                    {
                        SetWindowSubclass(hDUI, DirectUISubclassProc,
                            kDirectUISubclassId, (DWORD_PTR)d->pCfg);
                    }
                       
                }

                d->found = true;
                if (comInit) CoUninitialize();
                return TRUE;   // keep enumerating (CCSysLink may follow)
            }, (LPARAM)&data);

        if (data.found)
        {
            // Apply the theme to dialog in Windows 11 to get all theme for children need to be after TaskPage.
            if (g_hasNativeTheme)
            {
                AllowForWindow(hwndTD, L"DarkMode_Explorer");
            }

            EnableForTLW(hwndTD);
            SendMessage(hwndTD, WM_THEMECHANGED, 0, 0);
        }
    }

    void RemoveFromTaskDialog(HWND hwndTD)
    {
        RemoveWindowSubclass(hwndTD, TaskDialogMainSubclassProc, kMainSubclassId);
        EnumChildWindows(hwndTD, [](HWND hwndChild, LPARAM) -> BOOL
            {
                DWORD_PTR ex = 0;
                if (GetWindowSubclass(hwndChild, DirectUISubclassProc, kDirectUISubclassId, &ex))
                {
                    RemoveWindowSubclass(hwndChild, DirectUISubclassProc, kDirectUISubclassId);
                    DestroyState(hwndChild);
                }
                if (GetWindowSubclass(hwndChild, WmCtColorSubclassProc, kCtlColorId, &ex))
                    RemoveWindowSubclass(hwndChild, WmCtColorSubclassProc, kCtlColorId);
                return TRUE;
            }, 0);
    }

} // namespace DarkMode