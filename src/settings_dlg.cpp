#include "settings_dlg.h"
#include "app_state.h"
#include "settings.h"
#include "hotkeys.h"
#include "drawing.h"

SettingsState g_settings;
HotkeyConfig g_tempHotkeyRect;
HotkeyConfig g_tempHotkeyWindow;
HotkeyConfig g_tempHotkeyFullscreen;
HotkeyConfig g_tempHotkeyText;
bool g_tempAutoSave;
bool g_tempRectEnabled;
bool g_tempWinEnabled;
bool g_tempFullEnabled;
bool g_tempTextEnabled;
bool g_tempReplaceWin;
bool g_tempStartup;
std::wstring g_tempSavePath;
int g_hoveredBtn = 0;
int g_hoveredToggle = 0;

SettingsBtn g_settingsBtns[] = {
    { BTN_BROWSE, L"Browse", {}, false },
    { BTN_RECORD_RECT, L"Record", {}, false },
    { BTN_RECORD_WIN, L"Record", {}, false },
    { BTN_RECORD_FULL, L"Record", {}, false },
    { BTN_RECORD_TEXT, L"Record", {}, false },
    { BTN_SAVE_SETTINGS, L"Save", {}, true },
    { BTN_CANCEL_SETTINGS, L"Cancel", {}, false },
};

void DrawToggleSwitch(HDC hdc, int x, int y, bool isOn, bool isHovered) {
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    int width = 44;
    int height = 22;

    Gdiplus::Color trackColor = isOn ? Gdiplus::Color(255, 76, 194, 255) :
                                 (isHovered ? Gdiplus::Color(255, 70, 70, 70) : Gdiplus::Color(255, 50, 50, 50));
    Gdiplus::Color borderColor = isOn ? Gdiplus::Color(255, 76, 194, 255) : Gdiplus::Color(255, 140, 140, 140);
    Gdiplus::Color knobColor(255, 255, 255, 255);

    Gdiplus::GraphicsPath trackPath;
    trackPath.AddArc((float)x, (float)y, (float)height, (float)height, 90, 180);
    trackPath.AddArc((float)(x + width - height), (float)y, (float)height, (float)height, 270, 180);
    trackPath.CloseFigure();

    Gdiplus::SolidBrush trackBrush(trackColor);
    graphics.FillPath(&trackBrush, &trackPath);

    Gdiplus::Pen borderPen(borderColor, 1.5f);
    graphics.DrawPath(&borderPen, &trackPath);

    int knobSize = 14;
    float knobX = isOn ? (float)(x + width - knobSize - 4) : (float)(x + 4);
    float knobY = (float)y + (height - knobSize) / 2.0f;
    Gdiplus::SolidBrush knobBrush(knobColor);
    graphics.FillEllipse(&knobBrush, knobX, knobY, (float)knobSize, (float)knobSize);
}

void DrawTextGdiPlus(HDC hdc, const wchar_t* text, const RECT& rect, COLORREF color,
                     float fontSize, bool bold, bool center) {
    Gdiplus::Graphics graphics(hdc);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    Gdiplus::FontFamily fontFamily(L"Segoe UI");
    Gdiplus::Font font(&fontFamily, fontSize, bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
                       Gdiplus::UnitPixel);

    Gdiplus::StringFormat format;
    format.SetAlignment(center ? Gdiplus::StringAlignmentCenter : Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentNear);

    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));
    Gdiplus::RectF rectF((float)rect.left, (float)rect.top,
                         (float)(rect.right - rect.left), (float)(rect.bottom - rect.top));

    graphics.DrawString(text, -1, &font, rectF, &format, &brush);
}

void DrawSettingsCard(HDC hdc, int x, int y, int width, int height) {
    HBRUSH brush = CreateSolidBrush(Colors::Surface);
    HPEN pen = CreatePen(PS_SOLID, 1, Colors::Border);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    RoundRect(hdc, x, y, x + width, y + height, 8, 8);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawSettingsButton(HDC hdc, const RECT& rect, const wchar_t* text, bool isAccent, bool isHovered) {
    COLORREF bgColor;
    COLORREF textColor;

    if (isAccent) {
        bgColor = isHovered ? Colors::AccentHover : Colors::Accent;
        textColor = RGB(0, 0, 0);
    } else {
        bgColor = isHovered ? Colors::SurfaceHover : Colors::Surface;
        textColor = Colors::Text;
    }

    DrawRoundedRect(hdc, rect, 4, bgColor, Colors::Border, 1);

    // Use GDI+ for cleaner text rendering
    Gdiplus::Graphics graphics(hdc);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::FontFamily fontFamily(L"Segoe UI");
    Gdiplus::Font font(&fontFamily, 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(textColor), GetGValue(textColor), GetBValue(textColor)));
    Gdiplus::RectF rectF((float)rect.left, (float)rect.top,
                         (float)(rect.right - rect.left), (float)(rect.bottom - rect.top));

    graphics.DrawString(text, -1, &font, rectF, &format, &brush);
}

void UpdateHotkeyDisplay(HWND hwnd, int editId, int recordId, const HotkeyConfig& hk, bool recording) {
    if (recording) {
        SetDlgItemTextW(hwnd, editId, L"Press hotkey...");
        SetDlgItemTextW(hwnd, recordId, L"Cancel");
    } else {
        SetDlgItemTextW(hwnd, editId, hk.GetString().c_str());
        SetDlgItemTextW(hwnd, recordId, L"Record");
    }
}

INT_PTR CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_PATH_BROWSE: {
            BROWSEINFOW bi = {};
            bi.hwndOwner = hwnd;
            bi.lpszTitle = L"Select Screenshot Folder";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

            PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t path[MAX_PATH];
                if (SHGetPathFromIDListW(pidl, path)) {
                    SetDlgItemTextW(hwnd, IDC_PATH_EDIT, path);
                }
                CoTaskMemFree(pidl);
            }
            return TRUE;
        }

        case IDC_HOTKEY_RECT_RECORD:
            if (g_app.recordingHotkeyType != 1) {
                g_app.recordingHotkeyType = 1;
                g_app.tempHotkey = { 0, 0, false };
                UpdateHotkeyDisplay(hwnd, IDC_HOTKEY_RECT_EDIT, IDC_HOTKEY_RECT_RECORD, g_app.tempHotkey, true);
                UpdateHotkeyDisplay(hwnd, IDC_HOTKEY_WIN_EDIT, IDC_HOTKEY_WIN_RECORD, g_tempHotkeyWindow, false);
                UpdateHotkeyDisplay(hwnd, IDC_HOTKEY_FULL_EDIT, IDC_HOTKEY_FULL_RECORD, g_tempHotkeyFullscreen, false);
            } else {
                g_app.recordingHotkeyType = 0;
                UpdateHotkeyDisplay(hwnd, IDC_HOTKEY_RECT_EDIT, IDC_HOTKEY_RECT_RECORD, g_tempHotkeyRect, false);
            }
            return TRUE;

        case IDC_HOTKEY_WIN_RECORD:
            if (g_app.recordingHotkeyType != 2) {
                g_app.recordingHotkeyType = 2;
                g_app.tempHotkey = { 0, 0, false };
                UpdateHotkeyDisplay(hwnd, IDC_HOTKEY_WIN_EDIT, IDC_HOTKEY_WIN_RECORD, g_app.tempHotkey, true);
                UpdateHotkeyDisplay(hwnd, IDC_HOTKEY_RECT_EDIT, IDC_HOTKEY_RECT_RECORD, g_tempHotkeyRect, false);
                UpdateHotkeyDisplay(hwnd, IDC_HOTKEY_FULL_EDIT, IDC_HOTKEY_FULL_RECORD, g_tempHotkeyFullscreen, false);
            } else {
                g_app.recordingHotkeyType = 0;
                UpdateHotkeyDisplay(hwnd, IDC_HOTKEY_WIN_EDIT, IDC_HOTKEY_WIN_RECORD, g_tempHotkeyWindow, false);
            }
            return TRUE;

        case IDC_HOTKEY_FULL_RECORD:
            if (g_app.recordingHotkeyType != 3) {
                g_app.recordingHotkeyType = 3;
                g_app.tempHotkey = { 0, 0, false };
                UpdateHotkeyDisplay(hwnd, IDC_HOTKEY_FULL_EDIT, IDC_HOTKEY_FULL_RECORD, g_app.tempHotkey, true);
                UpdateHotkeyDisplay(hwnd, IDC_HOTKEY_RECT_EDIT, IDC_HOTKEY_RECT_RECORD, g_tempHotkeyRect, false);
                UpdateHotkeyDisplay(hwnd, IDC_HOTKEY_WIN_EDIT, IDC_HOTKEY_WIN_RECORD, g_tempHotkeyWindow, false);
            } else {
                g_app.recordingHotkeyType = 0;
                UpdateHotkeyDisplay(hwnd, IDC_HOTKEY_FULL_EDIT, IDC_HOTKEY_FULL_RECORD, g_tempHotkeyFullscreen, false);
            }
            return TRUE;

        case IDOK: {
            wchar_t path[MAX_PATH];
            GetDlgItemTextW(hwnd, IDC_PATH_EDIT, path, MAX_PATH);
            g_app.settings.savePath = path;
            g_app.settings.autoSave = (IsDlgButtonChecked(hwnd, IDC_AUTOSAVE) == BST_CHECKED);

            g_app.settings.hotkeyRect = g_tempHotkeyRect;
            g_app.settings.hotkeyRect.enabled = (IsDlgButtonChecked(hwnd, IDC_HOTKEY_RECT_ENABLED) == BST_CHECKED);
            g_app.settings.hotkeyWindow = g_tempHotkeyWindow;
            g_app.settings.hotkeyWindow.enabled = (IsDlgButtonChecked(hwnd, IDC_HOTKEY_WIN_ENABLED) == BST_CHECKED);
            g_app.settings.hotkeyFullscreen = g_tempHotkeyFullscreen;
            g_app.settings.hotkeyFullscreen.enabled = (IsDlgButtonChecked(hwnd, IDC_HOTKEY_FULL_ENABLED) == BST_CHECKED);

            bool newReplaceWindows = (IsDlgButtonChecked(hwnd, IDC_REPLACE_WINDOWS) == BST_CHECKED);
            bool newRunAtStartup = (IsDlgButtonChecked(hwnd, IDC_RUN_STARTUP) == BST_CHECKED);

            if (newReplaceWindows != g_app.settings.replaceWindowsSnipping) {
                ApplyWindowsSnippingReplacement(newReplaceWindows);
                g_app.settings.replaceWindowsSnipping = newReplaceWindows;

                if (newReplaceWindows) {
                    MessageBoxW(hwnd,
                        L"Windows Snipping Tool hotkey has been disabled.\n\n"
                        L"You may need to restart Explorer or log out/in for the change to take effect.\n\n"
                        L"This app will now use Win+Shift+S for Rectangle capture.",
                        L"Snipping Tool Replaced", MB_ICONINFORMATION);
                }
            }

            if (newRunAtStartup != g_app.settings.runAtStartup) {
                SetRunAtStartup(newRunAtStartup);
                g_app.settings.runAtStartup = newRunAtStartup;
            }

            SaveSettings();
            DestroyWindow(hwnd);
            return TRUE;
        }

        case IDCANCEL:
            DestroyWindow(hwnd);
            return TRUE;
        }
        break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (g_app.recordingHotkeyType != 0) {
            UINT vk = (UINT)wParam;

            if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) {
                return TRUE;
            }

            UINT mods = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
            if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
            if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
            if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) mods |= MOD_WIN;

            HotkeyConfig newHotkey = { mods, vk, true };

            switch (g_app.recordingHotkeyType) {
            case 1:
                g_tempHotkeyRect = newHotkey;
                UpdateHotkeyDisplay(hwnd, IDC_HOTKEY_RECT_EDIT, IDC_HOTKEY_RECT_RECORD, g_tempHotkeyRect, false);
                break;
            case 2:
                g_tempHotkeyWindow = newHotkey;
                UpdateHotkeyDisplay(hwnd, IDC_HOTKEY_WIN_EDIT, IDC_HOTKEY_WIN_RECORD, g_tempHotkeyWindow, false);
                break;
            case 3:
                g_tempHotkeyFullscreen = newHotkey;
                UpdateHotkeyDisplay(hwnd, IDC_HOTKEY_FULL_EDIT, IDC_HOTKEY_FULL_RECORD, g_tempHotkeyFullscreen, false);
                break;
            }

            g_app.recordingHotkeyType = 0;
            return TRUE;
        }
        break;
    }

    return FALSE;
}

static HWND s_hPathEdit = nullptr;

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        s_hPathEdit = CreateWindowExW(0, L"EDIT", g_tempSavePath.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            SETTINGS_PADDING + 10, 93, SETTINGS_WIDTH - SETTINGS_PADDING * 2 - 105, 20,
            hwnd, (HMENU)IDC_PATH_EDIT, g_app.hInstance, nullptr);
        SendMessageW(s_hPathEdit, WM_SETFONT, (WPARAM)g_app.fontSmall, TRUE);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        int y = SETTINGS_PADDING;
        int contentWidth = SETTINGS_WIDTH - SETTINGS_PADDING * 2;

        RECT titleRect = { SETTINGS_PADDING, y, SETTINGS_WIDTH - SETTINGS_PADDING, y + 30 };
        DrawTextGdiPlus(hdc, L"Settings", titleRect, Colors::Text, 18.0f, true);
        y += 40;

        RECT pathLabel = { SETTINGS_PADDING, y, SETTINGS_WIDTH - SETTINGS_PADDING, y + 20 };
        DrawTextGdiPlus(hdc, L"Screenshot save location", pathLabel, Colors::TextSecondary, 12.0f);
        y += 24;

        RECT pathBg = { SETTINGS_PADDING, y, SETTINGS_WIDTH - SETTINGS_PADDING - 85, y + 30 };
        DrawRoundedRect(hdc, pathBg, 4, Colors::Surface, Colors::Border, 1);

        g_settingsBtns[0].rect = { SETTINGS_WIDTH - SETTINGS_PADDING - 75, y, SETTINGS_WIDTH - SETTINGS_PADDING, y + 30 };
        DrawSettingsButton(hdc, g_settingsBtns[0].rect, L"Browse", false, g_hoveredBtn == BTN_BROWSE);
        y += 46;

        DrawSettingsCard(hdc, SETTINGS_PADDING, y, contentWidth, SETTINGS_ROW_HEIGHT);
        RECT autoLabel = { SETTINGS_PADDING + 12, y + 8, SETTINGS_WIDTH - 80, y + 26 };
        DrawTextGdiPlus(hdc, L"Auto-save screenshots", autoLabel, Colors::Text, 13.0f);
        RECT autoDesc = { SETTINGS_PADDING + 12, y + 26, SETTINGS_WIDTH - 80, y + 42 };
        DrawTextGdiPlus(hdc, L"Skip the save dialog", autoDesc, Colors::TextSecondary, 11.0f);
        DrawToggleSwitch(hdc, SETTINGS_WIDTH - SETTINGS_PADDING - 52, y + 14, g_tempAutoSave, g_hoveredToggle == TOGGLE_AUTOSAVE);
        y += SETTINGS_ROW_HEIGHT + 12;

        RECT hkHeader = { SETTINGS_PADDING, y, SETTINGS_WIDTH - SETTINGS_PADDING, y + 20 };
        DrawTextGdiPlus(hdc, L"Keyboard shortcuts", hkHeader, Colors::TextSecondary, 12.0f);
        y += 28;

        DrawSettingsCard(hdc, SETTINGS_PADDING, y, contentWidth, SETTINGS_ROW_HEIGHT);
        RECT rectLabel = { SETTINGS_PADDING + 12, y + 6, 140, y + 24 };
        DrawTextGdiPlus(hdc, L"Rectangle", rectLabel, Colors::Text, 13.0f);
        RECT rectHk = { SETTINGS_PADDING + 12, y + 24, 200, y + 42 };
        DrawTextGdiPlus(hdc, g_tempHotkeyRect.GetString().c_str(), rectHk, Colors::TextSecondary, 11.0f);
        g_settingsBtns[1].rect = { SETTINGS_WIDTH - SETTINGS_PADDING - 115, y + 10, SETTINGS_WIDTH - SETTINGS_PADDING - 60, y + 38 };
        DrawSettingsButton(hdc, g_settingsBtns[1].rect, g_app.recordingHotkeyType == 1 ? L"..." : L"Record", false, g_hoveredBtn == BTN_RECORD_RECT);
        DrawToggleSwitch(hdc, SETTINGS_WIDTH - SETTINGS_PADDING - 52, y + 14, g_tempRectEnabled, g_hoveredToggle == TOGGLE_RECT_ENABLED);
        y += SETTINGS_ROW_HEIGHT + 4;

        DrawSettingsCard(hdc, SETTINGS_PADDING, y, contentWidth, SETTINGS_ROW_HEIGHT);
        RECT winLabel = { SETTINGS_PADDING + 12, y + 6, 140, y + 24 };
        DrawTextGdiPlus(hdc, L"Window", winLabel, Colors::Text, 13.0f);
        RECT winHk = { SETTINGS_PADDING + 12, y + 24, 200, y + 42 };
        DrawTextGdiPlus(hdc, g_tempHotkeyWindow.GetString().c_str(), winHk, Colors::TextSecondary, 11.0f);
        g_settingsBtns[2].rect = { SETTINGS_WIDTH - SETTINGS_PADDING - 115, y + 10, SETTINGS_WIDTH - SETTINGS_PADDING - 60, y + 38 };
        DrawSettingsButton(hdc, g_settingsBtns[2].rect, g_app.recordingHotkeyType == 2 ? L"..." : L"Record", false, g_hoveredBtn == BTN_RECORD_WIN);
        DrawToggleSwitch(hdc, SETTINGS_WIDTH - SETTINGS_PADDING - 52, y + 14, g_tempWinEnabled, g_hoveredToggle == TOGGLE_WIN_ENABLED);
        y += SETTINGS_ROW_HEIGHT + 4;

        DrawSettingsCard(hdc, SETTINGS_PADDING, y, contentWidth, SETTINGS_ROW_HEIGHT);
        RECT fullLabel = { SETTINGS_PADDING + 12, y + 6, 140, y + 24 };
        DrawTextGdiPlus(hdc, L"Fullscreen", fullLabel, Colors::Text, 13.0f);
        RECT fullHk = { SETTINGS_PADDING + 12, y + 24, 200, y + 42 };
        DrawTextGdiPlus(hdc, g_tempHotkeyFullscreen.GetString().c_str(), fullHk, Colors::TextSecondary, 11.0f);
        g_settingsBtns[3].rect = { SETTINGS_WIDTH - SETTINGS_PADDING - 115, y + 10, SETTINGS_WIDTH - SETTINGS_PADDING - 60, y + 38 };
        DrawSettingsButton(hdc, g_settingsBtns[3].rect, g_app.recordingHotkeyType == 3 ? L"..." : L"Record", false, g_hoveredBtn == BTN_RECORD_FULL);
        DrawToggleSwitch(hdc, SETTINGS_WIDTH - SETTINGS_PADDING - 52, y + 14, g_tempFullEnabled, g_hoveredToggle == TOGGLE_FULL_ENABLED);
        y += SETTINGS_ROW_HEIGHT + 4;

        DrawSettingsCard(hdc, SETTINGS_PADDING, y, contentWidth, SETTINGS_ROW_HEIGHT);
        RECT textLabel = { SETTINGS_PADDING + 12, y + 6, 140, y + 24 };
        DrawTextGdiPlus(hdc, L"Text (OCR)", textLabel, Colors::Text, 13.0f);
        RECT textHk = { SETTINGS_PADDING + 12, y + 24, 200, y + 42 };
        DrawTextGdiPlus(hdc, g_tempHotkeyText.GetString().c_str(), textHk, Colors::TextSecondary, 11.0f);
        g_settingsBtns[4].rect = { SETTINGS_WIDTH - SETTINGS_PADDING - 115, y + 10, SETTINGS_WIDTH - SETTINGS_PADDING - 60, y + 38 };
        DrawSettingsButton(hdc, g_settingsBtns[4].rect, g_app.recordingHotkeyType == 4 ? L"..." : L"Record", false, g_hoveredBtn == BTN_RECORD_TEXT);
        DrawToggleSwitch(hdc, SETTINGS_WIDTH - SETTINGS_PADDING - 52, y + 14, g_tempTextEnabled, g_hoveredToggle == TOGGLE_TEXT_ENABLED);
        y += SETTINGS_ROW_HEIGHT + 12;

        RECT sysHeader = { SETTINGS_PADDING, y, SETTINGS_WIDTH - SETTINGS_PADDING, y + 20 };
        DrawTextGdiPlus(hdc, L"System", sysHeader, Colors::TextSecondary, 12.0f);
        y += 28;

        DrawSettingsCard(hdc, SETTINGS_PADDING, y, contentWidth, SETTINGS_ROW_HEIGHT);
        RECT replLabel = { SETTINGS_PADDING + 12, y + 8, SETTINGS_WIDTH - 80, y + 26 };
        DrawTextGdiPlus(hdc, L"Replace Windows Snipping Tool", replLabel, Colors::Text, 13.0f);
        RECT replDesc = { SETTINGS_PADDING + 12, y + 26, SETTINGS_WIDTH - 80, y + 42 };
        DrawTextGdiPlus(hdc, L"Capture Win+Shift+S", replDesc, Colors::TextSecondary, 11.0f);
        DrawToggleSwitch(hdc, SETTINGS_WIDTH - SETTINGS_PADDING - 52, y + 14, g_tempReplaceWin, g_hoveredToggle == TOGGLE_REPLACE_WIN);
        y += SETTINGS_ROW_HEIGHT + 4;

        DrawSettingsCard(hdc, SETTINGS_PADDING, y, contentWidth, SETTINGS_ROW_HEIGHT);
        RECT startLabel = { SETTINGS_PADDING + 12, y + 8, SETTINGS_WIDTH - 80, y + 26 };
        DrawTextGdiPlus(hdc, L"Start with Windows", startLabel, Colors::Text, 13.0f);
        RECT startDesc = { SETTINGS_PADDING + 12, y + 26, SETTINGS_WIDTH - 80, y + 42 };
        DrawTextGdiPlus(hdc, L"Launch automatically", startDesc, Colors::TextSecondary, 11.0f);
        DrawToggleSwitch(hdc, SETTINGS_WIDTH - SETTINGS_PADDING - 52, y + 14, g_tempStartup, g_hoveredToggle == TOGGLE_STARTUP);
        y += SETTINGS_ROW_HEIGHT + 20;

        g_settingsBtns[5].rect = { SETTINGS_WIDTH - SETTINGS_PADDING - 140, y, SETTINGS_WIDTH - SETTINGS_PADDING - 75, y + 32 };
        g_settingsBtns[6].rect = { SETTINGS_WIDTH - SETTINGS_PADDING - 70, y, SETTINGS_WIDTH - SETTINGS_PADDING, y + 32 };
        DrawSettingsButton(hdc, g_settingsBtns[5].rect, L"Save", true, g_hoveredBtn == BTN_SAVE_SETTINGS);
        DrawSettingsButton(hdc, g_settingsBtns[6].rect, L"Cancel", false, g_hoveredBtn == BTN_CANCEL_SETTINGS);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        int newHoveredBtn = 0;
        for (int i = 0; i < 7; i++) {
            if (PtInRect(&g_settingsBtns[i].rect, pt)) {
                newHoveredBtn = g_settingsBtns[i].id;
                break;
            }
        }

        int newHoveredToggle = 0;
        int toggleX = SETTINGS_WIDTH - SETTINGS_PADDING - 56;
        if (x >= toggleX && x <= toggleX + 50) {
            if (y >= 130 && y < 130 + SETTINGS_ROW_HEIGHT) newHoveredToggle = TOGGLE_AUTOSAVE;
            else if (y >= 222 && y < 222 + SETTINGS_ROW_HEIGHT) newHoveredToggle = TOGGLE_RECT_ENABLED;
            else if (y >= 278 && y < 278 + SETTINGS_ROW_HEIGHT) newHoveredToggle = TOGGLE_WIN_ENABLED;
            else if (y >= 334 && y < 334 + SETTINGS_ROW_HEIGHT) newHoveredToggle = TOGGLE_FULL_ENABLED;
            else if (y >= 390 && y < 390 + SETTINGS_ROW_HEIGHT) newHoveredToggle = TOGGLE_TEXT_ENABLED;
            else if (y >= 482 && y < 482 + SETTINGS_ROW_HEIGHT) newHoveredToggle = TOGGLE_REPLACE_WIN;
            else if (y >= 538 && y < 538 + SETTINGS_ROW_HEIGHT) newHoveredToggle = TOGGLE_STARTUP;
        }

        if (newHoveredBtn != g_hoveredBtn || newHoveredToggle != g_hoveredToggle) {
            // Invalidate old button
            if (g_hoveredBtn != 0) {
                for (int i = 0; i < 7; i++) {
                    if (g_settingsBtns[i].id == g_hoveredBtn) {
                        InvalidateRect(hwnd, &g_settingsBtns[i].rect, FALSE);
                        break;
                    }
                }
            }
            // Invalidate new button
            if (newHoveredBtn != 0) {
                for (int i = 0; i < 7; i++) {
                    if (g_settingsBtns[i].id == newHoveredBtn) {
                        InvalidateRect(hwnd, &g_settingsBtns[i].rect, FALSE);
                        break;
                    }
                }
            }
            g_hoveredBtn = newHoveredBtn;
            g_hoveredToggle = newHoveredToggle;
        }

        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_hoveredBtn != 0) {
            for (int i = 0; i < 7; i++) {
                if (g_settingsBtns[i].id == g_hoveredBtn) {
                    InvalidateRect(hwnd, &g_settingsBtns[i].rect, FALSE);
                    break;
                }
            }
        }
        g_hoveredBtn = 0;
        g_hoveredToggle = 0;
        return 0;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        for (int i = 0; i < 7; i++) {
            if (PtInRect(&g_settingsBtns[i].rect, pt)) {
                switch (g_settingsBtns[i].id) {
                case BTN_BROWSE: {
                    BROWSEINFOW bi = {};
                    bi.hwndOwner = hwnd;
                    bi.lpszTitle = L"Select Screenshot Folder";
                    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
                    if (pidl) {
                        wchar_t path[MAX_PATH];
                        if (SHGetPathFromIDListW(pidl, path)) {
                            g_tempSavePath = path;
                            SetWindowTextW(s_hPathEdit, path);
                        }
                        CoTaskMemFree(pidl);
                    }
                    break;
                }
                case BTN_RECORD_RECT:
                    g_app.recordingHotkeyType = (g_app.recordingHotkeyType == 1) ? 0 : 1;
                    InvalidateRect(hwnd, &g_settingsBtns[1].rect, FALSE);
                    break;
                case BTN_RECORD_WIN:
                    g_app.recordingHotkeyType = (g_app.recordingHotkeyType == 2) ? 0 : 2;
                    InvalidateRect(hwnd, &g_settingsBtns[2].rect, FALSE);
                    break;
                case BTN_RECORD_FULL:
                    g_app.recordingHotkeyType = (g_app.recordingHotkeyType == 3) ? 0 : 3;
                    InvalidateRect(hwnd, &g_settingsBtns[3].rect, FALSE);
                    break;
                case BTN_RECORD_TEXT:
                    g_app.recordingHotkeyType = (g_app.recordingHotkeyType == 4) ? 0 : 4;
                    InvalidateRect(hwnd, &g_settingsBtns[4].rect, FALSE);
                    break;
                case BTN_SAVE_SETTINGS: {
                    wchar_t path[MAX_PATH];
                    GetWindowTextW(s_hPathEdit, path, MAX_PATH);
                    g_app.settings.savePath = path;
                    g_app.settings.autoSave = g_tempAutoSave;
                    g_app.settings.hotkeyRect = g_tempHotkeyRect;
                    g_app.settings.hotkeyRect.enabled = g_tempRectEnabled;
                    g_app.settings.hotkeyWindow = g_tempHotkeyWindow;
                    g_app.settings.hotkeyWindow.enabled = g_tempWinEnabled;
                    g_app.settings.hotkeyFullscreen = g_tempHotkeyFullscreen;
                    g_app.settings.hotkeyFullscreen.enabled = g_tempFullEnabled;
                    g_app.settings.hotkeyText = g_tempHotkeyText;
                    g_app.settings.hotkeyText.enabled = g_tempTextEnabled;

                    if (g_tempReplaceWin != g_app.settings.replaceWindowsSnipping) {
                        ApplyWindowsSnippingReplacement(g_tempReplaceWin);
                        g_app.settings.replaceWindowsSnipping = g_tempReplaceWin;
                    }
                    if (g_tempStartup != g_app.settings.runAtStartup) {
                        SetRunAtStartup(g_tempStartup);
                        g_app.settings.runAtStartup = g_tempStartup;
                    }

                    SaveSettings();
                    DestroyWindow(hwnd);
                    break;
                }
                case BTN_CANCEL_SETTINGS:
                    DestroyWindow(hwnd);
                    break;
                }
                return 0;
            }
        }

        int toggleX = SETTINGS_WIDTH - SETTINGS_PADDING - 56;
        if (x >= toggleX && x <= toggleX + 50) {
            RECT toggleRect = { toggleX, 0, toggleX + 50, 0 };
            if (y >= 130 && y < 130 + SETTINGS_ROW_HEIGHT) {
                g_tempAutoSave = !g_tempAutoSave;
                toggleRect.top = 130; toggleRect.bottom = 130 + SETTINGS_ROW_HEIGHT;
                InvalidateRect(hwnd, &toggleRect, FALSE);
            }
            else if (y >= 222 && y < 222 + SETTINGS_ROW_HEIGHT) {
                g_tempRectEnabled = !g_tempRectEnabled;
                toggleRect.top = 222; toggleRect.bottom = 222 + SETTINGS_ROW_HEIGHT;
                InvalidateRect(hwnd, &toggleRect, FALSE);
            }
            else if (y >= 278 && y < 278 + SETTINGS_ROW_HEIGHT) {
                g_tempWinEnabled = !g_tempWinEnabled;
                toggleRect.top = 278; toggleRect.bottom = 278 + SETTINGS_ROW_HEIGHT;
                InvalidateRect(hwnd, &toggleRect, FALSE);
            }
            else if (y >= 334 && y < 334 + SETTINGS_ROW_HEIGHT) {
                g_tempFullEnabled = !g_tempFullEnabled;
                toggleRect.top = 334; toggleRect.bottom = 334 + SETTINGS_ROW_HEIGHT;
                InvalidateRect(hwnd, &toggleRect, FALSE);
            }
            else if (y >= 390 && y < 390 + SETTINGS_ROW_HEIGHT) {
                g_tempTextEnabled = !g_tempTextEnabled;
                toggleRect.top = 390; toggleRect.bottom = 390 + SETTINGS_ROW_HEIGHT;
                InvalidateRect(hwnd, &toggleRect, FALSE);
            }
            else if (y >= 482 && y < 482 + SETTINGS_ROW_HEIGHT) {
                g_tempReplaceWin = !g_tempReplaceWin;
                toggleRect.top = 482; toggleRect.bottom = 482 + SETTINGS_ROW_HEIGHT;
                InvalidateRect(hwnd, &toggleRect, FALSE);
            }
            else if (y >= 538 && y < 538 + SETTINGS_ROW_HEIGHT) {
                g_tempStartup = !g_tempStartup;
                toggleRect.top = 538; toggleRect.bottom = 538 + SETTINGS_ROW_HEIGHT;
                InvalidateRect(hwnd, &toggleRect, FALSE);
            }
        }
        return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (g_app.recordingHotkeyType != 0) {
            UINT vk = (UINT)wParam;
            if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) {
                return 0;
            }

            UINT mods = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
            if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
            if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
            if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) mods |= MOD_WIN;

            HotkeyConfig newHotkey = { mods, vk, true };

            int btnIndex = 0;
            switch (g_app.recordingHotkeyType) {
            case 1: g_tempHotkeyRect = newHotkey; btnIndex = 1; break;
            case 2: g_tempHotkeyWindow = newHotkey; btnIndex = 2; break;
            case 3: g_tempHotkeyFullscreen = newHotkey; btnIndex = 3; break;
            case 4: g_tempHotkeyText = newHotkey; btnIndex = 4; break;
            }

            g_app.recordingHotkeyType = 0;
            if (btnIndex > 0) {
                // Invalidate the hotkey row area (button + hotkey text)
                RECT rowRect = g_settingsBtns[btnIndex].rect;
                rowRect.left = SETTINGS_PADDING;
                InvalidateRect(hwnd, &rowRect, FALSE);
            }
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(hdc, &rc, brush);
        DeleteObject(brush);
        return 1;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdcEdit = (HDC)wParam;
        SetTextColor(hdcEdit, Colors::Text);
        SetBkColor(hdcEdit, Colors::Surface);
        static HBRUSH hBrush = CreateSolidBrush(Colors::Surface);
        return (LRESULT)hBrush;
    }

    case WM_DESTROY:
        g_settings.hwnd = nullptr;
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowSettingsDialog(HWND parent) {
    UnregisterHotKey(g_app.mainWnd, HOTKEY_RECTANGLE);
    UnregisterHotKey(g_app.mainWnd, HOTKEY_WINDOW);
    UnregisterHotKey(g_app.mainWnd, HOTKEY_FULLSCREEN);
    UnregisterHotKey(g_app.mainWnd, HOTKEY_TEXT);

    g_tempAutoSave = g_app.settings.autoSave;
    g_tempRectEnabled = g_app.settings.hotkeyRect.enabled;
    g_tempWinEnabled = g_app.settings.hotkeyWindow.enabled;
    g_tempFullEnabled = g_app.settings.hotkeyFullscreen.enabled;
    g_tempTextEnabled = g_app.settings.hotkeyText.enabled;
    g_tempReplaceWin = g_app.settings.replaceWindowsSnipping;
    g_tempStartup = g_app.settings.runAtStartup;
    g_tempSavePath = g_app.settings.savePath;
    g_tempHotkeyRect = g_app.settings.hotkeyRect;
    g_tempHotkeyWindow = g_app.settings.hotkeyWindow;
    g_tempHotkeyFullscreen = g_app.settings.hotkeyFullscreen;
    g_tempHotkeyText = g_app.settings.hotkeyText;
    g_app.recordingHotkeyType = 0;
    g_hoveredBtn = 0;
    g_hoveredToggle = 0;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = g_app.hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"SnippingToolSettings";
        RegisterClassExW(&wc);
        registered = true;
    }

    int x = (GetSystemMetrics(SM_CXSCREEN) - SETTINGS_WIDTH) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - SETTINGS_HEIGHT) / 2;

    g_settings.hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"SnippingToolSettings", L"Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, SETTINGS_WIDTH, SETTINGS_HEIGHT,
        parent, nullptr, g_app.hInstance, nullptr
    );

    EnableDarkMode(g_settings.hwnd);

    // Enable Mica blur effect (Windows 11 22H2+)
    enum { DWMWA_SYSTEMBACKDROP_TYPE = 38 };
    enum { DWMSBT_MAINWINDOW = 2 };  // Mica
    int backdropType = DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(g_settings.hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));

    // Extend frame into client area for blur
    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(g_settings.hwnd, &margins);

    ShowWindow(g_settings.hwnd, SW_SHOW);

    EnableWindow(parent, FALSE);
    MSG msg;
    while (IsWindow(g_settings.hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);

    RegisterHotkeys();
}
