#pragma once

#include "common.h"

// Global application state
struct AppState {
    HWND mainWnd = nullptr;
    HWND overlayWnd = nullptr;
    HWND modePickerWnd = nullptr;
    HWND tooltipWnd = nullptr;
    HWND notificationWnd = nullptr;
    HINSTANCE hInstance = nullptr;

    // Notification state
    std::wstring lastScreenshotPath;
    HBITMAP notificationPreview = nullptr;
    int notificationHovered = -1;

    // Low-level keyboard hook for intercepting Win+Shift+S
    HHOOK keyboardHook = nullptr;

    // Mode picker state
    int modePickerHovered = -1;

    // UI state
    CaptureMode captureMode = MODE_RECTANGLE;
    int delaySeconds = 0;
    int hoveredButton = -1;
    int lastTooltipButton = -1;

    // Selection state
    bool isSelecting = false;
    POINT startPoint = {};
    POINT endPoint = {};
    RECT selectionRect = {};

    // Window capture mode
    HWND hoveredWindow = nullptr;
    RECT hoveredWindowRect = {};
    std::vector<std::pair<HWND, RECT>> windowList;

    // Captured screenshot
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D> capturedTexture;
    UINT screenWidth = 0;
    UINT screenHeight = 0;

    // Overlay background
    HBITMAP overlayBitmap = nullptr;

    // Fonts
    HFONT fontRegular = nullptr;
    HFONT fontIcon = nullptr;
    HFONT fontSmall = nullptr;

    // Settings
    Settings settings;
    std::wstring settingsPath;

    // Hotkey recording state
    int recordingHotkeyType = 0;
    HotkeyConfig tempHotkey;

    // GDI+ token
    ULONG_PTR gdiplusToken = 0;
};

extern AppState g_app;

// Button definitions
struct ToolbarButton {
    int id;
    const wchar_t* tooltip;
    const wchar_t* description;
    RECT rect;
    bool isToggle;
    bool isActive;
};

enum ButtonID {
    BTN_MODE_RECT = 1,
    BTN_MODE_WINDOW,
    BTN_MODE_FULLSCREEN,
    BTN_MODE_TEXT,
    BTN_DELAY,
    BTN_SETTINGS
};

extern ToolbarButton g_buttons[];
extern const int NUM_BUTTONS;
