#pragma once

#include "common.h"

LRESULT CALLBACK TooltipWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ShowTooltip(const wchar_t* text, int x, int y);
void HideTooltip();
