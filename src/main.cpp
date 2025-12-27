#include "common.h"
#include "app_state.h"
#include "settings.h"
#include "hotkeys.h"
#include "capture.h"
#include "main_window.h"
#include "overlay.h"
#include "gallery.h"
#include "editor.h"

// Global state definition
AppState g_app;

// Button definitions
ToolbarButton g_buttons[] = {
    { BTN_MODE_RECT, L"Rectangle", L"Capture a rectangular region", {}, true, true },
    { BTN_MODE_WINDOW, L"Window", L"Capture a window", {}, true, false },
    { BTN_MODE_FULLSCREEN, L"Fullscreen", L"Capture entire screen", {}, true, false },
    { BTN_MODE_TEXT, L"Text", L"Capture text from screen (OCR)", {}, true, false },
    { BTN_DELAY, L"Delay", L"Set a timer before capture starts", {}, false, false },
    { BTN_GALLERY, L"Gallery", L"View and edit screenshots", {}, false, false },
    { BTN_SETTINGS, L"Settings", L"Configure hotkeys and preferences", {}, false, false },
};

const int NUM_BUTTONS = sizeof(g_buttons) / sizeof(g_buttons[0]);

//------------------------------------------------------------------------------
// Entry Point
//------------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // Single instance check using mutex
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"SnippingToolSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // App already running - find existing window and show it
        HWND existingWnd = FindWindowW(L"SnippingToolMain", L"Snipping Tool");
        if (existingWnd) {
            ShowWindow(existingWnd, SW_SHOW);
            SetForegroundWindow(existingWnd);
        }
        CloseHandle(hMutex);
        return 0;
    }

    // Enable Per-Monitor DPI awareness for accurate window coordinates
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_app.gdiplusToken, &gdiplusStartupInput, nullptr);

    g_app.hInstance = hInstance;

    // Load settings
    LoadSettings();

    // Create fonts
    g_app.fontRegular = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    g_app.fontIcon = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");

    g_app.fontSmall = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    if (!InitializeD3D()) {
        MessageBoxW(nullptr, L"Failed to initialize DirectX 11", L"Error", MB_ICONERROR);
        return 1;
    }

    // Register main window class
    WNDCLASSEXW wcMain = {};
    wcMain.cbSize = sizeof(wcMain);
    wcMain.lpfnWndProc = MainWndProc;
    wcMain.hInstance = hInstance;
    wcMain.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcMain.hbrBackground = nullptr;
    wcMain.lpszClassName = L"SnippingToolMain";
    wcMain.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    RegisterClassExW(&wcMain);

    // Register overlay window class
    WNDCLASSEXW wcOverlay = {};
    wcOverlay.cbSize = sizeof(wcOverlay);
    wcOverlay.lpfnWndProc = OverlayWndProc;
    wcOverlay.hInstance = hInstance;
    wcOverlay.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wcOverlay.lpszClassName = L"SnippingToolOverlay";
    RegisterClassExW(&wcOverlay);

    // Register gallery window class
    WNDCLASSEXW wcGallery = {};
    wcGallery.cbSize = sizeof(wcGallery);
    wcGallery.lpfnWndProc = GalleryWndProc;
    wcGallery.hInstance = hInstance;
    wcGallery.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcGallery.lpszClassName = L"SnippingToolGallery";
    wcGallery.style = CS_DBLCLKS;
    RegisterClassExW(&wcGallery);

    // Register editor window class
    WNDCLASSEXW wcEditor = {};
    wcEditor.cbSize = sizeof(wcEditor);
    wcEditor.lpfnWndProc = EditorWndProc;
    wcEditor.hInstance = hInstance;
    wcEditor.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcEditor.lpszClassName = L"SnippingToolEditor";
    RegisterClassExW(&wcEditor);

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int winX = (screenWidth - WINDOW_WIDTH) / 2;
    int winY = screenHeight / 3;

    g_app.mainWnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"SnippingToolMain", L"Snipping Tool",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        winX, winY, WINDOW_WIDTH, WINDOW_HEIGHT + GetSystemMetrics(SM_CYCAPTION),
        nullptr, nullptr, hInstance, nullptr
    );

    // Enable dark mode and rounded corners (but not caption color to allow blur)
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(g_app.mainWnd, 20, &darkMode, sizeof(darkMode));  // DWMWA_USE_IMMERSIVE_DARK_MODE

    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_app.mainWnd, 33, &corner, sizeof(corner));  // DWMWA_WINDOW_CORNER_PREFERENCE

    // Enable Mica blur effect (Windows 11 22H2+)
    int backdropType = 2;  // DWMSBT_MAINWINDOW (Mica)
    DwmSetWindowAttribute(g_app.mainWnd, 38, &backdropType, sizeof(backdropType));  // DWMWA_SYSTEMBACKDROP_TYPE

    // Extend frame into client area for blur
    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(g_app.mainWnd, &margins);

    // Register hotkeys now that g_app.mainWnd is set
    RegisterHotkeys();

    // Install keyboard hook if Windows Snipping Tool replacement is enabled
    if (g_app.settings.replaceWindowsSnipping) {
        InstallKeyboardHook();
    }

    g_app.overlayWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"SnippingToolOverlay", L"",
        WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        nullptr, nullptr, hInstance, nullptr
    );

    // Start minimized to tray - don't show main window
    // User can click tray icon or use hotkeys to show/capture

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_app.overlayBitmap) DeleteObject(g_app.overlayBitmap);
    if (g_app.fontRegular) DeleteObject(g_app.fontRegular);
    if (g_app.fontIcon) DeleteObject(g_app.fontIcon);
    if (g_app.fontSmall) DeleteObject(g_app.fontSmall);

    // Shutdown GDI+
    Gdiplus::GdiplusShutdown(g_app.gdiplusToken);

    CoUninitialize();
    CloseHandle(hMutex);
    return (int)msg.wParam;
}
