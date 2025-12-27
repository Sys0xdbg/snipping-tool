#include "main_window.h"
#include "app_state.h"
#include "drawing.h"
#include "tooltip.h"
#include "overlay.h"
#include "capture.h"
#include "notification.h"
#include "mode_picker.h"
#include "settings_dlg.h"
#include "hotkeys.h"

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        UpdateButtonRects();
        return 0;

    case WM_HOTKEY:
        // Ignore hotkeys if overlay is already visible
        if (IsWindowVisible(g_app.overlayWnd)) {
            return 0;
        }
        switch (wParam) {
        case HOTKEY_RECTANGLE:
            g_app.captureMode = MODE_RECTANGLE;
            ShowOverlay();
            break;
        case HOTKEY_WINDOW:
            g_app.captureMode = MODE_WINDOW;
            ShowOverlay();
            break;
        case HOTKEY_FULLSCREEN:
            g_app.captureMode = MODE_FULLSCREEN;
            CaptureFullscreen();
            break;
        case HOTKEY_PRINTSCREEN:
            ShowModePicker();
            break;
        }
        return 0;

    case WM_USER + 101: {
        g_app.captureMode = (CaptureMode)wParam;

        if (g_app.modePickerWnd) {
            DestroyWindow(g_app.modePickerWnd);
            g_app.modePickerWnd = nullptr;
        }

        if (g_printScreenMode) {
            g_printScreenMode = false;
            if (g_app.captureMode == MODE_FULLSCREEN) {
                ShowWindow(g_app.overlayWnd, SW_HIDE);

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
                ShowWindow(g_app.mainWnd, SW_SHOW);
            } else {
                SetForegroundWindow(g_app.overlayWnd);
                SetCapture(g_app.overlayWnd);
            }
        } else {
            if (g_app.captureMode == MODE_FULLSCREEN) {
                CaptureFullscreen();
            } else {
                ShowOverlay();
            }
        }
        return 0;
    }

    case WM_TRIGGER_CAPTURE:
        g_app.captureMode = (CaptureMode)wParam;
        if (g_app.captureMode == MODE_FULLSCREEN) {
            CaptureFullscreen();
        } else {
            ShowOverlay();
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdcScreen = BeginPaint(hwnd, &ps);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        HDC hdc = CreateCompatibleDC(hdcScreen);
        HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, clientRect.right, clientRect.bottom);
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdc, hBitmap);

        HBRUSH bgBrush = CreateSolidBrush(Colors::Background);
        FillRect(hdc, &clientRect, bgBrush);
        DeleteObject(bgBrush);

        for (int i = 0; i < NUM_BUTTONS; i++) {
            if (i == 4) {  // Delay button is now at index 4
                DrawDelayDropdown(hdc, g_buttons[i].rect, g_app.hoveredButton == g_buttons[i].id);
            } else {
                DrawToolbarButton(hdc, g_buttons[i], g_app.hoveredButton == g_buttons[i].id);
            }
        }

        HPEN divPen = CreatePen(PS_SOLID, 1, Colors::Divider);
        HPEN oldPen = (HPEN)SelectObject(hdc, divPen);
        int divX = g_buttons[3].rect.right + 6;  // Divider after Text button (index 3)
        int divTop = TOOLBAR_HEIGHT / 2 - 10;
        int divBottom = TOOLBAR_HEIGHT / 2 + 10;
        MoveToEx(hdc, divX, divTop, nullptr);
        LineTo(hdc, divX, divBottom);
        SelectObject(hdc, oldPen);
        DeleteObject(divPen);

        BitBlt(hdcScreen, 0, 0, clientRect.right, clientRect.bottom, hdc, 0, 0, SRCCOPY);

        SelectObject(hdc, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hdc);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        int newHovered = -1;
        int hoveredIndex = -1;
        for (int i = 0; i < NUM_BUTTONS; i++) {
            POINT pt = { x, y };
            if (PtInRect(&g_buttons[i].rect, pt)) {
                newHovered = g_buttons[i].id;
                hoveredIndex = i;
                break;
            }
        }

        if (newHovered != g_app.hoveredButton) {
            g_app.hoveredButton = newHovered;
            InvalidateRect(hwnd, nullptr, FALSE);

            if (newHovered >= 0 && hoveredIndex >= 0 && g_buttons[hoveredIndex].description) {
                RECT btnRect = g_buttons[hoveredIndex].rect;
                POINT screenPt = { (btnRect.left + btnRect.right) / 2, btnRect.bottom + 8 };
                ClientToScreen(hwnd, &screenPt);
                ShowTooltip(g_buttons[hoveredIndex].description, screenPt.x, screenPt.y);
                g_app.lastTooltipButton = newHovered;
            } else {
                HideTooltip();
            }
        }

        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        g_app.hoveredButton = -1;
        HideTooltip();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        for (int i = 0; i < NUM_BUTTONS; i++) {
            POINT pt = { x, y };
            if (PtInRect(&g_buttons[i].rect, pt)) {
                switch (g_buttons[i].id) {
                case BTN_MODE_RECT:
                    g_app.captureMode = MODE_RECTANGLE;
                    g_buttons[0].isActive = true;
                    g_buttons[1].isActive = false;
                    g_buttons[2].isActive = false;
                    g_buttons[3].isActive = false;
                    StartCapture();
                    break;
                case BTN_MODE_WINDOW:
                    g_app.captureMode = MODE_WINDOW;
                    g_buttons[0].isActive = false;
                    g_buttons[1].isActive = true;
                    g_buttons[2].isActive = false;
                    g_buttons[3].isActive = false;
                    StartCapture();
                    break;
                case BTN_MODE_FULLSCREEN:
                    g_app.captureMode = MODE_FULLSCREEN;
                    g_buttons[0].isActive = false;
                    g_buttons[1].isActive = false;
                    g_buttons[2].isActive = true;
                    g_buttons[3].isActive = false;
                    StartCapture();
                    break;
                case BTN_MODE_TEXT:
                    g_app.captureMode = MODE_TEXT;
                    g_buttons[0].isActive = false;
                    g_buttons[1].isActive = false;
                    g_buttons[2].isActive = false;
                    g_buttons[3].isActive = true;
                    StartCapture();
                    break;
                case BTN_DELAY:
                    ShowDelayMenu(hwnd);
                    break;
                case BTN_SETTINGS:
                    ShowSettingsDialog(hwnd);
                    break;
                }
                break;
            }
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_RECTANGLE);
        UnregisterHotKey(hwnd, HOTKEY_WINDOW);
        UnregisterHotKey(hwnd, HOTKEY_FULLSCREEN);
        UninstallKeyboardHook();
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
