#include "mode_picker.h"
#include "app_state.h"
#include "capture.h"
#include "overlay.h"

ModePickerBtn g_modePickerBtns[] = {
    { MODE_RECTANGLE, L"\u25AD", L"Rectangle", L"Draw a rectangle to capture a region", {} },
    { MODE_WINDOW, L"\u2750", L"Window", L"Click a window to capture it", {} },
    { MODE_FULLSCREEN, L"\u2B1C", L"Fullscreen", L"Capture the entire screen", {} },
    { MODE_TEXT, L"\u2131", L"Text", L"Copy text from screen (OCR)", {} },
    { -1, L"\u2715", L"Close", L"Cancel screenshot", {} },
};

bool g_printScreenMode = false;

LRESULT CALLBACK ModePickerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdcScreen = BeginPaint(hwnd, &ps);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        HDC hdc = CreateCompatibleDC(hdcScreen);
        HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, clientRect.right, clientRect.bottom);
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdc, hBitmap);

        Gdiplus::Graphics graphics(hdc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(245, 40, 40, 40));
        Gdiplus::GraphicsPath bgPath;
        int radius = 12;
        bgPath.AddArc(0, 0, radius * 2, radius * 2, 180, 90);
        bgPath.AddArc(clientRect.right - radius * 2, 0, radius * 2, radius * 2, 270, 90);
        bgPath.AddArc(clientRect.right - radius * 2, clientRect.bottom - radius * 2, radius * 2, radius * 2, 0, 90);
        bgPath.AddArc(0, clientRect.bottom - radius * 2, radius * 2, radius * 2, 90, 90);
        bgPath.CloseFigure();
        graphics.FillPath(&bgBrush, &bgPath);

        Gdiplus::Pen borderPen(Gdiplus::Color(255, 60, 60, 60), 1.0f);
        graphics.DrawPath(&borderPen, &bgPath);

        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        Gdiplus::FontFamily fontFamily(L"Segoe UI Symbol");
        Gdiplus::Font iconFont(&fontFamily, 14, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::FontFamily textFamily(L"Segoe UI");
        Gdiplus::Font descFont(&textFamily, 11, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

        Gdiplus::StringFormat centerFormat;
        centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
        centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        int x = 16;
        int btnY = 10;

        for (int i = 0; i < MODE_PICKER_NUM_BTNS; i++) {
            g_modePickerBtns[i].rect = { x, btnY, x + MODE_PICKER_BTN_SIZE, btnY + MODE_PICKER_BTN_SIZE };

            bool isHovered = (g_app.modePickerHovered == i);

            if (isHovered) {
                Gdiplus::SolidBrush hoverBrush(Gdiplus::Color(255, 70, 70, 70));
                graphics.FillEllipse(&hoverBrush, (float)(x + 2), (float)(btnY + 2),
                    (float)(MODE_PICKER_BTN_SIZE - 4), (float)(MODE_PICKER_BTN_SIZE - 4));
            }

            Gdiplus::SolidBrush iconBrush(i == 4 ? Gdiplus::Color(255, 200, 200, 200) : Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::RectF iconRect((float)x, (float)btnY, (float)MODE_PICKER_BTN_SIZE, (float)MODE_PICKER_BTN_SIZE);

            // Draw custom "T" icon for text mode (index 3)
            if (i == 3) {
                int cx = x + MODE_PICKER_BTN_SIZE / 2;
                int cy = btnY + MODE_PICKER_BTN_SIZE / 2;
                Gdiplus::Pen thickPen(Gdiplus::Color(255, 255, 255, 255), 2.0f);
                Gdiplus::Pen thinPen(Gdiplus::Color(255, 255, 255, 255), 1.0f);
                // Top horizontal line of T
                graphics.DrawLine(&thickPen, cx - 7, cy - 7, cx + 8, cy - 7);
                // Vertical stem of T
                graphics.DrawLine(&thickPen, cx, cy - 7, cx, cy + 8);
                // Bottom serif
                graphics.DrawLine(&thinPen, cx - 4, cy + 8, cx + 5, cy + 8);
            } else {
                graphics.DrawString(g_modePickerBtns[i].icon, -1, &iconFont, iconRect, &centerFormat, &iconBrush);
            }

            if (i == 3) {
                Gdiplus::Pen divPen(Gdiplus::Color(255, 80, 80, 80), 1.0f);
                graphics.DrawLine(&divPen, x + MODE_PICKER_BTN_SIZE + 10, btnY + 8,
                    x + MODE_PICKER_BTN_SIZE + 10, btnY + MODE_PICKER_BTN_SIZE - 8);
                x += 20;
            }

            x += MODE_PICKER_BTN_SIZE + 8;
        }

        if (g_app.modePickerHovered >= 0 && g_app.modePickerHovered < MODE_PICKER_NUM_BTNS) {
            Gdiplus::SolidBrush descBrush(Gdiplus::Color(255, 180, 180, 180));
            Gdiplus::RectF descRect(8.0f, (float)(btnY + MODE_PICKER_BTN_SIZE + 4),
                (float)(clientRect.right - 16), 20.0f);
            graphics.DrawString(g_modePickerBtns[g_app.modePickerHovered].description, -1,
                &descFont, descRect, &centerFormat, &descBrush);
        }

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
        POINT pt = { x, y };

        int newHovered = -1;
        for (int i = 0; i < MODE_PICKER_NUM_BTNS; i++) {
            if (PtInRect(&g_modePickerBtns[i].rect, pt)) {
                newHovered = i;
                break;
            }
        }

        if (newHovered != g_app.modePickerHovered) {
            g_app.modePickerHovered = newHovered;
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        g_app.modePickerHovered = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        for (int i = 0; i < MODE_PICKER_NUM_BTNS; i++) {
            if (PtInRect(&g_modePickerBtns[i].rect, pt)) {
                if (g_modePickerBtns[i].mode >= 0) {
                    g_app.modePickerWnd = nullptr;
                    DestroyWindow(hwnd);
                    PostMessageW(g_app.mainWnd, WM_USER + 101, g_modePickerBtns[i].mode, 0);
                } else {
                    HideModePicker();
                }
                return 0;
            }
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            HideModePicker();
            return 0;
        }
        break;

    case WM_KILLFOCUS:
        if (g_printScreenMode && g_app.modePickerWnd && IsWindow(g_app.modePickerWnd)) {
            DestroyWindow(g_app.modePickerWnd);
            g_app.modePickerWnd = nullptr;
            PostMessageW(g_app.mainWnd, WM_USER + 101, MODE_RECTANGLE, 0);
        } else if (!g_printScreenMode) {
            HideModePicker();
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowModePicker() {
    if (g_app.modePickerWnd && IsWindowVisible(g_app.modePickerWnd)) {
        HideModePicker();
        return;
    }

    ShowWindow(g_app.mainWnd, SW_HIDE);
    Sleep(150);

    if (!CaptureScreen()) {
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

    g_printScreenMode = true;
    g_app.isSelecting = false;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ModePickerWndProc;
        wc.hInstance = g_app.hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"SnippingToolModePicker";
        RegisterClassExW(&wc);
        registered = true;
    }

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int x = (screenWidth - MODE_PICKER_WIDTH) / 2;
    int y = 40;

    g_app.modePickerHovered = -1;

    g_app.modePickerWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        L"SnippingToolModePicker", L"",
        WS_POPUP,
        x, y, MODE_PICKER_WIDTH, MODE_PICKER_HEIGHT,
        nullptr, nullptr, g_app.hInstance, nullptr
    );

    SetLayeredWindowAttributes(g_app.modePickerWnd, 0, 255, LWA_ALPHA);

    HRGN rgn = CreateRoundRectRgn(0, 0, MODE_PICKER_WIDTH + 1, MODE_PICKER_HEIGHT + 1, 12, 12);
    SetWindowRgn(g_app.modePickerWnd, rgn, TRUE);

    ShowWindow(g_app.modePickerWnd, SW_SHOWNOACTIVATE);
    SetForegroundWindow(g_app.modePickerWnd);
    SetFocus(g_app.modePickerWnd);
}

void HideModePicker() {
    // Set this first to prevent WM_KILLFOCUS from triggering capture
    bool wasPrintScreenMode = g_printScreenMode;
    g_printScreenMode = false;

    if (g_app.modePickerWnd) {
        DestroyWindow(g_app.modePickerWnd);
        g_app.modePickerWnd = nullptr;
    }

    if (wasPrintScreenMode) {
        ShowWindow(g_app.overlayWnd, SW_HIDE);
        ShowWindow(g_app.mainWnd, SW_SHOW);
    }
}
