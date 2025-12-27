#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shlobj.h>
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <memory>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdiplus.lib")

using Microsoft::WRL::ComPtr;

// Capture modes
enum CaptureMode {
    MODE_RECTANGLE = 0,
    MODE_WINDOW,
    MODE_FULLSCREEN,
    MODE_TEXT
};

// Colors - Modern Windows 11 style
namespace Colors {
    const COLORREF Background = RGB(30, 30, 30);
    const COLORREF Surface = RGB(45, 45, 45);
    const COLORREF SurfaceHover = RGB(60, 60, 60);
    const COLORREF SurfaceActive = RGB(70, 70, 70);
    const COLORREF Accent = RGB(76, 194, 255);
    const COLORREF AccentHover = RGB(96, 205, 255);
    const COLORREF AccentDark = RGB(0, 120, 212);
    const COLORREF Text = RGB(255, 255, 255);
    const COLORREF TextSecondary = RGB(160, 160, 160);
    const COLORREF TextDim = RGB(120, 120, 120);
    const COLORREF Border = RGB(55, 55, 55);
    const COLORREF Divider = RGB(50, 50, 50);
    const COLORREF Success = RGB(108, 203, 95);
}

// Hotkey IDs
#define HOTKEY_RECTANGLE  1
#define HOTKEY_WINDOW     2
#define HOTKEY_FULLSCREEN 3
#define HOTKEY_PRINTSCREEN 4
#define HOTKEY_TEXT 5

// Tray icon
#define WM_TRAYICON (WM_USER + 200)
#define ID_TRAY_SHOW 3001
#define ID_TRAY_EXIT 3002

// UI Constants
const int TOOLBAR_HEIGHT = 48;
const int BUTTON_SIZE = 36;
const int BUTTON_MARGIN = 6;
const int WINDOW_WIDTH = 390;
const int WINDOW_HEIGHT = 56;

// Hotkey structure
struct HotkeyConfig {
    UINT modifiers = 0;
    UINT vk = 0;
    bool enabled = false;

    std::wstring GetString() const {
        if (vk == 0) return L"None";
        std::wstring result;
        if (modifiers & MOD_WIN) result += L"Win+";
        if (modifiers & MOD_CONTROL) result += L"Ctrl+";
        if (modifiers & MOD_ALT) result += L"Alt+";
        if (modifiers & MOD_SHIFT) result += L"Shift+";

        if (vk >= 'A' && vk <= 'Z') {
            result += (wchar_t)vk;
        } else if (vk >= '0' && vk <= '9') {
            result += (wchar_t)vk;
        } else if (vk >= VK_F1 && vk <= VK_F12) {
            result += L"F" + std::to_wstring(vk - VK_F1 + 1);
        } else if (vk == VK_SNAPSHOT) {
            result += L"PrtSc";
        }
        return result;
    }
};

// Settings structure
struct Settings {
    std::wstring savePath;
    bool autoSave = false;
    HotkeyConfig hotkeyRect = { MOD_CONTROL | MOD_SHIFT, 'S', true };
    HotkeyConfig hotkeyWindow = { MOD_CONTROL | MOD_SHIFT, 'W', false };
    HotkeyConfig hotkeyFullscreen = { MOD_CONTROL | MOD_SHIFT, 'F', false };
    HotkeyConfig hotkeyText = { MOD_CONTROL | MOD_SHIFT, 'T', false };
    bool replaceWindowsSnipping = false;
    bool runAtStartup = false;
};
