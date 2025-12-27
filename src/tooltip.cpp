#include "tooltip.h"
#include "app_state.h"

LRESULT CALLBACK TooltipWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        const wchar_t* text = (const wchar_t*)GetPropW(hwnd, L"TooltipText");
        if (text) {
            Gdiplus::Graphics graphics(hdc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(240, 45, 45, 45));
            Gdiplus::GraphicsPath path;
            int r = 6;
            path.AddArc(0, 0, r * 2, r * 2, 180, 90);
            path.AddArc(clientRect.right - r * 2, 0, r * 2, r * 2, 270, 90);
            path.AddArc(clientRect.right - r * 2, clientRect.bottom - r * 2, r * 2, r * 2, 0, 90);
            path.AddArc(0, clientRect.bottom - r * 2, r * 2, r * 2, 90, 90);
            path.CloseFigure();
            graphics.FillPath(&bgBrush, &path);

            Gdiplus::Pen borderPen(Gdiplus::Color(255, 70, 70, 70), 1.0f);
            graphics.DrawPath(&borderPen, &path);

            Gdiplus::FontFamily fontFamily(L"Segoe UI");
            Gdiplus::Font font(&fontFamily, 11, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 220, 220, 220));
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::RectF textRect(0, 0, (float)clientRect.right, (float)clientRect.bottom);
            graphics.DrawString(text, -1, &font, textRect, &format, &textBrush);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void ShowTooltip(const wchar_t* text, int x, int y) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = TooltipWndProc;
        wc.hInstance = g_app.hInstance;
        wc.lpszClassName = L"SnippingToolTooltip";
        RegisterClassExW(&wc);
        registered = true;
    }

    HDC hdc = GetDC(nullptr);
    Gdiplus::Graphics graphics(hdc);
    Gdiplus::FontFamily fontFamily(L"Segoe UI");
    Gdiplus::Font font(&fontFamily, 11, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::RectF bounds;
    graphics.MeasureString(text, -1, &font, Gdiplus::PointF(0, 0), &bounds);
    ReleaseDC(nullptr, hdc);

    int width = (int)bounds.Width + 16;
    int height = 24;

    if (g_app.tooltipWnd) {
        RemovePropW(g_app.tooltipWnd, L"TooltipText");
        DestroyWindow(g_app.tooltipWnd);
    }

    g_app.tooltipWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        L"SnippingToolTooltip", L"",
        WS_POPUP,
        x - width / 2, y, width, height,
        nullptr, nullptr, g_app.hInstance, nullptr
    );

    SetLayeredWindowAttributes(g_app.tooltipWnd, 0, 255, LWA_ALPHA);
    SetPropW(g_app.tooltipWnd, L"TooltipText", (HANDLE)text);

    HRGN rgn = CreateRoundRectRgn(0, 0, width + 1, height + 1, 6, 6);
    SetWindowRgn(g_app.tooltipWnd, rgn, TRUE);

    ShowWindow(g_app.tooltipWnd, SW_SHOWNOACTIVATE);
}

void HideTooltip() {
    if (g_app.tooltipWnd) {
        RemovePropW(g_app.tooltipWnd, L"TooltipText");
        DestroyWindow(g_app.tooltipWnd);
        g_app.tooltipWnd = nullptr;
    }
    g_app.lastTooltipButton = -1;
}
