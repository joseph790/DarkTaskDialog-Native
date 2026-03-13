# DarkTaskDialog-Native

> Win32 `TaskDialogIndirect` with complete dark mode support —
> **zero dependencies · zero hooks · documented APIs only · MIT licensed.**
> Windows 10 (UIA + subclassing + owner-draw) and Windows 11 (native `DarkMode_TaskDialog` panels + owner-drawn text).

[![Platform](https://img.shields.io/badge/platform-Windows%2010%2B-blue?logo=windows&logoColor=white)](https://docs.microsoft.com/windows)
[![Language](https://img.shields.io/badge/language-C%2B%2B17-blue?logo=cplusplus)](https://en.cppreference.com)
[![License](https://img.shields.io/badge/license-MIT-brightgreen)](LICENSE)
[![Dependencies](https://img.shields.io/badge/dependencies-none-brightgreen)]()
[![API hooks](https://img.shields.io/badge/API%20hooks-none-brightgreen)]()
[![Undocumented APIs](https://img.shields.io/badge/undocumented%20APIs-none-brightgreen)]()

---

## Screenshots

| Progress dialog | Expando | Expando + footer | Command links | Rtl + Nave |
|:---:|:---:| :---:| :---:| :---:| 
| ![Progress dark](docs/screenshot-progress.png) | ![Expando dark](docs/screenshot-expando.png) | ![Expando dark](docs/screenshot-expando_1.png) | ![Command links](docs/Screenshot-Command_links.png) | ![Command links](docs/Screenshot-Nave.png) | 

--- 
The only other public solution —
[SFTRS/DarkTaskDialog](https://github.com/SFTRS/DarkTaskDialog) — hooks
`DrawTheme*` APIs via **Microsoft Detours**. That requires a third-party build
dependency and is GPL-3.0 licensed.

This library uses **UI Automation**, **window subclassing**, **UxTheme**, and
**`DrawThemeTextEx`** — all fully documented Win32 APIs — and adds features the
Detours approach cannot support:

| | SFTRS/DarkTaskDialog | **DarkTaskDialog-Native** |
|---|:---:|:---:|
| Dependency | Microsoft Detours | **None** |
| Approach | `DrawTheme*` API hooking | UIA + subclassing + UxTheme |
| Documented APIs only | ✅ | ✅ |
| License | GPL-3.0 | **MIT** |
---

## Quick start

### Requirements

| | Version |
|---|---|
| Windows SDK | 10.0.19041+ |
| Visual Studio | 2022 (v143) |
| C++ standard | C++17 |
| Target OS | Windows 10 1809+ (build 17763) |

### Integration

**1.** Copy `DarkMode.h` and `DarkMode.cpp` into your project.

**2.** Call once at startup, before any windows are created:

```cpp
#include "DarkMode.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    DarkMode::Init();   // reads OS dark-mode state; safe no-op on pre-Win10
    // ...
}
```

**3.** Add two lines to your `TaskDialogIndirect` callback:

```cpp
static HRESULT CALLBACK MyCallback(
    HWND hwnd, UINT note, WPARAM wParam, LPARAM lParam, LONG_PTR dwRef)
{
    auto* pCfg = reinterpret_cast<TASKDIALOGCONFIG*>(dwRef);
    switch (note)
    {
    case TDN_CREATED:
        DarkMode::AllowForTaskDialog(hwnd, pCfg);  // ← applies dark mode
        break;
    case TDN_NAVIGATED:
        // Required when using TDM_NAVIGATE_PAGE
        DarkMode::AllowForTaskDialog(hwnd, reinterpret_cast<TASKDIALOGCONFIG*>(lParam));
        break;
    case TDN_DESTROYED:
        DarkMode::RemoveFromTaskDialog(hwnd);       // ← frees per-dialog state
        break;
    }
    return S_OK;
}

TASKDIALOGCONFIG cfg = { sizeof(cfg) };
cfg.pfCallback     = MyCallback;
cfg.lpCallbackData = (LONG_PTR)&cfg;
TaskDialogIndirect(&cfg, &nButton, nullptr, nullptr);
```

If the system is in light mode every call is a no-op.
If the user switches themes while the dialog is open it adapts automatically.

---

## API reference

```cpp
namespace DarkMode
{
    // Call once at startup. Reads OS theme state.
    bool Init();

    // True when the OS is currently in dark mode.
    bool IsActive();

    // True on Windows 11 where DarkMode_TaskDialog UxTheme class exists.
    bool HasNativeTaskDialogTheme();

    // Sets DWMWA_USE_IMMERSIVE_DARK_MODE on a top-level window (dark title bar).
    void EnableForTLW(HWND hwnd);

    // Applies SetWindowTheme to any child control.
    void AllowForWindow(HWND hwnd, const wchar_t* themeClass = nullptr);

    // Main entry point. Call from TDN_CREATED (and TDN_NAVIGATED for page nav).
    void AllowForTaskDialog(HWND hwndTaskDialog, TASKDIALOGCONFIG* pConfig);

    // Call from TDN_DESTROYED to free per-dialog state.
    void RemoveFromTaskDialog(HWND hwndTaskDialog);
}
```

---

## How it works

### The comctl32 UIFILE bug — why text needs owner-draw on every Windows version

The TaskDialog layout engine reads its colours from a stylesheet embedded in
`comctl32.dll` as resource 4255 (`UIFILE`). Every text element in that
stylesheet queries colour via:

```
foreground="gtc(TaskDialogStyle, <part>, 0, 3803)"
```

The key is the class name: **`TaskDialogStyle`** — not `DarkMode_TaskDialogStyle`.
`DarkMode_TaskDialogStyle` does not exist in any shipping version of Windows.
Because `TaskDialogStyle` always returns light-mode colours, every text element
paints black-on-dark even when the panel backgrounds are correctly dark.


### Windows 11 

1- `DarkMode_TaskDialog` **does** exist — it covers all panel backgrounds and the
expando glyph. `AllowForTaskDialog` walks the `DirectUIHWND` child tree by its `AutomationId`, calls
`SetWindowTheme(pane, L"DarkMode_TaskDialog", nullptr)` 
2- Text is then owner-drawn to
fix the `TaskDialogStyle` colour bug described above.

### Windows 10 (build 17763–19045)

`DarkMode_TaskDialog` does not exist. The library:

1. Uses **UI Automation** to walk the `DirectUIHWND` child tree, identifying
   each element by its `AutomationId` (the UIFILE atom names).
2. Owner-draws all elements.

| Element | Windows 10 | Windows 11 |
|---|---|---|
| `PrimaryPanel` background | `WM_ERASEBKGND` dark brush | `dtb(DarkMode_TaskDialog, 1, 0)` ✅ |
| `SecondaryPanel` background | `WM_ERASEBKGND` dark brush | `dtb(DarkMode_TaskDialog, 8, 0)` ✅ |
| `FootnoteArea` background | `WM_ERASEBKGND` dark brush | `dtb(DarkMode_TaskDialog, 15, 0)` ✅ |
| `SeparatorLine` background | dark brush | `dtb(DarkMode_TaskDialog, 17, 0)` ✅ |
| Expando glyph | `dtb(TaskDialog, 13, state)` | `dtb(DarkMode_TaskDialog, 13, state)` ✅ |
| **All text** (`gtc(TaskDialogStyle,*,*)`) | `FillRect` + `DrawThemeTextEx` + `DTT_TEXTCOLOR` | `DrawThemeText` + `DTT_TEXTCOLOR` |

---

## UIA element reference

These `AutomationId` strings come directly from the comctl32 UIFILE (resource
4255, Windows 11 build 26100.7965). They are the **atom names** the DirectUI
engine uses internally and are exactly what `IUIAutomationElement::get_CurrentAutomationId`
returns when walking the `DirectUIHWND` tree.

> **This is the only public cross-reference of these identifiers against their
> UxTheme part IDs and resolved dark-mode colour values.**

### Root

| AutomationId | Description |
|---|---|
| `"TaskDialog"` | The `DirectUIHWND` TaskPage window — UIA walk entry point |

### Primary panel

| AutomationId | UIFILE atom | Control type | UxTheme query | Dark colour |
|---|---|---|---|---|
| `MainIcon` | `atom(MainIcon)` | Image | — | No recolour |
| `MainInstruction` | `atom(MainInstruction)` | Text | `gtc(TaskDialogStyle, 2, 0, 3803)` | `RGB(153, 235, 255)` |
| `ContentText` | `atom(ContentText)` | Text | `gtc(TaskDialogStyle, 4, 0, 3803)` | `RGB(255, 255, 255)` |
| `ContentLink` | `atom(ContentLink)` | Hyperlink | `gtc(TaskDialogStyle, 4, 0, 3803)` + `dtb(TaskDialog,1,0)` bg | `RGB(255, 255, 255)` |
| `ExpandedInformationText` | `atom(ExpandedInformationText)` | Text | `gtc(TaskDialogStyle, 6, 0, 3803)` | `RGB(255, 255, 255)` |
| `ExpandedInformationLink` | `atom(ExpandedInformationLink)` | Hyperlink | `gtc(TaskDialogStyle, 6, 0, 3803)` + `dtb(TaskDialog,1,0)` bg | `RGB(255, 255, 255)` |
| `ExpandoButton` | `atom(ExpandoButton)` | Button | `dtb(TaskDialog, 13, state)` | Owner-drawn glyph |
| `ExpandoTextExpanded` | `atom(ExpandoTextExpanded)` | Text | `gtc(TaskDialogStyle, 12, 0, 3803)` | `RGB(255, 255, 255)` |
| `ExpandoTextCollapsed` | `atom(ExpandoTextCollapsed)` | Text | `gtc(TaskDialogStyle, 12, 0, 3803)` | `RGB(255, 255, 255)` |
| `VerificationCheckBox` | `atom(VerificationCheckBox)` | CheckBox | — | System-themed |
| `VerificationText` | `atom(VerificationText)` | Text | `gtc(TaskDialogStyle, 14, 0, 3803)` | `RGB(255, 255, 255)` |
| `RadioButton_0` … `_N` | `class RadioButton` | RadioButton | `dtb(TaskDialog, 1, 0)` bg | System-themed |
| `CommandLink_0` … `_N` | `class CommandLink` | Button | `dtb(TaskDialog, 1, 0)` bg | `DarkMode_Explorer` theme |
| `CommandButton_0` … `_N` | `class CommandButton` | Button | — | `DarkMode_Explorer` theme |
| `ProgressBar` | `atom(ProgressBar)` | ProgressBar | — | System-themed |

### Secondary panel (button row)

| AutomationId | UIFILE atom | UxTheme query | Dark colour |
|---|---|---|---|
| *(push buttons)* | `class CommandButton` | `dtb(TaskDialog, 8, 0)` bg | `DarkMode_Explorer` / `DarkMode_CFD` |
| `ButtonArea` | `atom(ButtonArea)` | `gtc(TaskDialogStyle, **15**, 0, 3803)` ⚠️ | Uses footnote part — UIFILE quirk |

### Separator

| AutomationId | UIFILE atom | UxTheme query | Dark colour |
|---|---|---|---|
| `Separator` | `atom(Separator)` | `dtb(TaskDialog, 15, 0)` bg | `RGB(44, 44, 44)` |
| `SeparatorLine` | `atom(SeparatorLine)` | `dtb(TaskDialog, 17, 0)` bg | `RGB(77, 77, 77)` |

### Footnote / expanded footer panel

| AutomationId | UIFILE atom | UxTheme query | Dark colour |
|---|---|---|---|
| `FootnoteIcon` | `atom(FootnoteIcon)` | — | No recolour |
| `FootnoteText` | `atom(FootnoteText)` | `gtc(TaskDialogStyle, 15, 0, 3803)` | `RGB(224, 224, 224)` |
| `FootnoteTextLink` | `atom(FootnoteTextLink)` | `gtc(TaskDialogStyle, 15, 0, 3803)` | `RGB(224, 224, 224)` |
| `ExpandedFooterText` | `atom(ExpandedFooterText)` | `gtc(TaskDialogStyle, 18, 0, 3803)` | `RGB(224, 224, 224)` |
| `ExpandedFooterTextLink` | `atom(ExpandedFooterTextLink)` | `gtc(TaskDialogStyle, **4**, 0, 3803)` ⚠️ | `RGB(224, 224, 224)` |

> ⚠️ **`ExpandedFooterTextLink` uses part 4** (the content text part) instead
> of part 18 in the UIFILE. This is a Microsoft bug — the wrong part ID is
> hardcoded. The library handles this by checking the `AutomationId` and
> applying part-18 colours regardless of what `gtc()` returns.

---

## FAQ

**Does this work with the simple `TaskDialog()` overload?**
`TaskDialog()` has no callback, so there is no `TDN_CREATED` hook point.
Use `TaskDialogIndirect()` with a `TASKDIALOGCONFIG`.

**Does `TDM_NAVIGATE_PAGE` work?**
Yes — call `DarkMode::AllowForTaskDialog(hwnd, pNewConfig)` from `TDN_NAVIGATED`.
The included `main.cpp` demonstrates page navigation to an Arabic RTL page.

**I see a white flash when the dialog first opens.**
Ensure `DarkMode::AllowForTaskDialog` is called from `TDN_CREATED`, not
`TDN_DIALOG_CONSTRUCTED`. `TDN_CREATED` fires after the window is fully
initialised.

**Can I use this from MFC?**
Yes — no MFC dependency. Override `DoMessageBox` and call
`TaskDialogIndirect` directly. `DarkMode::Init()` can go in `InitInstance`.
---


## Related

### Tools

- [memoarfaa/TaskDialog-Stylesheet-Dumper](https://github.com/memoarfaa/TaskDialog-Stylesheet-Dumper) —
  Win32 tool that extracts and parses `comctl32.dll` resource 4255 (`UIFILE`) at
  runtime, then evaluates every `gtf()`, `gtc()`, `gtmar()`, `gtmet()`, and
  `dtb()` call live against `OpenThemeData(L"TaskDialog")` and
  `OpenThemeData(L"TaskDialogStyle")`. The resolved colour table and UIFILE
  bug findings documented in this README were produced with this tool.
  Uses only `IXmlReader`, `FindResourceW`, and `GetThemeColor` — no third-party
  dependencies. Output can be saved as XML.

### Other implementations

- [SFTRS/DarkTaskDialog](https://github.com/SFTRS/DarkTaskDialog) —
  alternative approach using Microsoft Detours to hook `DrawTheme*` APIs (GPL-3.0)

### References

- [Stack Overflow #79403975: Dark Mode Task Dialog](https://stackoverflow.com/questions/79403975/) 
- [TaskDialogIndirect — Win32 docs](https://docs.microsoft.com/windows/win32/api/commctrl/nf-commctrl-taskdialogindirect)

---

## License

MIT — see [LICENSE](LICENSE).
