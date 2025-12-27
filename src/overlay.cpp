#include "overlay.h"
#include "app_state.h"
#include "capture.h"
#include "notification.h"
#include "drawing.h"
#include "tooltip.h"
#include "ocr.h"

RECT GetWindowVisibleRect(HWND hwnd) {
    RECT rect = {};
    RECT windowRect = {};
    GetWindowRect(hwnd, &windowRect);

    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
        // DWMWA_EXTENDED_FRAME_BOUNDS should give visible bounds
        // Add 1 pixel inset to remove any remaining shadow/edge artifacts
        rect.left += 1;
        rect.top += 1;
        rect.right -= 1;
        rect.bottom -= 1;
    } else {
        // Fallback: adjust for typical Windows 10/11 invisible borders
        rect = windowRect;
        rect.left += 8;
        rect.right -= 8;
        rect.bottom -= 8;
    }

    return rect;
}

BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (IsIconic(hwnd)) return TRUE;
    if (hwnd == g_app.mainWnd || hwnd == g_app.overlayWnd) return TRUE;

    RECT rect = GetWindowVisibleRect(hwnd);

    if (rect.right > rect.left && rect.bottom > rect.top) {
        g_app.windowList.push_back({ hwnd, rect });
    }

    return TRUE;
}

void CacheWindowList() {
    g_app.windowList.clear();
    EnumWindows(EnumWindowsCallback, 0);
}

HWND FindWindowAtPoint(POINT pt) {
    for (const auto& entry : g_app.windowList) {
        if (PtInRect(&entry.second, pt)) {
            return entry.first;
        }
    }
    return nullptr;
}

void ShowOverlay() {
    ShowWindow(g_app.mainWnd, SW_HIDE);

    if (g_app.delaySeconds > 0) {
        Sleep(g_app.delaySeconds * 1000);
    } else {
        Sleep(200);
    }

    if (!CaptureScreen()) {
        MessageBoxW(g_app.mainWnd, L"Failed to capture screen", L"Error", MB_ICONERROR);
        ShowWindow(g_app.mainWnd, SW_SHOW);
        return;
    }

    if (g_app.overlayBitmap) {
        DeleteObject(g_app.overlayBitmap);
    }
    g_app.overlayBitmap = CaptureScreenToBitmap();

    CacheWindowList();

    // Use DXGI screen dimensions for consistency
    int width = g_app.screenWidth;
    int height = g_app.screenHeight;

    SetWindowPos(g_app.overlayWnd, HWND_TOPMOST, 0, 0, width, height, SWP_SHOWWINDOW);
    SetForegroundWindow(g_app.overlayWnd);
    SetCapture(g_app.overlayWnd);

    g_app.isSelecting = false;
}

void HideOverlay() {
    ReleaseCapture();
    ShowWindow(g_app.overlayWnd, SW_HIDE);
    ShowWindow(g_app.mainWnd, SW_SHOW);

    g_app.hoveredWindow = nullptr;
    SetRectEmpty(&g_app.hoveredWindowRect);
}

void NormalizeRect(RECT& r) {
    if (r.left > r.right) std::swap(r.left, r.right);
    if (r.top > r.bottom) std::swap(r.top, r.bottom);
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        int width = clientRect.right;
        int height = clientRect.bottom;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
        HBITMAP oldMemBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        HDC srcDC = CreateCompatibleDC(hdc);
        HBITMAP oldSrcBitmap = (HBITMAP)SelectObject(srcDC, g_app.overlayBitmap);
        BitBlt(memDC, 0, 0, width, height, srcDC, 0, 0, SRCCOPY);

        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 160, 0 };
        HDC overlayDC = CreateCompatibleDC(hdc);
        HBITMAP overlayBmp = CreateCompatibleBitmap(hdc, width, height);
        SelectObject(overlayDC, overlayBmp);

        HBRUSH darkBrush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(overlayDC, &clientRect, darkBrush);
        DeleteObject(darkBrush);

        AlphaBlend(memDC, 0, 0, width, height, overlayDC, 0, 0, width, height, blend);

        DeleteObject(overlayBmp);
        DeleteDC(overlayDC);

        RECT sel = {};
        bool hasSelection = false;

        if (g_app.captureMode == MODE_WINDOW && g_app.hoveredWindow && !IsRectEmpty(&g_app.hoveredWindowRect)) {
            sel = g_app.hoveredWindowRect;
            hasSelection = true;
        } else if (g_app.isSelecting) {
            sel = g_app.selectionRect;
            NormalizeRect(sel);
            hasSelection = (sel.right > sel.left && sel.bottom > sel.top);
        }

        if (hasSelection) {
            BitBlt(memDC, sel.left, sel.top, sel.right - sel.left, sel.bottom - sel.top,
                   srcDC, sel.left, sel.top, SRCCOPY);

            HPEN pen = CreatePen(PS_SOLID, 2, Colors::Accent);
            HPEN oldPen = (HPEN)SelectObject(memDC, pen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
            Rectangle(memDC, sel.left, sel.top, sel.right, sel.bottom);
            SelectObject(memDC, oldPen);
            SelectObject(memDC, oldBrush);
            DeleteObject(pen);

            wchar_t sizeText[64];
            swprintf_s(sizeText, L"%d x %d", sel.right - sel.left, sel.bottom - sel.top);

            RECT sizeRect = { sel.left, sel.bottom + 8, sel.left + 100, sel.bottom + 30 };
            DrawRoundedRect(memDC, sizeRect, 4, RGB(40, 40, 40));

            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, Colors::Text);
            HFONT oldFont = (HFONT)SelectObject(memDC, g_app.fontSmall);
            DrawTextW(memDC, sizeText, -1, &sizeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(memDC, oldFont);
        }

        SelectObject(srcDC, oldSrcBitmap);
        DeleteDC(srcDC);

        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, Colors::Text);
        HFONT oldFont = (HFONT)SelectObject(memDC, g_app.fontRegular);

        const wchar_t* text;
        if (g_app.captureMode == MODE_WINDOW) {
            text = L"Click on a window to capture it  -  Press ESC to cancel";
        } else if (g_app.captureMode == MODE_TEXT) {
            text = L"Select text area to copy  -  Press ESC to cancel";
        } else {
            text = L"Click and drag to select area  -  Press ESC to cancel";
        }
        RECT textRect = { 0, 20, width, 50 };
        DrawTextW(memDC, text, -1, &textRect, DT_CENTER | DT_SINGLELINE);
        SelectObject(memDC, oldFont);

        BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldMemBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
        if (g_app.captureMode == MODE_WINDOW) {
            if (g_app.hoveredWindow && !IsRectEmpty(&g_app.hoveredWindowRect)) {
                RECT captureRect = g_app.hoveredWindowRect;
                HideOverlay();

                std::wstring autoPath = GenerateAutoFilename();
                wchar_t filepath[MAX_PATH];
                wcscpy_s(filepath, autoPath.c_str());

                if (g_app.settings.autoSave) {
                    if (SaveScreenshot(captureRect, filepath)) {
                        ShowNotification(filepath);
                    } else {
                        MessageBoxW(g_app.mainWnd, L"Failed to save screenshot", L"Error", MB_ICONERROR);
                    }
                } else {
                    if (ShowSaveDialog(filepath, MAX_PATH)) {
                        if (SaveScreenshot(captureRect, filepath)) {
                            ShowNotification(filepath);
                        } else {
                            MessageBoxW(g_app.mainWnd, L"Failed to save screenshot", L"Error", MB_ICONERROR);
                        }
                    }
                }
            }
        } else {
            g_app.isSelecting = true;
            g_app.startPoint.x = GET_X_LPARAM(lParam);
            g_app.startPoint.y = GET_Y_LPARAM(lParam);
            g_app.endPoint = g_app.startPoint;
            g_app.selectionRect = { g_app.startPoint.x, g_app.startPoint.y,
                                    g_app.startPoint.x, g_app.startPoint.y };
        }
        return 0;

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (g_app.captureMode == MODE_WINDOW) {
            POINT screenPt = { x, y };
            ClientToScreen(hwnd, &screenPt);

            HWND targetWnd = FindWindowAtPoint(screenPt);

            if (targetWnd && targetWnd != g_app.hoveredWindow) {
                g_app.hoveredWindow = targetWnd;
                for (const auto& entry : g_app.windowList) {
                    if (entry.first == targetWnd) {
                        g_app.hoveredWindowRect = entry.second;
                        break;
                    }
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (!targetWnd && g_app.hoveredWindow) {
                g_app.hoveredWindow = nullptr;
                SetRectEmpty(&g_app.hoveredWindowRect);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        } else if (g_app.isSelecting) {
            g_app.endPoint.x = x;
            g_app.endPoint.y = y;
            g_app.selectionRect = { g_app.startPoint.x, g_app.startPoint.y,
                                    g_app.endPoint.x, g_app.endPoint.y };
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if ((g_app.captureMode == MODE_RECTANGLE || g_app.captureMode == MODE_TEXT) && g_app.isSelecting) {
            g_app.isSelecting = false;
            g_app.endPoint.x = GET_X_LPARAM(lParam);
            g_app.endPoint.y = GET_Y_LPARAM(lParam);
            g_app.selectionRect = { g_app.startPoint.x, g_app.startPoint.y,
                                    g_app.endPoint.x, g_app.endPoint.y };
            NormalizeRect(g_app.selectionRect);

            int w = g_app.selectionRect.right - g_app.selectionRect.left;
            int h = g_app.selectionRect.bottom - g_app.selectionRect.top;

            if (w > 5 && h > 5) {
                HideOverlay();

                if (g_app.captureMode == MODE_TEXT) {
                    // Perform OCR and copy text to clipboard
                    PerformOCR(g_app.selectionRect);
                } else {
                    // Regular screenshot
                    std::wstring autoPath = GenerateAutoFilename();
                    wchar_t filepath[MAX_PATH];
                    wcscpy_s(filepath, autoPath.c_str());

                    if (g_app.settings.autoSave) {
                        if (SaveScreenshot(g_app.selectionRect, filepath)) {
                            ShowNotification(filepath);
                        } else {
                            MessageBoxW(g_app.mainWnd, L"Failed to save screenshot", L"Error", MB_ICONERROR);
                        }
                    } else {
                        if (ShowSaveDialog(filepath, MAX_PATH)) {
                            if (SaveScreenshot(g_app.selectionRect, filepath)) {
                                ShowNotification(filepath);
                            } else {
                                MessageBoxW(g_app.mainWnd, L"Failed to save screenshot", L"Error", MB_ICONERROR);
                            }
                        }
                    }
                }
            }
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            HideOverlay();
        }
        return 0;

    case WM_RBUTTONDOWN:
        HideOverlay();
        return 0;

    case WM_CLOSE:
        HideOverlay();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void CaptureFullscreen() {
    ShowWindow(g_app.mainWnd, SW_HIDE);

    if (g_app.delaySeconds > 0) {
        Sleep(g_app.delaySeconds * 1000);
    } else {
        Sleep(200);
    }

    if (!CaptureScreen()) {
        MessageBoxW(g_app.mainWnd, L"Failed to capture screen", L"Error", MB_ICONERROR);
        ShowWindow(g_app.mainWnd, SW_SHOW);
        return;
    }

    ShowWindow(g_app.mainWnd, SW_SHOW);

    RECT fullscreen = { 0, 0, (LONG)g_app.screenWidth, (LONG)g_app.screenHeight };

    std::wstring autoPath = GenerateAutoFilename();
    wchar_t filepath[MAX_PATH];
    wcscpy_s(filepath, autoPath.c_str());

    if (g_app.settings.autoSave) {
        if (SaveScreenshot(fullscreen, filepath)) {
            ShowNotification(filepath);
        } else {
            MessageBoxW(g_app.mainWnd, L"Failed to save screenshot", L"Error", MB_ICONERROR);
        }
    } else {
        if (ShowSaveDialog(filepath, MAX_PATH)) {
            if (SaveScreenshot(fullscreen, filepath)) {
                ShowNotification(filepath);
            } else {
                MessageBoxW(g_app.mainWnd, L"Failed to save screenshot", L"Error", MB_ICONERROR);
            }
        }
    }
}

void StartCapture() {
    HideTooltip();

    switch (g_app.captureMode) {
    case MODE_RECTANGLE:
    case MODE_WINDOW:
    case MODE_TEXT:
        ShowOverlay();
        break;
    case MODE_FULLSCREEN:
        CaptureFullscreen();
        break;
    }
}

void ShowDelayMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_app.delaySeconds == 0 ? MF_CHECKED : 0), 1, L"No delay");
    AppendMenuW(menu, MF_STRING | (g_app.delaySeconds == 3 ? MF_CHECKED : 0), 2, L"3 seconds");
    AppendMenuW(menu, MF_STRING | (g_app.delaySeconds == 5 ? MF_CHECKED : 0), 3, L"5 seconds");
    AppendMenuW(menu, MF_STRING | (g_app.delaySeconds == 10 ? MF_CHECKED : 0), 4, L"10 seconds");

    RECT btnRect = g_buttons[3].rect;
    POINT pt = { btnRect.left, btnRect.bottom };
    ClientToScreen(hwnd, &pt);

    int result = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);

    switch (result) {
    case 1: g_app.delaySeconds = 0; break;
    case 2: g_app.delaySeconds = 3; break;
    case 3: g_app.delaySeconds = 5; break;
    case 4: g_app.delaySeconds = 10; break;
    }

    DestroyMenu(menu);
    InvalidateRect(hwnd, nullptr, TRUE);
}
