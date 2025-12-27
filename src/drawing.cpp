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

    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HPEN penThick = CreatePen(PS_SOLID, 2, color);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    switch (id) {
    case BTN_MODE_RECT: {
        int s = 7;
        int c = 4;
        SelectObject(hdc, penThick);
        MoveToEx(hdc, cx - s, cy - s + c, nullptr); LineTo(hdc, cx - s, cy - s); LineTo(hdc, cx - s + c, cy - s);
        MoveToEx(hdc, cx + s - c, cy - s, nullptr); LineTo(hdc, cx + s, cy - s); LineTo(hdc, cx + s, cy - s + c);
        MoveToEx(hdc, cx + s, cy + s - c, nullptr); LineTo(hdc, cx + s, cy + s); LineTo(hdc, cx + s - c, cy + s);
        MoveToEx(hdc, cx - s + c, cy + s, nullptr); LineTo(hdc, cx - s, cy + s); LineTo(hdc, cx - s, cy + s - c);
        break;
    }
    case BTN_MODE_WINDOW: {
        SelectObject(hdc, penThick);
        RoundRect(hdc, cx - 8, cy - 6, cx + 8, cy + 7, 3, 3);
        SelectObject(hdc, pen);
        MoveToEx(hdc, cx - 7, cy - 2, nullptr);
        LineTo(hdc, cx + 7, cy - 2);
        HBRUSH dotBrush = CreateSolidBrush(color);
        RECT dot1 = { cx + 3, cy - 5, cx + 5, cy - 3 };
        RECT dot2 = { cx + 5, cy - 5, cx + 7, cy - 3 };
        FillRect(hdc, &dot1, dotBrush);
        FillRect(hdc, &dot2, dotBrush);
        DeleteObject(dotBrush);
        break;
    }
    case BTN_MODE_FULLSCREEN: {
        SelectObject(hdc, penThick);
        RoundRect(hdc, cx - 9, cy - 6, cx + 9, cy + 4, 2, 2);
        SelectObject(hdc, pen);
        MoveToEx(hdc, cx - 3, cy + 4, nullptr);
        LineTo(hdc, cx - 3, cy + 7);
        LineTo(hdc, cx + 3, cy + 7);
        LineTo(hdc, cx + 3, cy + 4);
        break;
    }
    case BTN_MODE_TEXT: {
        // Draw "T" for text/OCR
        SelectObject(hdc, penThick);
        MoveToEx(hdc, cx - 6, cy - 6, nullptr);
        LineTo(hdc, cx + 7, cy - 6);
        MoveToEx(hdc, cx, cy - 6, nullptr);
        LineTo(hdc, cx, cy + 7);
        // Serifs
        SelectObject(hdc, pen);
        MoveToEx(hdc, cx - 3, cy + 7, nullptr);
        LineTo(hdc, cx + 4, cy + 7);
        break;
    }
    case BTN_SETTINGS: {
        SelectObject(hdc, penThick);
        Ellipse(hdc, cx - 3, cy - 3, cx + 4, cy + 4);
        SelectObject(hdc, pen);
        for (int i = 0; i < 6; i++) {
            double angle = i * 3.14159 / 3;
            int x1 = cx + (int)(5 * cos(angle));
            int y1 = cy + (int)(5 * sin(angle));
            int x2 = cx + (int)(8 * cos(angle));
            int y2 = cy + (int)(8 * sin(angle));
            MoveToEx(hdc, x1, y1, nullptr);
            LineTo(hdc, x2, y2);
        }
        break;
    }
    }

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    DeleteObject(penThick);
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
