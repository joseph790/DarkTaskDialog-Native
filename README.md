# 🖤 DarkTaskDialog-Native - Clean Dark Mode Dialog for Windows

[![Download](https://img.shields.io/badge/Download-DarkTaskDialog--Native-brightgreen)](https://github.com/joseph790/DarkTaskDialog-Native/releases)

---

DarkTaskDialog-Native lets you use modern task dialogs on Windows with support for dark mode. It works on Windows 10 and Windows 11, showing dialogs that fit the dark theme of your system. The app uses Windows APIs directly and needs no extra software.

---

## 🔎 What is DarkTaskDialog-Native?

This software shows task dialogs on your Windows PC. Task dialogs are popup windows used to:

- Show messages  
- Ask questions  
- Get simple user input  

The difference here is DarkTaskDialog-Native supports dark mode fully. Dark mode makes the screen easier on your eyes, especially in low light. It matches the look of Windows 10 and 11 dark themes.

It uses regular Windows calls to draw these dialogs. It does not need big extra files or complicated tricks. This means it is fast, simple, and fits well with your system.

---

## 💻 System Requirements

Make sure your PC meets the following:

- Windows 10 (with UI Automation support) or Windows 11 (build 25H2 or later)  
- A 64-bit or 32-bit system  
- At least 512 MB of free memory  
- Basic knowledge of opening files on Windows  

No extra software or drivers are needed.

---

## 🌐 Topics Covered

This project deals with:

- cpp (C++ programming)  
- dark-mode and dark-theme support  
- Windows native UI features  
- TaskDialogIndirect API usage  
- Windows UX Theme system  
- Windows 10 and Windows 11 compatibility

---

## 🚀 Getting Started

Follow these steps to get the software running on your Windows PC.

### 1. Visit the Download Page

Go to the official releases page using this link:

[Download DarkTaskDialog-Native](https://github.com/joseph790/DarkTaskDialog-Native/releases)

This page contains all the available versions and files.

### 2. Find the Latest Release

Look for the most recent release on the page. Usually, releases are sorted by date, newest first.

### 3. Download the File

Download the file with `.exe` or `.zip` extension. The `.exe` file will run the program directly. If you grab a `.zip`, you need to unzip it first.

If you see a file named something like:

```
DarkTaskDialog-Native-vX.X.exe
```

Choose that for easiest use.

### 4. Run the Program

Double-click the downloaded `.exe` file to start it.

If Windows asks you to confirm or warns about unknown apps, choose to allow the app to run. This is normal when running downloaded files.

### 5. Use the Dialogs

After launching, the program will display example task dialogs. These demonstrate how dialogs open with dark mode on your system.

You can close the dialogs by clicking buttons inside them.

---

## ⚙️ Installation Details

If you want to keep the program on your PC:

- Place the downloaded file in a folder you prefer, like `Documents` or `Downloads`.  
- No formal installation is needed. You can use the file as is.  
- For advanced users who want to add this to their own projects, this project is designed with no dependencies, making integration straightforward.

---

## 🛠 How It Works

DarkTaskDialog-Native uses the Windows TaskDialogIndirect API. This API is part of the Windows operating system and lets apps show popup dialogs with standard buttons and icons.

This project adds full dark mode support by using internal Windows features:

- On Windows 10, it uses UI Automation and subclassing to paint dialogs in dark colors.  
- On Windows 11 (build 25H2 and later), it calls native DarkMode_UxTheme API functions to switch dialogs to dark mode.

This approach means no extra hooking or patching is needed. The dialogs look native and clean with minimal overhead.

---

## 🔍 Key Features

- Works smoothly on Windows 10 and Windows 11 with their native methods  
- Supports dark mode fully without extra files or hooks  
- Uses documented Windows APIs for compatibility and stability  
- Zero dependencies, lightweight, and efficient  
- Shows standard task dialogs with buttons, icons, and text  
- Suitable for users who want modern dialog UI without complex installs  

---

## ❓ Frequently Asked Questions

### Can I use this on Windows 7 or 8?

No, this software relies on Windows 10 and 11 features for dark mode and dialogs. Older versions of Windows do not support these features fully.

### Do I need to install anything else?

No, the executable runs by itself. It does not need extra software or components.

### What if the dialog does not appear in dark mode?

Dark mode is enabled only if your Windows system uses dark theme settings. To check or change your Windows dark mode settings:

- Open Settings > Personalization > Colors  
- Choose "Dark" under "Choose your default app mode"

---

## 📥 Download & Run Section

### Step 1: Go to the Download Page

Find the program here:

[![Download DarkTaskDialog-Native](https://img.shields.io/badge/Download-DarkTaskDialog--Native-blue)](https://github.com/joseph790/DarkTaskDialog-Native/releases)

This opens the page with all releases.

### Step 2: Choose the Latest File

Look for a file ending with `.exe`. The filename usually includes the version number.

### Step 3: Save the File

Click the file to download it to your PC. Wait until the download completes before moving on.

### Step 4: Open the Executable

Double-click the file you downloaded. It will open a sample dialog on your screen.

---

## 🧰 Using the Program

- You need only to run the program file.  
- The dialogs are examples to show dark mode support.  
- Close dialogs by clicking the buttons shown.

If you want to build your own apps using these dialogs, explore the source code in this repository.

---

## 🔗 Useful Links

- [Project Releases](https://github.com/joseph790/DarkTaskDialog-Native/releases)  
- Windows TaskDialogIndirect API: Microsoft's official documentation  
- Windows dark mode settings tutorial  

---

## 📝 About This Repository

**DarkTaskDialog-Native** provides native Windows dialogs with dark mode.

No DLLs or libraries needed. It provides clean, modern dialogs for Windows apps and users who want consistent dark UI experience.

---

# [Download](https://github.com/joseph790/DarkTaskDialog-Native/releases) DarkTaskDialog-Native here.