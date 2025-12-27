#pragma once

#include "common.h"

#define WM_TRIGGER_CAPTURE (WM_USER + 100)

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
void InstallKeyboardHook();
void UninstallKeyboardHook();
void ApplyWindowsSnippingReplacement(bool enable);
bool IsWinShiftS(const HotkeyConfig& hk);
void RegisterHotkeys();
void EnableDarkMode(HWND hwnd);
