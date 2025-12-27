#pragma once

#include "common.h"

// Mode picker constants
const int MODE_PICKER_WIDTH = 280;
const int MODE_PICKER_HEIGHT = 80;
const int MODE_PICKER_BTN_SIZE = 40;

struct ModePickerBtn {
    int mode;
    const wchar_t* icon;
    const wchar_t* name;
    const wchar_t* description;
    RECT rect;
};

extern ModePickerBtn g_modePickerBtns[];
extern bool g_printScreenMode;

LRESULT CALLBACK ModePickerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ShowModePicker();
void HideModePicker();
