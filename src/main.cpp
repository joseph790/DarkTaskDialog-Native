///////////////////////////////////////////////////////////////////////////////
// main.cpp  —  Dark TaskDialog demo  (Win32 / VS2022)
///////////////////////////////////////////////////////////////////////////////

#include "DarkMode.h"
#include <strsafe.h>

#pragma comment(linker,"/manifestdependency:\"type='win32' "\
    "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' "\
    "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ─── Radio button IDs used inside the TaskDialog ──────────────────────────────
#define RB_THEME_SYSTEM  1
#define RB_THEME_DARK    2
#define RB_THEME_LIGHT   3

HWND g_hwnd;
// ─── Per-session theme — updated live from TDN_RADIO_BUTTON_CLICKED ───────────
static DarkMode::Theme g_theme = DarkMode::Theme::System;

// ─── Helper: re-apply theme to an already-open dialog ─────────────────────────
// Called from TDN_RADIO_BUTTON_CLICKED after g_theme is updated.
// Passes the new theme so AllowForTaskDialog either attaches or removes
// dark subclasses without touching global state.
static void ReapplyTheme(HWND hwndTD, TASKDIALOGCONFIG* pCfg)
{
    DarkMode::AllowForTaskDialog(hwndTD, pCfg, g_theme);
    // Force a full repaint so the change is visible immediately.
    RedrawWindow(hwndTD, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// ─── Shared callback ──────────────────────────────────────────────────────────
static HRESULT CALLBACK TDCallback(
    HWND hwnd, UINT note, WPARAM wParam, LPARAM lParam, LONG_PTR dwRef)
{
    TASKDIALOGCONFIG* pCfg = reinterpret_cast<TASKDIALOGCONFIG*>(dwRef);

    switch (note)
    {
    case TDN_CREATED:
        SetProp(hwnd, L"IsExpanded",
            (HANDLE)(LONG_PTR)((pCfg->dwFlags & TDF_EXPANDED_BY_DEFAULT) ? 1 : 0));
        SetProp(hwnd, L"IsChecked",
            (HANDLE)(LONG_PTR)((pCfg->dwFlags & TDF_VERIFICATION_FLAG_CHECKED) ? 1 : 0));
        DarkMode::AllowForTaskDialog(hwnd, pCfg, g_theme);
        break;

    case TDN_NAVIGATED:
        DarkMode::AllowForTaskDialog(
            hwnd,
            reinterpret_cast<TASKDIALOGCONFIG*>(lParam),
            g_theme);
        break;

    case TDN_RADIO_BUTTON_CLICKED:
        // Update the global theme and re-apply to this dialog immediately —
        // the user sees the switch without closing and re-opening.
        switch ((int)wParam)
        {
        case RB_THEME_SYSTEM: g_theme = DarkMode::Theme::System; break;
        case RB_THEME_DARK:   g_theme = DarkMode::Theme::Dark;   break;
        case RB_THEME_LIGHT:  g_theme = DarkMode::Theme::Light;  break;
        }
        ReapplyTheme(hwnd, pCfg);
        SendMessage(g_hwnd, WM_SETTINGCHANGE, NULL, NULL);
        break;

    case TDN_EXPANDO_BUTTON_CLICKED:
        SetProp(hwnd, L"IsExpanded", (HANDLE)(LONG_PTR)(wParam ? 1 : 0));
        break;

    case TDN_VERIFICATION_CLICKED:
        SetProp(hwnd, L"IsChecked", (HANDLE)(LONG_PTR)(wParam ? 1 : 0));
        break;

    case TDN_HYPERLINK_CLICKED:
        ShellExecuteW(hwnd, L"open", (LPCWSTR)lParam, nullptr, nullptr, SW_SHOWNORMAL);
        break;

    case TDN_BUTTON_CLICKED:
    {
        if ((int)wParam == IDOK)
        {
            // Navigate to an Arabic RTL page.
            static  TASKDIALOGCONFIG configNext = { sizeof(TASKDIALOGCONFIG) };
            configNext.dwFlags = TDF_RTL_LAYOUT |
                TDF_ALLOW_DIALOG_CANCELLATION |
                TDF_ENABLE_HYPERLINKS;
            configNext.pszWindowTitle = L"عنوان النافذة";
            configNext.pszMainInstruction = L"تم الانتقال بنجاح!";
            configNext.pszContent =
                L"هذه المحتويات الجديدة مع إضافة اتجاه العربية من اليمين لليسار.\r\n"
                L"انظر إلى <a href=\"https://learn.microsoft.com/en-us/dynamics365/"
                L"fin-ops-core/dev-itpro/user-interface/bidirectional-support\">"
                L"دعم اللغة من اليمين إلى اليسار</a> لمزيد من المعلومات.";
            configNext.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;
            configNext.pszMainIcon = TD_INFORMATION_ICON;

            const DarkMode::Theme capturedTheme = g_theme;
            struct EnumData
            {

                TASKDIALOGCONFIG* pCfg;
                DarkMode::Theme   theme;
            };
            EnumData* pData = new EnumData{ &configNext, capturedTheme };
            configNext.lpCallbackData = (LONG_PTR)pData;
            EnumData* data = NULL;
            configNext.pfCallback = [](HWND hwnd, UINT note, WPARAM wParam, LPARAM lParam, LONG_PTR dwRef) -> HRESULT
                {
                    EnumData* data = reinterpret_cast<EnumData*>(dwRef);

                    switch (note)
                    {
                    case TDN_NAVIGATED:
                        DarkMode::AllowForTaskDialog(hwnd, data->pCfg, data->theme);
                        break;

                    case TDN_DESTROYED:
                        DarkMode::RemoveFromTaskDialog(hwnd);
                        delete data;
                        break;
                    }
                    return S_OK;
                };
            SendMessage(hwnd, TDM_NAVIGATE_PAGE, 0, (LPARAM)&configNext);
            return S_FALSE;
        }
        break;
    }

    case TDN_DESTROYED:
        DarkMode::RemoveFromTaskDialog(hwnd);
        RemoveProp(hwnd, L"IsExpanded");
        RemoveProp(hwnd, L"IsChecked");
        break;
    }
    return S_OK;
}

// ─── Dialog launchers ─────────────────────────────────────────────────────────
// Every launcher includes the three theme radio buttons in pRadioButtons.
// nDefaultRadioButton is set to match the current g_theme so the correct
// option appears pre-selected when the dialog opens.

static int ThemeToRadioId()
{
    switch (g_theme)
    {
    case DarkMode::Theme::Dark:  return RB_THEME_DARK;
    case DarkMode::Theme::Light: return RB_THEME_LIGHT;
    default:                     return RB_THEME_SYSTEM;
    }
}

// Shared radio button definitions — used by every launcher.
static const TASKDIALOG_BUTTON kThemeRadios[] = {
    { RB_THEME_SYSTEM, L"System\nFollow the OS dark-mode preference" },
    { RB_THEME_DARK,   L"Force Dark\nAlways dark regardless of OS"   },
    { RB_THEME_LIGHT,  L"Force Light\nAlways light regardless of OS" },
};

static void ShowSimpleDialog(HWND hwndParent)
{
    static const TASKDIALOG_BUTTON btns[] = {
        { IDOK,     L"Navigate\nNavigate to an RTL page"  },
        { IDCANCEL, L"Cancel\nClose without saving"       },
    };
    TASKDIALOGCONFIG cfg = {};
    cfg.cbSize = sizeof(cfg);
    cfg.hwndParent = hwndParent;
    cfg.hInstance = GetModuleHandleW(nullptr);
    cfg.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_USE_COMMAND_LINKS;
    cfg.pszWindowTitle = L"Dark TaskDialog \u2014 Simple";
    cfg.pszMainInstruction = L"Hello from a dark box!";
    cfg.pszContent = L"Select a theme below, then launch another "
        L"dialog to see it applied.";
    cfg.pszMainIcon = TD_WARNING_ICON;
    cfg.pfCallback = TDCallback;
    cfg.lpCallbackData = (LONG_PTR)&cfg;
    cfg.pButtons = btns;        cfg.cButtons = ARRAYSIZE(btns);
    cfg.pRadioButtons = kThemeRadios; cfg.cRadioButtons = ARRAYSIZE(kThemeRadios);
    cfg.nDefaultRadioButton = ThemeToRadioId();
    int n = 0, r = 0;
    TaskDialogIndirect(&cfg, &n, &r, nullptr);
}

static void ShowExpandoDialog(HWND hwndParent)
{
    static const TASKDIALOG_BUTTON btns[] = {
        { IDOK,     L"OK"     },
        { IDCANCEL, L"Cancel" },
    };
    TASKDIALOGCONFIG cfg = {};
    cfg.cbSize = sizeof(cfg);
    cfg.hwndParent = hwndParent;
    cfg.hInstance = GetModuleHandleW(nullptr);
    cfg.dwFlags = TDF_EXPAND_FOOTER_AREA |
        TDF_EXPANDED_BY_DEFAULT;
    cfg.pszWindowTitle = L"Dark TaskDialog \u2014 Expando";
    cfg.pszMainInstruction = L"Dialog with expandable details";
    cfg.pszContent = L"Main content area.";
    cfg.pszExpandedInformation = L"These are the expanded details.\r\n"
        L"More information here.";
    cfg.pszCollapsedControlText = L"Show details";
    cfg.pszExpandedControlText = L"Hide details";
    cfg.pszMainIcon = TD_INFORMATION_ICON;
    cfg.pfCallback = TDCallback;
    cfg.lpCallbackData = (LONG_PTR)&cfg;
    cfg.pButtons = btns;        cfg.cButtons = ARRAYSIZE(btns);
    cfg.pRadioButtons = kThemeRadios; cfg.cRadioButtons = ARRAYSIZE(kThemeRadios);
    cfg.nDefaultRadioButton = ThemeToRadioId();
    int n = 0, r = 0;
    TaskDialogIndirect(&cfg, &n, &r, nullptr);
}

static void ShowVerificationDialog(HWND hwndParent)
{
    static const TASKDIALOG_BUTTON links[] = {
        { IDYES, L"Yes, proceed\nContinue with the operation" },
        { IDNO,  L"No, cancel\nAbort and go back"            },
    };
    TASKDIALOGCONFIG cfg = {};
    cfg.cbSize = sizeof(cfg);
    cfg.hwndParent = hwndParent;
    cfg.hInstance = GetModuleHandleW(nullptr);
    cfg.dwFlags = TDF_USE_COMMAND_LINKS;
    cfg.pszWindowTitle = L"Dark TaskDialog \u2014 Verification";
    cfg.pszMainInstruction = L"Confirm your choice";
    cfg.pszContent = L"This dialog includes a verification checkbox "
        L"and a live theme selector.";
    cfg.pszVerificationText = L"Don\u2019t show this again";
    cfg.pszMainIcon = TD_SHIELD_ICON;
    cfg.pfCallback = TDCallback;
    cfg.lpCallbackData = (LONG_PTR)&cfg;
    cfg.pButtons = links;       cfg.cButtons = ARRAYSIZE(links);
    cfg.pRadioButtons = kThemeRadios; cfg.cRadioButtons = ARRAYSIZE(kThemeRadios);
    cfg.nDefaultRadioButton = ThemeToRadioId();
    int n = 0; BOOL chk = FALSE;
    TaskDialogIndirect(&cfg, &n, nullptr, &chk);
    if (chk) MessageBoxW(hwndParent, L"Checkbox was checked!", L"Result", MB_OK);
}

static void ShowErrorDialog(HWND hwndParent)
{
    static const TASKDIALOG_BUTTON btns[] = {
        { IDRETRY, L"Retry"  },
        { IDCANCEL,L"Cancel" },
    };
    TASKDIALOGCONFIG cfg = {};
    cfg.cbSize = sizeof(cfg);
    cfg.hwndParent = hwndParent;
    cfg.hInstance = GetModuleHandleW(nullptr);
    cfg.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_EXPAND_FOOTER_AREA;
    cfg.pszWindowTitle = L"Dark TaskDialog \u2014 Error";
    cfg.pszMainInstruction = L"An error has occurred";
    cfg.pszContent = L"Something went wrong. Check the details below.";
    cfg.pszExpandedInformation = L"Error code: 0x80070005\r\nAccess is denied.";
    cfg.pszFooter = L"See the <a href=\"help\">help documentation</a> "
        L"for more info.";
    cfg.pszFooterIcon = TD_INFORMATION_ICON;
    cfg.pszMainIcon = TD_ERROR_ICON;
    cfg.pfCallback = TDCallback;
    cfg.lpCallbackData = (LONG_PTR)&cfg;
    cfg.pButtons = btns;        cfg.cButtons = ARRAYSIZE(btns);
    cfg.pRadioButtons = kThemeRadios; cfg.cRadioButtons = ARRAYSIZE(kThemeRadios);
    cfg.nDefaultRadioButton = ThemeToRadioId();
    int n = 0, r = 0;
    TaskDialogIndirect(&cfg, &n, &r, nullptr);
}

static void ShowProgressDialog(HWND hwndParent)
{
    static const TASKDIALOG_BUTTON btns[] = { { IDOK, L"Close" } };
    static int s_pos = 0;

    TASKDIALOGCONFIG cfg = {};
    cfg.cbSize = sizeof(cfg);
    cfg.hwndParent = hwndParent;
    cfg.hInstance = GetModuleHandleW(nullptr);
    cfg.dwFlags = TDF_SHOW_PROGRESS_BAR | TDF_CALLBACK_TIMER |
        TDF_ENABLE_HYPERLINKS;
    cfg.pszWindowTitle = L"Dark TaskDialog \u2014 Progress";
    cfg.pszMainInstruction = L"Processing your request\u2026";
    cfg.pszContent = L"Demonstrating a determinate progress bar.\r\n"
        L"Switch theme live using the radio buttons below.";
    cfg.pszMainIcon = TD_INFORMATION_ICON;
    cfg.pButtons = btns;        cfg.cButtons = ARRAYSIZE(btns);
    cfg.pRadioButtons = kThemeRadios; cfg.cRadioButtons = ARRAYSIZE(kThemeRadios);
    cfg.nDefaultRadioButton = ThemeToRadioId();
    cfg.lpCallbackData = (LONG_PTR)&cfg;
    cfg.pfCallback =
        [](HWND hwnd, UINT note, WPARAM wParam, LPARAM, LONG_PTR dwRef) -> HRESULT
        {
            auto* pCfg = reinterpret_cast<TASKDIALOGCONFIG*>(dwRef);
            switch (note)
            {
            case TDN_CREATED:
                s_pos = 0;
                DarkMode::AllowForTaskDialog(hwnd, pCfg, g_theme);
                SendMessageW(hwnd, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, 100));
                break;
            case TDN_TIMER:
                if (s_pos < 100)
                {
                    s_pos = min(100, s_pos + 3);
                    SendMessageW(hwnd, TDM_SET_PROGRESS_BAR_POS, s_pos, 0);
                }
                break;
            case TDN_RADIO_BUTTON_CLICKED:
                switch ((int)wParam)
                {
                case RB_THEME_SYSTEM: g_theme = DarkMode::Theme::System; break;
                case RB_THEME_DARK:   g_theme = DarkMode::Theme::Dark;   break;
                case RB_THEME_LIGHT:  g_theme = DarkMode::Theme::Light;  break;
                }
                ReapplyTheme(hwnd, pCfg);
                break;
            case TDN_DESTROYED:
                DarkMode::RemoveFromTaskDialog(hwnd);
                break;
            }
            return S_OK;
        };
    int n = 0, r = 0;
    TaskDialogIndirect(&cfg, &n, &r, nullptr);
}

// ─── Main window ──────────────────────────────────────────────────────────────

#define ID_SIMPLE   101
#define ID_EXPANDO  102
#define ID_VERIFY   103
#define ID_ERROR    104
#define ID_PROGRESS 105

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        auto Btn = [&](const wchar_t* txt, int id, int y)
            {
                HWND h = CreateWindowExW(0, L"BUTTON", txt,
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    20, y, 260, 35, hwnd, (HMENU)(INT_PTR)id,
                    GetModuleHandleW(nullptr), nullptr);
                if (DarkMode::IsActive())
                {
                    SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
                }

            };
        Btn(L"Simple Dialog (icon + Command links)", ID_SIMPLE, 20);
        Btn(L"Expando Dialog (info + details)", ID_EXPANDO, 65);
        Btn(L"Verification Dialog (checkbox)", ID_VERIFY, 110);
        Btn(L"Error Dialog (footer link)", ID_ERROR, 155);
        Btn(L"Progress Dialog (timer + progress bar)", ID_PROGRESS, 200);
        break;
    }
    case WM_CTLCOLORBTN:
    {
        if (DarkMode::IsActive())
        {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, RGB(32, 32, 32));
            SetTextColor(hdc, RGB(224, 224, 224));
            SetBkMode(hdc, OPAQUE);
            static HBRUSH sBr = CreateSolidBrush(RGB(32, 32, 32));
            SetDCBrushColor(hdc, RGB(32, 32, 32));
            return (LRESULT)sBr;
        }

    }
    break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        if (DarkMode::IsActive())
        {
            HBRUSH backBrush = CreateSolidBrush(RGB(32, 32, 32));
            FillRect(hdc, &rcClient, backBrush);
            DeleteObject(backBrush);
        }
        EndPaint(hwnd, &ps);
    }
    return 0;
    case WM_SETTINGCHANGE:
        SendMessage(hwnd, WM_SYSCOLORCHANGE, 0, 0);
        break;
    case WM_SYSCOLORCHANGE:
    {
        {
            EnumChildWindows(hwnd, [](HWND hwndDuiChild, LPARAM) -> BOOL
                {

                    SetWindowTheme(hwndDuiChild, DarkMode::IsActive() ? L"DarkMode_Explorer" : NULL, NULL);
                    SendMessage(hwndDuiChild, WM_SYSCOLORCHANGE, 0, 0);

                    return TRUE;
                }, 0);
        }
        if (DarkMode::IsActive())
        {
            DarkMode::EnableForTLW(hwnd);
        }
        InvalidateRect(hwnd, NULL, TRUE);
    }
    break;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_SIMPLE:   ShowSimpleDialog(hwnd);       break;
        case ID_EXPANDO:  ShowExpandoDialog(hwnd);      break;
        case ID_VERIFY:   ShowVerificationDialog(hwnd); break;
        case ID_ERROR:    ShowErrorDialog(hwnd);        break;
        case ID_PROGRESS: ShowProgressDialog(hwnd);     break;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ─── WinMain ─────────────────────────────────────────────────────────────────
int WINAPI wWinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR pCmdLine, _In_ int nShow)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)NULL_BRUSH;
    wc.lpszClassName = L"DarkTDDemo";
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(0, L"DarkTDDemo",
        L"Dark TaskDialog Demo",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 320, 290,
        nullptr, nullptr, hInst, nullptr);
    if (DarkMode::IsActive())
    {
        DarkMode::EnableForTLW(g_hwnd);
        DarkMode::AllowForWindow(g_hwnd);
    }

    ShowWindow(g_hwnd, nShow);
    UpdateWindow(g_hwnd);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}