#include "drawing.h"
#include <cmath>

void DrawRoundedRect(HDC hdc, const RECT& rect, int radius, COLORREF fillColor, COLORREF borderColor, int borderWidth) {
    HBRUSH brush = CreateSolidBrush(fillColor);
    HPEN pen = borderWidth > 0 ? CreatePen(PS_SOLID, borderWidth, borderColor) : (HPEN)GetStockObject(NULL_PEN);

    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);

    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    if (borderWidth > 0) DeleteObject(pen);
}

void DrawIcon(HDC hdc, int id, const RECT& rect, COLORREF color) {
    int cx = (rect.left + rect.right) / 2;
    int cy = (rect.top + rect.bottom) / 2;

    // For mode buttons, use Unicode characters like the overlay
    if (id == BTN_MODE_RECT || id == BTN_MODE_WINDOW || id == BTN_MODE_FULLSCREEN || id == BTN_MODE_TEXT) {
        const wchar_t* icon = nullptr;
        switch (id) {
        case BTN_MODE_RECT:       icon = L"\u25AD"; break;  // ▭
        case BTN_MODE_WINDOW:     icon = L"\u2750"; break;  // ❐
        case BTN_MODE_FULLSCREEN: icon = L"\u2B1C"; break;  // ⬜
        case BTN_MODE_TEXT:       icon = L"\u0054"; break;  // T
        }

        if (icon) {
            HFONT iconFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
            HFONT oldFont = (HFONT)SelectObject(hdc, iconFont);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, color);

            RECT textRect = rect;
            DrawTextW(hdc, icon, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, oldFont);
            DeleteObject(iconFont);
        }
        return;
    }

    // Settings gear icon
    if (id == BTN_SETTINGS) {
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

        const double PI = 3.14159265;
        const int numTeeth = 6;
        const int innerR = 5;
        const int outerR = 8;

        POINT gearPoints[24];
        for (int i = 0; i < numTeeth; i++) {
            double baseAngle = i * 2 * PI / numTeeth - PI / 2;
            double toothWidth = PI / numTeeth * 0.6;

            gearPoints[i * 4 + 0].x = cx + (int)(innerR * cos(baseAngle - toothWidth));
            gearPoints[i * 4 + 0].y = cy + (int)(innerR * sin(baseAngle - toothWidth));
            gearPoints[i * 4 + 1].x = cx + (int)(outerR * cos(baseAngle - toothWidth * 0.4));
            gearPoints[i * 4 + 1].y = cy + (int)(outerR * sin(baseAngle - toothWidth * 0.4));
            gearPoints[i * 4 + 2].x = cx + (int)(outerR * cos(baseAngle + toothWidth * 0.4));
            gearPoints[i * 4 + 2].y = cy + (int)(outerR * sin(baseAngle + toothWidth * 0.4));
            gearPoints[i * 4 + 3].x = cx + (int)(innerR * cos(baseAngle + toothWidth));
            gearPoints[i * 4 + 3].y = cy + (int)(innerR * sin(baseAngle + toothWidth));
        }

        HBRUSH gearBrush = CreateSolidBrush(color);
        HBRUSH oldFillBrush = (HBRUSH)SelectObject(hdc, gearBrush);
        Polygon(hdc, gearPoints, 24);
        SelectObject(hdc, oldFillBrush);
        DeleteObject(gearBrush);

        HBRUSH holeBrush = CreateSolidBrush(Colors::Surface);
        SelectObject(hdc, holeBrush);
        Ellipse(hdc, cx - 2, cy - 2, cx + 3, cy + 3);
        DeleteObject(holeBrush);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }
}

void DrawToolbarButton(HDC hdc, const ToolbarButton& btn, bool isHovered) {
    COLORREF bgColor;
    COLORREF iconColor = Colors::Text;

    if (btn.isToggle && btn.isActive) {
        bgColor = Colors::AccentDark;
        iconColor = Colors::Text;
    } else {
        bgColor = isHovered ? Colors::SurfaceHover : Colors::Surface;
        iconColor = isHovered ? Colors::Text : Colors::TextSecondary;
    }

    DrawRoundedRect(hdc, btn.rect, 6, bgColor);
    DrawIcon(hdc, btn.id, btn.rect, iconColor);
}

void DrawDelayDropdown(HDC hdc, const RECT& rect, bool isHovered) {
    DrawRoundedRect(hdc, rect, 6, isHovered ? Colors::SurfaceHover : Colors::Surface);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, isHovered ? Colors::Text : Colors::TextSecondary);

    wchar_t text[32];
    if (g_app.delaySeconds == 0) {
        wcscpy_s(text, L"No delay");
    } else {
        swprintf_s(text, L"%ds", g_app.delaySeconds);
    }

    HFONT oldFont = (HFONT)SelectObject(hdc, g_app.fontSmall);

    RECT textRect = rect;
    textRect.right -= 16;
    DrawTextW(hdc, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, oldFont);

    int ax = rect.right - 12;
    int ay = (rect.top + rect.bottom) / 2 - 1;

    POINT arrow[3] = {
        { ax - 3, ay },
        { ax + 3, ay },
        { ax, ay + 4 }
    };

    COLORREF arrowColor = isHovered ? Colors::Text : Colors::TextSecondary;
    HBRUSH arrowBrush = CreateSolidBrush(arrowColor);
    HPEN arrowPen = CreatePen(PS_SOLID, 1, Colors::Text);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, arrowBrush);
    HPEN oldPen = (HPEN)SelectObject(hdc, arrowPen);

    Polygon(hdc, arrow, 3);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(arrowBrush);
    DeleteObject(arrowPen);
}

void UpdateButtonRects() {
    int x = 12;
    int y = (TOOLBAR_HEIGHT - BUTTON_SIZE) / 2;

    // Mode buttons: Rectangle, Window, Fullscreen, Text (indices 0-3)
    for (int i = 0; i <= 3; i++) {
        g_buttons[i].rect = { x, y, x + BUTTON_SIZE, y + BUTTON_SIZE };
        x += BUTTON_SIZE + 4;
    }

    x += 12;

    // Delay dropdown (index 4)
    g_buttons[4].rect = { x, y, x + 70, y + BUTTON_SIZE };
    x += 70 + 12;

    // Settings button (index 5)
    g_buttons[5].rect = { x, y, x + BUTTON_SIZE, y + BUTTON_SIZE };
}
