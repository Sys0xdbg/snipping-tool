#pragma once

#include "common.h"

// Dialog control IDs
#define IDC_PATH_EDIT           101
#define IDC_PATH_BROWSE         102
#define IDC_AUTOSAVE            103
#define IDC_HOTKEY_RECT_EDIT    110
#define IDC_HOTKEY_RECT_RECORD  111
#define IDC_HOTKEY_RECT_ENABLED 112
#define IDC_HOTKEY_WIN_EDIT     120
#define IDC_HOTKEY_WIN_RECORD   121
#define IDC_HOTKEY_WIN_ENABLED  122
#define IDC_HOTKEY_FULL_EDIT    130
#define IDC_HOTKEY_FULL_RECORD  131
#define IDC_HOTKEY_FULL_ENABLED 132
#define IDC_HOTKEY_TEXT_EDIT    135
#define IDC_HOTKEY_TEXT_RECORD  136
#define IDC_HOTKEY_TEXT_ENABLED 137
#define IDC_REPLACE_WINDOWS     140
#define IDC_RUN_STARTUP         141

// Settings window dimensions
const int SETTINGS_WIDTH = 450;
const int SETTINGS_HEIGHT = 700;
const int SETTINGS_PADDING = 24;
const int SETTINGS_ROW_HEIGHT = 52;

// Toggle IDs
#define TOGGLE_AUTOSAVE        1
#define TOGGLE_RECT_ENABLED    2
#define TOGGLE_WIN_ENABLED     3
#define TOGGLE_FULL_ENABLED    4
#define TOGGLE_TEXT_ENABLED    5
#define TOGGLE_REPLACE_WIN     6
#define TOGGLE_STARTUP         7

// Button IDs
#define BTN_SAVE_SETTINGS      201
#define BTN_CANCEL_SETTINGS    202
#define BTN_BROWSE             203
#define BTN_RECORD_RECT        204
#define BTN_RECORD_WIN         205
#define BTN_RECORD_FULL        206
#define BTN_RECORD_TEXT        207

struct SettingsState {
    HWND hwnd = nullptr;
    int hoveredToggle = 0;
    int scrollY = 0;
    bool isDragging = false;
    POINT dragStart = {};
};

struct SettingsToggle {
    int id;
    const wchar_t* label;
    const wchar_t* description;
    bool* value;
    RECT rect;
    RECT toggleRect;
};

struct SettingsBtn {
    int id;
    const wchar_t* label;
    RECT rect;
    bool isAccent;
};

extern SettingsState g_settings;
extern HotkeyConfig g_tempHotkeyRect;
extern HotkeyConfig g_tempHotkeyWindow;
extern HotkeyConfig g_tempHotkeyFullscreen;
extern HotkeyConfig g_tempHotkeyText;
extern bool g_tempAutoSave;
extern bool g_tempRectEnabled;
extern bool g_tempWinEnabled;
extern bool g_tempFullEnabled;
extern bool g_tempTextEnabled;
extern bool g_tempReplaceWin;
extern bool g_tempStartup;
extern std::wstring g_tempSavePath;
extern int g_hoveredBtn;
extern int g_hoveredToggle;
extern SettingsBtn g_settingsBtns[];

void DrawToggleSwitch(HDC hdc, int x, int y, bool isOn, bool isHovered);
void DrawTextGdiPlus(HDC hdc, const wchar_t* text, const RECT& rect, COLORREF color,
                     float fontSize = 12.0f, bool bold = false, bool center = false);
void DrawSettingsCard(HDC hdc, int x, int y, int width, int height);
void DrawSettingsButton(HDC hdc, const RECT& rect, const wchar_t* text, bool isAccent, bool isHovered);
void UpdateHotkeyDisplay(HWND hwnd, int editId, int recordId, const HotkeyConfig& hk, bool recording);
INT_PTR CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ShowSettingsDialog(HWND parent);
