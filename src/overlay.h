#pragma once

#include "common.h"

// Forward declaration
void HideTooltip();

RECT GetWindowVisibleRect(HWND hwnd);
BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam);
void CacheWindowList();
HWND FindWindowAtPoint(POINT pt);
void ShowOverlay();
void HideOverlay();
void NormalizeRect(RECT& r);
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void CaptureFullscreen();
void StartCapture();
void ShowDelayMenu(HWND hwnd);
