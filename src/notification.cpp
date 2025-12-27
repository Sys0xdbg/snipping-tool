#include "notification.h"
#include "app_state.h"

NotifButton g_notifButtons[3];
int g_notifAnimStep = 0;
int g_notifTargetX = 0;
int g_notifStartX = 0;
bool g_notifClosing = false;

HBITMAP CreatePreviewBitmap(const wchar_t* filepath, int width, int height) {
    Gdiplus::Bitmap* original = Gdiplus::Bitmap::FromFile(filepath);
    if (!original || original->GetLastStatus() != Gdiplus::Ok) {
        delete original;
        return nullptr;
    }

    Gdiplus::Bitmap preview(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&preview);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    float scaleX = (float)width / original->GetWidth();
    float scaleY = (float)height / original->GetHeight();
    float scale = (std::min)(scaleX, scaleY);
    int scaledW = (int)(original->GetWidth() * scale);
    int scaledH = (int)(original->GetHeight() * scale);
    int offsetX = (width - scaledW) / 2;
    int offsetY = (height - scaledH) / 2;

    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 25, 25, 25));
    graphics.FillRectangle(&bgBrush, 0, 0, width, height);

    graphics.DrawImage(original, offsetX, offsetY, scaledW, scaledH);
    delete original;

    HBITMAP hBitmap = nullptr;
    preview.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hBitmap);
    return hBitmap;
}

LRESULT CALLBACK NotificationWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        Gdiplus::GraphicsPath bgPath;
        int radius = 8;
        bgPath.AddArc(0, 0, radius * 2, radius * 2, 180, 90);
        bgPath.AddArc(clientRect.right - radius * 2, 0, radius * 2, radius * 2, 270, 90);
        bgPath.AddArc(clientRect.right - radius * 2, clientRect.bottom - radius * 2, radius * 2, radius * 2, 0, 90);
        bgPath.AddArc(0, clientRect.bottom - radius * 2, radius * 2, radius * 2, 90, 90);
        bgPath.CloseFigure();

        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(250, 44, 44, 44));
        graphics.FillPath(&bgBrush, &bgPath);
        Gdiplus::Pen borderPen(Gdiplus::Color(255, 70, 70, 70), 1.0f);
        graphics.DrawPath(&borderPen, &bgPath);

        Gdiplus::FontFamily fontFamily(L"Segoe UI");
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));

        Gdiplus::Font titleFont(&fontFamily, 12, Gdiplus::FontStyleBold);
        graphics.DrawString(L"Screenshot saved", -1, &titleFont, Gdiplus::PointF(16.0f, 12.0f), &textBrush);

        int previewX = (NOTIF_WIDTH - NOTIF_PREVIEW_WIDTH) / 2;
        int previewY = 38;

        if (g_app.notificationPreview) {
            Gdiplus::GraphicsPath clipPath;
            int r = 6;
            clipPath.AddArc(previewX, previewY, r * 2, r * 2, 180, 90);
            clipPath.AddArc(previewX + NOTIF_PREVIEW_WIDTH - r * 2, previewY, r * 2, r * 2, 270, 90);
            clipPath.AddArc(previewX + NOTIF_PREVIEW_WIDTH - r * 2, previewY + NOTIF_PREVIEW_HEIGHT - r * 2, r * 2, r * 2, 0, 90);
            clipPath.AddArc(previewX, previewY + NOTIF_PREVIEW_HEIGHT - r * 2, r * 2, r * 2, 90, 90);
            clipPath.CloseFigure();

            Gdiplus::Region clipRegion(&clipPath);
            graphics.SetClip(&clipRegion);

            Gdiplus::Bitmap* previewBmp = Gdiplus::Bitmap::FromHBITMAP(g_app.notificationPreview, nullptr);
            if (previewBmp) {
                graphics.DrawImage(previewBmp, previewX, previewY, NOTIF_PREVIEW_WIDTH, NOTIF_PREVIEW_HEIGHT);
                delete previewBmp;
            }
            graphics.ResetClip();

            Gdiplus::Pen previewBorder(Gdiplus::Color(60, 255, 255, 255), 1.0f);
            graphics.DrawPath(&previewBorder, &clipPath);
        }

        int btnY = NOTIF_HEIGHT - NOTIF_BTN_HEIGHT - 12;
        int btnWidth = 100;
        int btnSpacing = 10;
        int totalBtnWidth = btnWidth * 3 + btnSpacing * 2;
        int btnStartX = (NOTIF_WIDTH - totalBtnWidth) / 2;

        g_notifButtons[0].rect = { btnStartX, btnY, btnStartX + btnWidth, btnY + NOTIF_BTN_HEIGHT };
        g_notifButtons[0].label = L"Copy";

        int btn2X = btnStartX + btnWidth + btnSpacing;
        g_notifButtons[1].rect = { btn2X, btnY, btn2X + btnWidth, btnY + NOTIF_BTN_HEIGHT };
        g_notifButtons[1].label = L"Open folder";

        int btn3X = btn2X + btnWidth + btnSpacing;
        g_notifButtons[2].rect = { btn3X, btnY, btn3X + btnWidth, btnY + NOTIF_BTN_HEIGHT };
        g_notifButtons[2].label = L"Dismiss";

        Gdiplus::Font btnFont(&fontFamily, 10, Gdiplus::FontStyleRegular);
        Gdiplus::StringFormat centerFormat;
        centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
        centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        for (int i = 0; i < 3; i++) {
            RECT& rect = g_notifButtons[i].rect;
            bool hovered = (g_app.notificationHovered == i);

            Gdiplus::Color btnBg = hovered
                ? Gdiplus::Color(255, 65, 65, 65)
                : Gdiplus::Color(255, 55, 55, 55);
            Gdiplus::SolidBrush btnBrush(btnBg);

            Gdiplus::GraphicsPath btnPath;
            int br = 4;
            btnPath.AddArc(rect.left, rect.top, br * 2, br * 2, 180, 90);
            btnPath.AddArc(rect.right - br * 2, rect.top, br * 2, br * 2, 270, 90);
            btnPath.AddArc(rect.right - br * 2, rect.bottom - br * 2, br * 2, br * 2, 0, 90);
            btnPath.AddArc(rect.left, rect.bottom - br * 2, br * 2, br * 2, 90, 90);
            btnPath.CloseFigure();
            graphics.FillPath(&btnBrush, &btnPath);

            Gdiplus::Pen btnBorder(Gdiplus::Color(100, 255, 255, 255), 1.0f);
            graphics.DrawPath(&btnBorder, &btnPath);

            Gdiplus::RectF btnRect((float)rect.left, (float)rect.top,
                (float)(rect.right - rect.left), (float)(rect.bottom - rect.top));
            graphics.DrawString(g_notifButtons[i].label, -1, &btnFont, btnRect, &centerFormat, &textBrush);
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
        for (int i = 0; i < 3; i++) {
            if (PtInRect(&g_notifButtons[i].rect, pt)) {
                newHovered = i;
                break;
            }
        }

        if (newHovered != g_app.notificationHovered) {
            g_app.notificationHovered = newHovered;
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_app.notificationHovered != -1) {
            g_app.notificationHovered = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        for (int i = 0; i < 3; i++) {
            if (PtInRect(&g_notifButtons[i].rect, pt)) {
                if (i == 0) {
                    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(g_app.lastScreenshotPath.c_str());
                    if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
                        HBITMAP hBmp;
                        bmp->GetHBITMAP(Gdiplus::Color(255, 255, 255), &hBmp);
                        if (OpenClipboard(hwnd)) {
                            EmptyClipboard();
                            SetClipboardData(CF_BITMAP, hBmp);
                            CloseClipboard();
                        }
                        delete bmp;
                    }
                } else if (i == 1) {
                    std::wstring cmd = L"/select,\"" + g_app.lastScreenshotPath + L"\"";
                    ShellExecuteW(nullptr, L"open", L"explorer.exe", cmd.c_str(), nullptr, SW_SHOWNORMAL);
                } else if (i == 2) {
                    HideNotification();
                }
                return 0;
            }
        }
        return 0;
    }

    case WM_TIMER:
        if (wParam == NOTIF_TIMER_ID) {
            KillTimer(hwnd, NOTIF_TIMER_ID);
            g_notifClosing = true;
            g_notifAnimStep = 0;
            RECT rect;
            GetWindowRect(hwnd, &rect);
            g_notifStartX = rect.left;
            g_notifTargetX = GetSystemMetrics(SM_CXSCREEN) + 10;
            SetTimer(hwnd, NOTIF_ANIM_TIMER_ID, NOTIF_ANIM_DURATION / NOTIF_ANIM_STEPS, nullptr);
        } else if (wParam == NOTIF_ANIM_TIMER_ID) {
            g_notifAnimStep++;
            if (g_notifAnimStep >= NOTIF_ANIM_STEPS) {
                KillTimer(hwnd, NOTIF_ANIM_TIMER_ID);
                if (g_notifClosing) {
                    DestroyWindow(hwnd);
                    g_app.notificationWnd = nullptr;
                    if (g_app.notificationPreview) {
                        DeleteObject(g_app.notificationPreview);
                        g_app.notificationPreview = nullptr;
                    }
                    g_notifClosing = false;
                }
            } else {
                float t = (float)g_notifAnimStep / NOTIF_ANIM_STEPS;
                float ease = g_notifClosing
                    ? t * t
                    : 1.0f - (1.0f - t) * (1.0f - t);
                int currentX = g_notifStartX + (int)((g_notifTargetX - g_notifStartX) * ease);

                RECT rect;
                GetWindowRect(hwnd, &rect);
                SetWindowPos(hwnd, nullptr, currentX, rect.top, 0, 0,
                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void ShowNotification(const wchar_t* filepath) {
    g_app.lastScreenshotPath = filepath;

    if (g_app.notificationPreview) {
        DeleteObject(g_app.notificationPreview);
    }
    g_app.notificationPreview = CreatePreviewBitmap(filepath, NOTIF_PREVIEW_WIDTH, NOTIF_PREVIEW_HEIGHT);

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = NotificationWndProc;
        wc.hInstance = g_app.hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"SnippingToolNotification";
        RegisterClassExW(&wc);
        registered = true;
    }

    if (g_app.notificationWnd) {
        KillTimer(g_app.notificationWnd, NOTIF_TIMER_ID);
        KillTimer(g_app.notificationWnd, NOTIF_ANIM_TIMER_ID);
        DestroyWindow(g_app.notificationWnd);
        g_app.notificationWnd = nullptr;
    }

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    g_notifTargetX = screenWidth - NOTIF_WIDTH - 20;
    g_notifStartX = screenWidth + 10;
    int y = screenHeight - NOTIF_HEIGHT - 60;

    g_app.notificationHovered = -1;
    g_notifAnimStep = 0;
    g_notifClosing = false;

    g_app.notificationWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        L"SnippingToolNotification", L"",
        WS_POPUP,
        g_notifStartX, y, NOTIF_WIDTH, NOTIF_HEIGHT,
        nullptr, nullptr, g_app.hInstance, nullptr
    );

    SetLayeredWindowAttributes(g_app.notificationWnd, 0, 255, LWA_ALPHA);
    ShowWindow(g_app.notificationWnd, SW_SHOWNOACTIVATE);

    SetTimer(g_app.notificationWnd, NOTIF_ANIM_TIMER_ID, NOTIF_ANIM_DURATION / NOTIF_ANIM_STEPS, nullptr);
    SetTimer(g_app.notificationWnd, NOTIF_TIMER_ID, NOTIF_DURATION, nullptr);
}

void HideNotification() {
    if (g_app.notificationWnd && !g_notifClosing) {
        KillTimer(g_app.notificationWnd, NOTIF_TIMER_ID);
        g_notifClosing = true;
        g_notifAnimStep = 0;
        RECT rect;
        GetWindowRect(g_app.notificationWnd, &rect);
        g_notifStartX = rect.left;
        g_notifTargetX = GetSystemMetrics(SM_CXSCREEN) + 10;
        SetTimer(g_app.notificationWnd, NOTIF_ANIM_TIMER_ID, NOTIF_ANIM_DURATION / NOTIF_ANIM_STEPS, nullptr);
    }
}
