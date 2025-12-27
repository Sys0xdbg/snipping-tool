#include "hotkeys.h"
#include "app_state.h"

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_app.settings.replaceWindowsSnipping) {
        KBDLLHOOKSTRUCT* pKey = (KBDLLHOOKSTRUCT*)lParam;

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            // Check for 'S' key
            if (pKey->vkCode == 'S') {
                // Check if Win and Shift are held
                bool winPressed = (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
                bool shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000);
                bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000);
                bool altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000);

                // Win+Shift+S without Ctrl or Alt
                if (winPressed && shiftPressed && !ctrlPressed && !altPressed) {
                    // Post message to main window to trigger capture
                    PostMessageW(g_app.mainWnd, WM_TRIGGER_CAPTURE, MODE_RECTANGLE, 0);
                    // Block the key from reaching Windows Snipping Tool
                    return 1;
                }
            }
        }
    }
    return CallNextHookEx(g_app.keyboardHook, nCode, wParam, lParam);
}

void InstallKeyboardHook() {
    if (g_app.keyboardHook == nullptr) {
        g_app.keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, g_app.hInstance, 0);
    }
}

void UninstallKeyboardHook() {
    if (g_app.keyboardHook != nullptr) {
        UnhookWindowsHookEx(g_app.keyboardHook);
        g_app.keyboardHook = nullptr;
    }
}

void ApplyWindowsSnippingReplacement(bool enable) {
    if (enable) {
        InstallKeyboardHook();
    } else {
        UninstallKeyboardHook();
    }

    // Set registry key to disable/enable Windows Print Screen snipping
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Keyboard", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        DWORD value = enable ? 0 : 1;
        RegSetValueExW(hKey, L"PrintScreenKeyForSnippingEnabled", 0, REG_DWORD, (BYTE*)&value, sizeof(value));
        RegCloseKey(hKey);
    }
}

bool IsWinShiftS(const HotkeyConfig& hk) {
    return (hk.modifiers == (MOD_WIN | MOD_SHIFT) && hk.vk == 'S');
}

void RegisterHotkeys() {
    UnregisterHotKey(g_app.mainWnd, HOTKEY_RECTANGLE);
    UnregisterHotKey(g_app.mainWnd, HOTKEY_WINDOW);
    UnregisterHotKey(g_app.mainWnd, HOTKEY_FULLSCREEN);
    UnregisterHotKey(g_app.mainWnd, HOTKEY_PRINTSCREEN);

    // Print Screen hotkey - always enabled
    RegisterHotKey(g_app.mainWnd, HOTKEY_PRINTSCREEN, MOD_NOREPEAT, VK_SNAPSHOT);

    // Rectangle hotkey
    if (g_app.settings.hotkeyRect.enabled && g_app.settings.hotkeyRect.vk != 0) {
        if (g_app.settings.replaceWindowsSnipping && IsWinShiftS(g_app.settings.hotkeyRect)) {
            // Skip - keyboard hook handles this
        } else {
            BOOL result = RegisterHotKey(g_app.mainWnd, HOTKEY_RECTANGLE,
                g_app.settings.hotkeyRect.modifiers | MOD_NOREPEAT,
                g_app.settings.hotkeyRect.vk);
            if (!result) {
                MessageBoxW(nullptr, L"Failed to register Rectangle hotkey.\nIt may already be in use by another application.",
                    L"Hotkey Registration Failed", MB_ICONWARNING);
            }
        }
    }
    if (g_app.settings.hotkeyWindow.enabled && g_app.settings.hotkeyWindow.vk != 0) {
        BOOL result = RegisterHotKey(g_app.mainWnd, HOTKEY_WINDOW,
            g_app.settings.hotkeyWindow.modifiers | MOD_NOREPEAT,
            g_app.settings.hotkeyWindow.vk);
        if (!result) {
            MessageBoxW(nullptr, L"Failed to register Window hotkey.\nIt may already be in use by another application.",
                L"Hotkey Registration Failed", MB_ICONWARNING);
        }
    }
    if (g_app.settings.hotkeyFullscreen.enabled && g_app.settings.hotkeyFullscreen.vk != 0) {
        BOOL result = RegisterHotKey(g_app.mainWnd, HOTKEY_FULLSCREEN,
            g_app.settings.hotkeyFullscreen.modifiers | MOD_NOREPEAT,
            g_app.settings.hotkeyFullscreen.vk);
        if (!result) {
            MessageBoxW(nullptr, L"Failed to register Fullscreen hotkey.\nIt may already be in use by another application.",
                L"Hotkey Registration Failed", MB_ICONWARNING);
        }
    }
}

void EnableDarkMode(HWND hwnd) {
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode));

    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));

    COLORREF captionColor = Colors::Background;
    DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));
}
