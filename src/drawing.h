#pragma once

#include "common.h"
#include "app_state.h"

void DrawRoundedRect(HDC hdc, const RECT& rect, int radius, COLORREF fillColor, COLORREF borderColor = 0, int borderWidth = 0);
void DrawIcon(HDC hdc, int id, const RECT& rect, COLORREF color);
void DrawToolbarButton(HDC hdc, const ToolbarButton& btn, bool isHovered);
void DrawDelayDropdown(HDC hdc, const RECT& rect, bool isHovered);
void UpdateButtonRects();
