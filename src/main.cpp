///////////////////////////////////////////////////////////////////////////////
// main.cpp  —  Dark TaskDialog demo  (Win32 / VS2022)
///////////////////////////////////////////////////////////////////////////////

#include "DarkMode.h"
#include <strsafe.h>

#pragma comment(linker,"/manifestdependency:\"type='win32' "\
    "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' "\
    "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ─── Shared TaskDialog callback ───────────────────────────────────────────────
// Syncs expand / check state into window props so DarkMode can read them.
// Props must match exactly what DarkMode.cpp reads:  "IsExpanded", "IsChecked"
TASKDIALOGCONFIG* pCfg1 = NULL;
static HRESULT CALLBACK TDCallback(
    HWND hwnd, UINT note, WPARAM wParam, LPARAM lParam, LONG_PTR dwRef)
{
    TASKDIALOGCONFIG* pCfg = reinterpret_cast<TASKDIALOGCONFIG*>(dwRef);

    switch (note)
    {
    case TDN_CREATED:
        // Seed initial state from config flags
        SetProp(hwnd, L"IsExpanded",
            (HANDLE)(LONG_PTR)((pCfg->dwFlags & TDF_EXPANDED_BY_DEFAULT) ? 1 : 0));
        SetProp(hwnd, L"IsChecked",
            (HANDLE)(LONG_PTR)((pCfg->dwFlags & TDF_VERIFICATION_FLAG_CHECKED) ? 1 : 0));
        DarkMode::AllowForTaskDialog(hwnd, pCfg);
        break;
    case TDM_NAVIGATE_PAGE:
    {
        OutputDebugString(L"TDM_NAVIGATE_PAGE:");
        pCfg = reinterpret_cast<TASKDIALOGCONFIG*>(lParam);
        DarkMode::AllowForTaskDialog(hwnd, pCfg);
        break;
    }
    case TDN_NAVIGATED:
    {
        OutputDebugString(L"TDN_NAVIGATED");
        DarkMode::AllowForTaskDialog(hwnd, pCfg);
        break;
    }

    case TDN_EXPANDO_BUTTON_CLICKED:
        // wParam: 1 = now expanded, 0 = now collapsed
        SetProp(hwnd, L"IsExpanded", (HANDLE)(LONG_PTR)(wParam ? 1 : 0));
        //// Invalidate DirectUI so it redraws the glyph with the new state
        //{
        //    HWND hChild = FindWindowExW(hwnd, nullptr, L"TaskDialog", nullptr);
        //    if (hChild) InvalidateRect(hChild, nullptr, FALSE);
        //}
        break;

    case TDN_VERIFICATION_CLICKED:
        // wParam: 1 = now checked, 0 = now unchecked  (NOT always-false)
        SetProp(hwnd, L"IsChecked", (HANDLE)(LONG_PTR)(wParam ? 1 : 0));
        /*{
            HWND hChild = FindWindowExW(hwnd, nullptr, L"TaskDialog", nullptr);
            if (hChild) InvalidateRect(hChild, nullptr, FALSE);
        }*/
        break;
    case   TDN_HYPERLINK_CLICKED:
    {
        LPCWSTR pszHREF = (LPCWSTR)lParam;
        // Use ShellExecute to open the URL in the default browser
        ShellExecuteW(hwnd, L"open", pszHREF, NULL, NULL, SW_SHOWNORMAL);
        break;
    }
    case TDN_DESTROYED:
        DarkMode::RemoveFromTaskDialog(hwnd);
        RemoveProp(hwnd, L"IsExpanded");
        RemoveProp(hwnd, L"IsChecked");
        break;
        case  TDN_BUTTON_CLICKED:
        {
            int buttonId = (int)wParam;

            if (buttonId == IDOK) {
                TASKDIALOGCONFIG configNext = { sizeof(TASKDIALOGCONFIG) };
                configNext.dwFlags = TDF_RTL_LAYOUT | TDF_ALLOW_DIALOG_CANCELLATION | TDF_ENABLE_HYPERLINKS;
                configNext.pszWindowTitle = L"عنوان النافذة";
                configNext.pszMainInstruction = L"تم الانتقال بنجاح!";
                configNext.pszContent = L"هذه المحتويات الجديدة مع اضافة اتجاة العربية من اليمين لليسار. \r\n انظر الي <a href=\"https://learn.microsoft.com/en-us/dynamics365/fin-ops-core/dev-itpro/user-interface/bidirectional-support\">دعم اللغة من اليمين إلى اليسار و النص ثنائي الاتجاه</a> لمعلومات أكثر.....";
                configNext.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;
                configNext.pszMainIcon = TD_INFORMATION_ICON;
                configNext.pfCallback = [](HWND hwnd, UINT note, WPARAM, LPARAM, LONG_PTR dwRef) -> HRESULT
                    {
                   
                        switch (note)
                        {
                        case TDN_DIALOG_CONSTRUCTED:
                            pCfg1  = (TASKDIALOGCONFIG*)dwRef;
                            break;
                        case TDN_NAVIGATED:
                        {
                            if (pCfg1)
                            {
                                DarkMode::AllowForTaskDialog(hwnd, pCfg1);
                                break;
                            }
                        }
                        case TDN_DESTROYED:
                            DarkMode::RemoveFromTaskDialog(hwnd);
                            break;
                        }
                        return S_OK;
                    };
                configNext.lpCallbackData = (LONG_PTR)&configNext;
                SendMessage(hwnd, TDM_NAVIGATE_PAGE, 0, (LPARAM)&configNext);
                return S_FALSE;
            }
            break;
        }
    }
    return S_OK;
}

// ─── Dialog launchers — all use stack-local TASKDIALOGCONFIG ─────────────────

static void ShowSimpleDialog(HWND hwndParent)
{
    static const TASKDIALOG_BUTTON btns[] = {
        { IDOK,     L"OK\nAccept and close"         },
        { IDCANCEL, L"Cancel\nClose without saving"  },
    };
    TASKDIALOGCONFIG cfg = {};
    cfg.cbSize = sizeof(cfg);
    cfg.hwndParent = hwndParent;
    cfg.hInstance = GetModuleHandleW(nullptr);
    cfg.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_USE_COMMAND_LINKS;
    cfg.pszWindowTitle = L"Dark TaskDialog \u2014 Simple";
    cfg.pszMainInstruction = L"Hello from a dark box!";
    cfg.pszContent = L"Demonstrates dark mode on a standard TaskDialog.";
    cfg.pszMainIcon = TD_WARNING_ICON;
    cfg.pfCallback = TDCallback;
    cfg.lpCallbackData = (LONG_PTR)&cfg;
    cfg.pButtons = btns;
    cfg.cButtons = ARRAYSIZE(btns);
    cfg.nDefaultButton = IDOK;
    int n = 0;
    TaskDialogIndirect(&cfg, &n, nullptr, nullptr);
}

static void ShowExpandoDialog(HWND hwndParent)
{

    static const TASKDIALOG_BUTTON btns[] = { {IDOK,L"OK"}, {IDCANCEL,L"Cancel"} };
    static const TASKDIALOG_BUTTON radio[] = { {30,L"Option A"}, {31,L"Option B"} };

    TASKDIALOGCONFIG cfg = {};
    cfg.cbSize = sizeof(cfg);
    cfg.hwndParent = hwndParent;
    cfg.hInstance = GetModuleHandleW(nullptr);
    cfg.dwFlags = TDF_EXPAND_FOOTER_AREA | TDF_EXPANDED_BY_DEFAULT;
    cfg.pszWindowTitle = L"Dark TaskDialog - Expando";
    cfg.pszMainInstruction = L"Dialog with expandable details";
    cfg.pszContent = L"Main content area.";
    cfg.pszExpandedInformation = L"These are the expanded details.\r\nMore information here.";
    cfg.pszCollapsedControlText = L"Show details";
    cfg.pszExpandedControlText = L"Hide details";
    cfg.pszMainIcon = TD_INFORMATION_ICON;
    cfg.pfCallback = TDCallback;
    cfg.lpCallbackData = (LONG_PTR)&cfg;
    cfg.pButtons = btns;  cfg.cButtons = ARRAYSIZE(btns);
    cfg.pRadioButtons = radio; cfg.cRadioButtons = ARRAYSIZE(radio);
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
    cfg.pszContent = L"This dialog includes a verification checkbox.";
    cfg.pszVerificationText = L"Don\u2019t show this again";
    cfg.pszMainIcon = TD_SHIELD_ICON;
    cfg.pfCallback = TDCallback;
    cfg.lpCallbackData = (LONG_PTR)&cfg;
    cfg.pButtons = links; cfg.cButtons = ARRAYSIZE(links);
    int n = 0; BOOL chk = FALSE;
    TaskDialogIndirect(&cfg, &n, nullptr, &chk);
    if (chk) MessageBoxW(hwndParent, L"Checkbox was checked!", L"Result", MB_OK);
}

static void ShowErrorDialog(HWND hwndParent)
{
    static const TASKDIALOG_BUTTON btns[] = { {IDRETRY,L"Retry"}, {IDCANCEL,L"Cancel"} };
    TASKDIALOGCONFIG cfg = {};
    cfg.cbSize = sizeof(cfg);
    cfg.hwndParent = hwndParent;
    cfg.hInstance = GetModuleHandleW(nullptr);
    cfg.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_EXPAND_FOOTER_AREA;
    cfg.pszWindowTitle = L"Dark TaskDialog \u2014 Error";
    cfg.pszMainInstruction = L"An error has occurred";
    cfg.pszContent = L"Something went wrong. Check the details below.";
    cfg.pszExpandedInformation = L"Error code: 0x80070005\r\nAccess is denied.";
    cfg.pszFooter = L"See the <a href=\"help\">help documentation</a> for more info.";
    cfg.pszFooterIcon = TD_INFORMATION_ICON;
    cfg.pszMainIcon = TD_ERROR_ICON;
    cfg.pfCallback = TDCallback;
    cfg.lpCallbackData = (LONG_PTR)&cfg;
    cfg.pButtons = btns; cfg.cButtons = ARRAYSIZE(btns);
    int n = 0;
    TaskDialogIndirect(&cfg, &n, nullptr, nullptr);
}

static void ShowProgressDialog(HWND hwndParent)
{
    static const TASKDIALOG_BUTTON btns[] = { {IDOK, L"Close"} };
    static int s_pos = 0;

    TASKDIALOGCONFIG cfg = {};
    cfg.cbSize = sizeof(cfg);
    cfg.hwndParent = hwndParent;
    cfg.hInstance = GetModuleHandleW(nullptr);
    cfg.dwFlags = TDF_SHOW_PROGRESS_BAR | TDF_CALLBACK_TIMER | TDF_ENABLE_HYPERLINKS;
    cfg.pszWindowTitle = L"Dark TaskDialog \u2014 Progress";
    cfg.pszMainInstruction = L"Processing your request\u2026";
    cfg.pszContent =  L"Demonstrating a determinate progress bar. \r\n See the <a href=\"help\">help documentation</a> for more info.";
    cfg.pszMainIcon = TD_INFORMATION_ICON;
    cfg.pButtons = btns; cfg.cButtons = ARRAYSIZE(btns);
    cfg.lpCallbackData = (LONG_PTR)&cfg;
    cfg.pfCallback = [](HWND hwnd, UINT note, WPARAM, LPARAM, LONG_PTR dwRef) -> HRESULT
        {
            auto* pCfg = (TASKDIALOGCONFIG*)dwRef;
            switch (note)
            {
            case TDN_CREATED:
                s_pos = 0;
                DarkMode::AllowForTaskDialog(hwnd, pCfg);
                SendMessageW(hwnd, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, 100));
                break;
            case TDN_TIMER:
                if (s_pos < 100) { s_pos = std::min(100, s_pos + 3); SendMessageW(hwnd, TDM_SET_PROGRESS_BAR_POS, s_pos, 0); }
                break;
            case TDN_DESTROYED:
                DarkMode::RemoveFromTaskDialog(hwnd);
                break;
            }
            return S_OK;
        };
    int n = 0;
    TaskDialogIndirect(&cfg, &n, nullptr, nullptr);
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
        auto Btn = [&](const wchar_t* txt, int id, int y) {
            HWND h = CreateWindowExW(0, L"BUTTON", txt,
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                20, y, 260, 35, hwnd, (HMENU)(INT_PTR)id,
                GetModuleHandleW(nullptr), nullptr);
            SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
            };
        Btn(L"Simple Dialog (icon + Command links)", ID_SIMPLE, 20);
        Btn(L"Expando Dialog (info + radio buttons)", ID_EXPANDO, 65);
        Btn(L"Verification Dialog (shield + checkbox)", ID_VERIFY, 110);
        Btn(L"Error Dialog (error icon + footer link)", ID_ERROR, 155);
        Btn(L"Progress Dialog (progress bar)", ID_PROGRESS, 200);
        break;
    }
    case WM_CTLCOLORBTN:
    {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(32, 32, 32));
        SetTextColor(hdc, RGB(224, 224, 224));
        static HBRUSH sBr = CreateSolidBrush(RGB(32, 32, 32));
        return (LRESULT)sBr;
    }
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
        PostQuitMessage(0); break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ─── WinMain ─────────────────────────────────────────────────────────────────

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow)
{
    // COM initialised once for the UI thread; reused by all UIA calls in DarkMode
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    DarkMode::Init();

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(32, 32, 32));
    wc.lpszClassName = L"DarkTDDemo";
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"DarkTDDemo",
        L"Dark TaskDialog Demo",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 320, 290,
        nullptr, nullptr, hInst, nullptr);

    DarkMode::EnableForTLW(hwnd);
    DarkMode::AllowForWindow(hwnd);
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg); DispatchMessage(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}