#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shlobj.h>
#include <string>
#include <memory>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::ComPtr;

// Forward declarations
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK OverlayWndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK SettingsDlgProc(HWND, UINT, WPARAM, LPARAM);

// Capture modes
enum CaptureMode {
    MODE_RECTANGLE = 0,
    MODE_WINDOW,
    MODE_FULLSCREEN
};

// Colors - Windows 11 style
namespace Colors {
    const COLORREF Background = RGB(32, 32, 32);
    const COLORREF Surface = RGB(44, 44, 44);
    const COLORREF SurfaceHover = RGB(55, 55, 55);
    const COLORREF SurfaceActive = RGB(65, 65, 65);
    const COLORREF Accent = RGB(0, 103, 192);
    const COLORREF AccentHover = RGB(26, 117, 196);
    const COLORREF Text = RGB(255, 255, 255);
    const COLORREF TextSecondary = RGB(180, 180, 180);
    const COLORREF Border = RGB(60, 60, 60);
    const COLORREF Divider = RGB(70, 70, 70);
}

// Hotkey IDs
#define HOTKEY_RECTANGLE  1
#define HOTKEY_WINDOW     2
#define HOTKEY_FULLSCREEN 3

// Hotkey structure
struct HotkeyConfig {
    UINT modifiers = 0;
    UINT vk = 0;
    bool enabled = false;

    std::wstring GetString() const {
        if (vk == 0) return L"None";
        std::wstring result;
        if (modifiers & MOD_WIN) result += L"Win+";
        if (modifiers & MOD_CONTROL) result += L"Ctrl+";
        if (modifiers & MOD_ALT) result += L"Alt+";
        if (modifiers & MOD_SHIFT) result += L"Shift+";

        if (vk >= 'A' && vk <= 'Z') {
            result += (wchar_t)vk;
        } else if (vk >= '0' && vk <= '9') {
            result += (wchar_t)vk;
        } else if (vk >= VK_F1 && vk <= VK_F12) {
            result += L"F" + std::to_wstring(vk - VK_F1 + 1);
        } else if (vk == VK_SNAPSHOT) {
            result += L"PrtSc";
        }
        return result;
    }
};

// Settings structure
struct Settings {
    std::wstring savePath;
    bool autoSave = false;
    // Default hotkeys use Ctrl+Shift (Win+Shift+S is reserved by Windows Snipping Tool)
    HotkeyConfig hotkeyRect = { MOD_CONTROL | MOD_SHIFT, 'S', true };
    HotkeyConfig hotkeyWindow = { MOD_CONTROL | MOD_SHIFT, 'W', false };
    HotkeyConfig hotkeyFullscreen = { MOD_CONTROL | MOD_SHIFT, 'F', false };
    bool replaceWindowsSnipping = false;
    bool runAtStartup = false;
};

// Global state
struct AppState {
    HWND mainWnd = nullptr;
    HWND overlayWnd = nullptr;
    HINSTANCE hInstance = nullptr;

    // Low-level keyboard hook for intercepting Win+Shift+S
    HHOOK keyboardHook = nullptr;

    // UI state
    CaptureMode captureMode = MODE_RECTANGLE;
    int delaySeconds = 0;
    int hoveredButton = -1;

    // Selection state
    bool isSelecting = false;
    POINT startPoint = {};
    POINT endPoint = {};
    RECT selectionRect = {};

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
    int recordingHotkeyType = 0;  // 0=none, 1=rect, 2=window, 3=fullscreen
    HotkeyConfig tempHotkey;
} g_app;

// Button definitions
struct ToolbarButton {
    int id;
    const wchar_t* tooltip;
    RECT rect;
    bool isToggle;
    bool isActive;
};

enum ButtonID {
    BTN_NEW = 1,
    BTN_MODE_RECT,
    BTN_MODE_WINDOW,
    BTN_MODE_FULLSCREEN,
    BTN_DELAY,
    BTN_SETTINGS
};

ToolbarButton g_buttons[] = {
    { BTN_NEW, L"New", {}, false, false },
    { BTN_MODE_RECT, L"Rectangle", {}, true, true },
    { BTN_MODE_WINDOW, L"Window", {}, true, false },
    { BTN_MODE_FULLSCREEN, L"Fullscreen", {}, true, false },
    { BTN_DELAY, L"Delay", {}, false, false },
    { BTN_SETTINGS, L"Settings", {}, false, false },
};

const int NUM_BUTTONS = sizeof(g_buttons) / sizeof(g_buttons[0]);
const int TOOLBAR_HEIGHT = 48;
const int BUTTON_SIZE = 40;
const int BUTTON_MARGIN = 4;
const int WINDOW_WIDTH = 460;
const int WINDOW_HEIGHT = 56;

//------------------------------------------------------------------------------
// Settings Management
//------------------------------------------------------------------------------
std::wstring GetSettingsPath() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        std::wstring settingsDir = std::wstring(path) + L"\\SnippingTool";
        CreateDirectoryW(settingsDir.c_str(), nullptr);
        return settingsDir + L"\\settings.ini";
    }
    return L"settings.ini";
}

std::wstring GetDefaultSavePath() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, 0, path))) {
        std::wstring screenshotsDir = std::wstring(path) + L"\\Screenshots";
        CreateDirectoryW(screenshotsDir.c_str(), nullptr);
        return screenshotsDir;
    }
    return L"";
}

void SaveSettings() {
    std::wofstream file(g_app.settingsPath);
    if (file.is_open()) {
        file << L"[Settings]\n";
        file << L"SavePath=" << g_app.settings.savePath << L"\n";
        file << L"AutoSave=" << (g_app.settings.autoSave ? L"1" : L"0") << L"\n";
        // Rectangle hotkey
        file << L"HotkeyRectMod=" << g_app.settings.hotkeyRect.modifiers << L"\n";
        file << L"HotkeyRectVK=" << g_app.settings.hotkeyRect.vk << L"\n";
        file << L"HotkeyRectEnabled=" << (g_app.settings.hotkeyRect.enabled ? L"1" : L"0") << L"\n";
        // Window hotkey
        file << L"HotkeyWinMod=" << g_app.settings.hotkeyWindow.modifiers << L"\n";
        file << L"HotkeyWinVK=" << g_app.settings.hotkeyWindow.vk << L"\n";
        file << L"HotkeyWinEnabled=" << (g_app.settings.hotkeyWindow.enabled ? L"1" : L"0") << L"\n";
        // Fullscreen hotkey
        file << L"HotkeyFullMod=" << g_app.settings.hotkeyFullscreen.modifiers << L"\n";
        file << L"HotkeyFullVK=" << g_app.settings.hotkeyFullscreen.vk << L"\n";
        file << L"HotkeyFullEnabled=" << (g_app.settings.hotkeyFullscreen.enabled ? L"1" : L"0") << L"\n";
        file << L"ReplaceWindowsSnipping=" << (g_app.settings.replaceWindowsSnipping ? L"1" : L"0") << L"\n";
        file << L"RunAtStartup=" << (g_app.settings.runAtStartup ? L"1" : L"0") << L"\n";
        file.close();
    }
}

void LoadSettings() {
    g_app.settingsPath = GetSettingsPath();
    g_app.settings.savePath = GetDefaultSavePath();

    std::wifstream file(g_app.settingsPath);
    if (file.is_open()) {
        std::wstring line;
        while (std::getline(file, line)) {
            size_t eq = line.find(L'=');
            if (eq != std::wstring::npos) {
                std::wstring key = line.substr(0, eq);
                std::wstring value = line.substr(eq + 1);

                if (key == L"SavePath" && !value.empty()) {
                    g_app.settings.savePath = value;
                } else if (key == L"AutoSave") {
                    g_app.settings.autoSave = (value == L"1");
                } else if (key == L"HotkeyRectMod") {
                    g_app.settings.hotkeyRect.modifiers = std::stoi(value);
                } else if (key == L"HotkeyRectVK") {
                    g_app.settings.hotkeyRect.vk = std::stoi(value);
                } else if (key == L"HotkeyRectEnabled") {
                    g_app.settings.hotkeyRect.enabled = (value == L"1");
                } else if (key == L"HotkeyWinMod") {
                    g_app.settings.hotkeyWindow.modifiers = std::stoi(value);
                } else if (key == L"HotkeyWinVK") {
                    g_app.settings.hotkeyWindow.vk = std::stoi(value);
                } else if (key == L"HotkeyWinEnabled") {
                    g_app.settings.hotkeyWindow.enabled = (value == L"1");
                } else if (key == L"HotkeyFullMod") {
                    g_app.settings.hotkeyFullscreen.modifiers = std::stoi(value);
                } else if (key == L"HotkeyFullVK") {
                    g_app.settings.hotkeyFullscreen.vk = std::stoi(value);
                } else if (key == L"HotkeyFullEnabled") {
                    g_app.settings.hotkeyFullscreen.enabled = (value == L"1");
                } else if (key == L"ReplaceWindowsSnipping") {
                    g_app.settings.replaceWindowsSnipping = (value == L"1");
                } else if (key == L"RunAtStartup") {
                    g_app.settings.runAtStartup = (value == L"1");
                }
            }
        }
        file.close();
    }
}

//------------------------------------------------------------------------------
// Windows Snipping Tool Replacement
//------------------------------------------------------------------------------
std::wstring GetExecutablePath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

void SetRunAtStartup(bool enable) {
    HKEY hKey;
    const wchar_t* keyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            std::wstring exePath = L"\"" + GetExecutablePath() + L"\"";
            RegSetValueExW(hKey, L"SnippingTool", 0, REG_SZ,
                (const BYTE*)exePath.c_str(), (DWORD)((exePath.length() + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(hKey, L"SnippingTool");
        }
        RegCloseKey(hKey);
    }
}

//------------------------------------------------------------------------------
// Low-Level Keyboard Hook for Win+Shift+S interception
//------------------------------------------------------------------------------
#define WM_TRIGGER_CAPTURE (WM_USER + 100)

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_app.settings.replaceWindowsSnipping) {
        KBDLLHOOKSTRUCT* pKey = (KBDLLHOOKSTRUCT*)lParam;

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            // Check for 'S' key
            if (pKey->vkCode == 'S') {
                // Check if Win and Shift are held
                bool winPressed = (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
                bool shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000);
                bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000);
                bool altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000);

                // Win+Shift+S without Ctrl or Alt
                if (winPressed && shiftPressed && !ctrlPressed && !altPressed) {
                    // Post message to main window to trigger capture
                    PostMessageW(g_app.mainWnd, WM_TRIGGER_CAPTURE, MODE_RECTANGLE, 0);
                    // Block the key from reaching Windows Snipping Tool
                    return 1;
                }
            }
        }
    }
    return CallNextHookEx(g_app.keyboardHook, nCode, wParam, lParam);
}

void InstallKeyboardHook() {
    if (g_app.keyboardHook == nullptr) {
        g_app.keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, g_app.hInstance, 0);
    }
}

void UninstallKeyboardHook() {
    if (g_app.keyboardHook != nullptr) {
        UnhookWindowsHookEx(g_app.keyboardHook);
        g_app.keyboardHook = nullptr;
    }
}

void ApplyWindowsSnippingReplacement(bool enable) {
    if (enable) {
        InstallKeyboardHook();
    } else {
        UninstallKeyboardHook();
    }
}

void RegisterHotkeys() {
    UnregisterHotKey(g_app.mainWnd, HOTKEY_RECTANGLE);
    UnregisterHotKey(g_app.mainWnd, HOTKEY_WINDOW);
    UnregisterHotKey(g_app.mainWnd, HOTKEY_FULLSCREEN);

    if (g_app.settings.hotkeyRect.enabled && g_app.settings.hotkeyRect.vk != 0) {
        BOOL result = RegisterHotKey(g_app.mainWnd, HOTKEY_RECTANGLE,
            g_app.settings.hotkeyRect.modifiers | MOD_NOREPEAT,
            g_app.settings.hotkeyRect.vk);
        if (!result) {
            MessageBoxW(nullptr, L"Failed to register Rectangle hotkey.\nIt may already be in use by another application.",
                L"Hotkey Registration Failed", MB_ICONWARNING);
        }
    }
    if (g_app.settings.hotkeyWindow.enabled && g_app.settings.hotkeyWindow.vk != 0) {
        BOOL result = RegisterHotKey(g_app.mainWnd, HOTKEY_WINDOW,
            g_app.settings.hotkeyWindow.modifiers | MOD_NOREPEAT,
            g_app.settings.hotkeyWindow.vk);
        if (!result) {
            MessageBoxW(nullptr, L"Failed to register Window hotkey.\nIt may already be in use by another application.",
                L"Hotkey Registration Failed", MB_ICONWARNING);
        }
    }
    if (g_app.settings.hotkeyFullscreen.enabled && g_app.settings.hotkeyFullscreen.vk != 0) {
        BOOL result = RegisterHotKey(g_app.mainWnd, HOTKEY_FULLSCREEN,
            g_app.settings.hotkeyFullscreen.modifiers | MOD_NOREPEAT,
            g_app.settings.hotkeyFullscreen.vk);
        if (!result) {
            MessageBoxW(nullptr, L"Failed to register Fullscreen hotkey.\nIt may already be in use by another application.",
                L"Hotkey Registration Failed", MB_ICONWARNING);
        }
    }
}

//------------------------------------------------------------------------------
// Dark Mode / Rounded Corners
//------------------------------------------------------------------------------
void EnableDarkMode(HWND hwnd) {
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode));

    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));

    COLORREF captionColor = Colors::Background;
    DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));
}

//------------------------------------------------------------------------------
// DX11 Screen Capture
//------------------------------------------------------------------------------
bool InitializeD3D() {
    HRESULT hr;

    ComPtr<IDXGIFactory1> factory;
    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIAdapter1> adapter;
    hr = factory->EnumAdapters1(0, &adapter);
    if (FAILED(hr)) return false;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(
        adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
        featureLevels, 1, D3D11_SDK_VERSION,
        &g_app.device, &featureLevel, &g_app.context
    );
    if (FAILED(hr)) return false;

    ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(0, &output);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr)) return false;

    DXGI_OUTPUT_DESC outputDesc;
    output->GetDesc(&outputDesc);
    g_app.screenWidth = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    g_app.screenHeight = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

    hr = output1->DuplicateOutput(g_app.device.Get(), &g_app.duplication);
    if (FAILED(hr)) return false;

    return true;
}

bool CaptureScreen() {
    if (!g_app.duplication) return false;

    HRESULT hr;
    ComPtr<IDXGIResource> desktopResource;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;

    g_app.duplication->ReleaseFrame();

    for (int i = 0; i < 10; i++) {
        hr = g_app.duplication->AcquireNextFrame(100, &frameInfo, &desktopResource);
        if (SUCCEEDED(hr)) break;
        if (hr != DXGI_ERROR_WAIT_TIMEOUT) return false;
    }
    if (FAILED(hr)) return false;

    ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);
    if (FAILED(hr)) {
        g_app.duplication->ReleaseFrame();
        return false;
    }

    D3D11_TEXTURE2D_DESC desc;
    desktopTexture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;

    g_app.capturedTexture.Reset();
    hr = g_app.device->CreateTexture2D(&desc, nullptr, &g_app.capturedTexture);
    if (FAILED(hr)) {
        g_app.duplication->ReleaseFrame();
        return false;
    }

    g_app.context->CopyResource(g_app.capturedTexture.Get(), desktopTexture.Get());
    g_app.duplication->ReleaseFrame();

    return true;
}

bool SaveScreenshot(const RECT& region, const wchar_t* filename) {
    if (!g_app.capturedTexture) return false;

    HRESULT hr;
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = g_app.context->Map(g_app.capturedTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    int x = region.left;
    int y = region.top;
    int width = region.right - region.left;
    int height = region.bottom - region.top;

    x = std::max(0, std::min(x, (int)g_app.screenWidth - 1));
    y = std::max(0, std::min(y, (int)g_app.screenHeight - 1));
    width = std::min(width, (int)g_app.screenWidth - x);
    height = std::min(height, (int)g_app.screenHeight - y);

    if (width <= 0 || height <= 0) {
        g_app.context->Unmap(g_app.capturedTexture.Get(), 0);
        return false;
    }

    ComPtr<IWICImagingFactory> wicFactory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr)) {
        g_app.context->Unmap(g_app.capturedTexture.Get(), 0);
        return false;
    }

    ComPtr<IWICBitmap> bitmap;
    hr = wicFactory->CreateBitmapFromMemory(
        g_app.screenWidth, g_app.screenHeight,
        GUID_WICPixelFormat32bppBGRA,
        mapped.RowPitch, mapped.RowPitch * g_app.screenHeight,
        (BYTE*)mapped.pData, &bitmap
    );
    g_app.context->Unmap(g_app.capturedTexture.Get(), 0);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapClipper> clipper;
    wicFactory->CreateBitmapClipper(&clipper);
    WICRect wicRect = { x, y, width, height };
    clipper->Initialize(bitmap.Get(), &wicRect);

    ComPtr<IWICStream> stream;
    wicFactory->CreateStream(&stream);
    stream->InitializeFromFilename(filename, GENERIC_WRITE);

    ComPtr<IWICBitmapEncoder> encoder;
    wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);

    ComPtr<IWICBitmapFrameEncode> frame;
    encoder->CreateNewFrame(&frame, nullptr);
    frame->Initialize(nullptr);
    frame->SetSize(width, height);

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&format);
    frame->WriteSource(clipper.Get(), nullptr);
    frame->Commit();
    encoder->Commit();

    return true;
}

//------------------------------------------------------------------------------
// File Path Generation
//------------------------------------------------------------------------------
std::wstring GenerateAutoFilename() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &time);

    wchar_t filename[256];
    swprintf_s(filename, L"Screenshot_%04d%02d%02d_%02d%02d%02d.png",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);

    if (!g_app.settings.savePath.empty()) {
        return g_app.settings.savePath + L"\\" + filename;
    }
    return filename;
}

bool ShowSaveDialog(wchar_t* filepath, int maxLen) {
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_app.mainWnd;
    ofn.lpstrFilter = L"PNG Image\0*.png\0All Files\0*.*\0";
    ofn.lpstrFile = filepath;
    ofn.nMaxFile = maxLen;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"png";

    if (!g_app.settings.savePath.empty()) {
        ofn.lpstrInitialDir = g_app.settings.savePath.c_str();
    }

    return GetSaveFileNameW(&ofn) != 0;
}

//------------------------------------------------------------------------------
// Drawing Helpers
//------------------------------------------------------------------------------
void DrawRoundedRect(HDC hdc, const RECT& rect, int radius, COLORREF fillColor, COLORREF borderColor = 0, int borderWidth = 0) {
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

void DrawIcon(HDC hdc, int id, const RECT& rect) {
    int cx = (rect.left + rect.right) / 2;
    int cy = (rect.top + rect.bottom) / 2;

    HPEN pen = CreatePen(PS_SOLID, 2, Colors::Text);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    switch (id) {
    case BTN_MODE_RECT: {
        Rectangle(hdc, cx - 10, cy - 8, cx + 10, cy + 8);
        MoveToEx(hdc, cx - 10, cy - 4, nullptr); LineTo(hdc, cx - 10, cy - 8); LineTo(hdc, cx - 6, cy - 8);
        MoveToEx(hdc, cx + 6, cy - 8, nullptr); LineTo(hdc, cx + 10, cy - 8); LineTo(hdc, cx + 10, cy - 4);
        MoveToEx(hdc, cx + 10, cy + 4, nullptr); LineTo(hdc, cx + 10, cy + 8); LineTo(hdc, cx + 6, cy + 8);
        MoveToEx(hdc, cx - 6, cy + 8, nullptr); LineTo(hdc, cx - 10, cy + 8); LineTo(hdc, cx - 10, cy + 4);
        break;
    }
    case BTN_MODE_WINDOW: {
        Rectangle(hdc, cx - 10, cy - 8, cx + 10, cy + 8);
        MoveToEx(hdc, cx - 10, cy - 4, nullptr);
        LineTo(hdc, cx + 10, cy - 4);
        HBRUSH fillBrush = CreateSolidBrush(Colors::Text);
        RECT btn1 = { cx + 2, cy - 7, cx + 5, cy - 5 };
        RECT btn2 = { cx + 6, cy - 7, cx + 9, cy - 5 };
        FillRect(hdc, &btn1, fillBrush);
        FillRect(hdc, &btn2, fillBrush);
        DeleteObject(fillBrush);
        break;
    }
    case BTN_MODE_FULLSCREEN: {
        Rectangle(hdc, cx - 11, cy - 7, cx + 11, cy + 6);
        MoveToEx(hdc, cx - 4, cy + 6, nullptr);
        LineTo(hdc, cx - 4, cy + 9);
        LineTo(hdc, cx + 4, cy + 9);
        LineTo(hdc, cx + 4, cy + 6);
        break;
    }
    case BTN_SETTINGS: {
        // Gear icon
        Ellipse(hdc, cx - 5, cy - 5, cx + 5, cy + 5);
        // Gear teeth
        for (int i = 0; i < 8; i++) {
            double angle = i * 3.14159 / 4;
            int x1 = cx + (int)(7 * cos(angle));
            int y1 = cy + (int)(7 * sin(angle));
            int x2 = cx + (int)(10 * cos(angle));
            int y2 = cy + (int)(10 * sin(angle));
            MoveToEx(hdc, x1, y1, nullptr);
            LineTo(hdc, x2, y2);
        }
        break;
    }
    }

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void DrawToolbarButton(HDC hdc, const ToolbarButton& btn, bool isHovered) {
    COLORREF bgColor;

    if (btn.isToggle && btn.isActive) {
        bgColor = isHovered ? Colors::AccentHover : Colors::Accent;
    } else if (btn.id == BTN_NEW) {
        bgColor = isHovered ? Colors::AccentHover : Colors::Accent;
    } else {
        bgColor = isHovered ? Colors::SurfaceHover : Colors::Surface;
    }

    DrawRoundedRect(hdc, btn.rect, 8, bgColor);

    if (btn.id != BTN_NEW) {
        DrawIcon(hdc, btn.id, btn.rect);
    }
}

void DrawDelayDropdown(HDC hdc, const RECT& rect, bool isHovered) {
    DrawRoundedRect(hdc, rect, 8, isHovered ? Colors::SurfaceHover : Colors::Surface);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, Colors::Text);

    wchar_t text[32];
    if (g_app.delaySeconds == 0) {
        wcscpy_s(text, L"No delay");
    } else {
        swprintf_s(text, L"%d sec", g_app.delaySeconds);
    }

    HFONT oldFont = (HFONT)SelectObject(hdc, g_app.fontSmall);

    RECT textRect = rect;
    textRect.right -= 20;
    DrawTextW(hdc, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, oldFont);

    int ax = rect.right - 15;
    int ay = (rect.top + rect.bottom) / 2 - 2;

    POINT arrow[3] = {
        { ax - 4, ay },
        { ax + 4, ay },
        { ax, ay + 5 }
    };

    HBRUSH arrowBrush = CreateSolidBrush(Colors::Text);
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
    int x = 8;
    int y = (TOOLBAR_HEIGHT - BUTTON_SIZE) / 2;

    // New button (wider)
    g_buttons[0].rect = { x, y, x + 60, y + BUTTON_SIZE };
    x += 60 + 12;

    x += 8;

    // Mode buttons
    for (int i = 1; i <= 3; i++) {
        g_buttons[i].rect = { x, y, x + BUTTON_SIZE, y + BUTTON_SIZE };
        x += BUTTON_SIZE + BUTTON_MARGIN;
    }

    x += 8;

    // Delay dropdown (wider)
    g_buttons[4].rect = { x, y, x + 80, y + BUTTON_SIZE };
    x += 80 + 12;

    x += 8;

    // Settings button
    g_buttons[5].rect = { x, y, x + BUTTON_SIZE, y + BUTTON_SIZE };
}

//------------------------------------------------------------------------------
// Overlay Window (for region selection)
//------------------------------------------------------------------------------
HBITMAP CaptureScreenToBitmap() {
    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);

    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);

    HBITMAP bitmap = CreateCompatibleBitmap(screenDC, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, bitmap);

    BitBlt(memDC, 0, 0, width, height, screenDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);

    return bitmap;
}

void StartCapture();

void ShowOverlay() {
    ShowWindow(g_app.mainWnd, SW_HIDE);

    if (g_app.delaySeconds > 0) {
        Sleep(g_app.delaySeconds * 1000);
    } else {
        Sleep(200);
    }

    if (!CaptureScreen()) {
        MessageBoxW(g_app.mainWnd, L"Failed to capture screen", L"Error", MB_ICONERROR);
        ShowWindow(g_app.mainWnd, SW_SHOW);
        return;
    }

    if (g_app.overlayBitmap) {
        DeleteObject(g_app.overlayBitmap);
    }
    g_app.overlayBitmap = CaptureScreenToBitmap();

    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);

    SetWindowPos(g_app.overlayWnd, HWND_TOPMOST, 0, 0, width, height, SWP_SHOWWINDOW);
    SetForegroundWindow(g_app.overlayWnd);
    SetCapture(g_app.overlayWnd);

    g_app.isSelecting = false;
}

void HideOverlay() {
    ReleaseCapture();
    ShowWindow(g_app.overlayWnd, SW_HIDE);
    ShowWindow(g_app.mainWnd, SW_SHOW);
}

void NormalizeRect(RECT& r) {
    if (r.left > r.right) std::swap(r.left, r.right);
    if (r.top > r.bottom) std::swap(r.top, r.bottom);
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        int width = clientRect.right;
        int height = clientRect.bottom;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
        HBITMAP oldMemBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        HDC srcDC = CreateCompatibleDC(hdc);
        HBITMAP oldSrcBitmap = (HBITMAP)SelectObject(srcDC, g_app.overlayBitmap);
        BitBlt(memDC, 0, 0, width, height, srcDC, 0, 0, SRCCOPY);

        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 160, 0 };
        HDC overlayDC = CreateCompatibleDC(hdc);
        HBITMAP overlayBmp = CreateCompatibleBitmap(hdc, width, height);
        SelectObject(overlayDC, overlayBmp);

        HBRUSH darkBrush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(overlayDC, &clientRect, darkBrush);
        DeleteObject(darkBrush);

        AlphaBlend(memDC, 0, 0, width, height, overlayDC, 0, 0, width, height, blend);

        DeleteObject(overlayBmp);
        DeleteDC(overlayDC);

        if (g_app.isSelecting) {
            RECT sel = g_app.selectionRect;
            NormalizeRect(sel);

            if (sel.right > sel.left && sel.bottom > sel.top) {
                BitBlt(memDC, sel.left, sel.top, sel.right - sel.left, sel.bottom - sel.top,
                       srcDC, sel.left, sel.top, SRCCOPY);

                HPEN pen = CreatePen(PS_SOLID, 2, Colors::Accent);
                HPEN oldPen = (HPEN)SelectObject(memDC, pen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
                Rectangle(memDC, sel.left, sel.top, sel.right, sel.bottom);
                SelectObject(memDC, oldPen);
                SelectObject(memDC, oldBrush);
                DeleteObject(pen);

                wchar_t sizeText[64];
                swprintf_s(sizeText, L"%d x %d", sel.right - sel.left, sel.bottom - sel.top);

                RECT sizeRect = { sel.left, sel.bottom + 8, sel.left + 100, sel.bottom + 30 };
                DrawRoundedRect(memDC, sizeRect, 4, RGB(40, 40, 40));

                SetBkMode(memDC, TRANSPARENT);
                SetTextColor(memDC, Colors::Text);
                HFONT oldFont = (HFONT)SelectObject(memDC, g_app.fontSmall);
                DrawTextW(memDC, sizeText, -1, &sizeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(memDC, oldFont);
            }
        }

        SelectObject(srcDC, oldSrcBitmap);
        DeleteDC(srcDC);

        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, Colors::Text);
        HFONT oldFont = (HFONT)SelectObject(memDC, g_app.fontRegular);

        const wchar_t* text = L"Click and drag to select area  -  Press ESC to cancel";
        RECT textRect = { 0, 20, width, 50 };
        DrawTextW(memDC, text, -1, &textRect, DT_CENTER | DT_SINGLELINE);
        SelectObject(memDC, oldFont);

        BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldMemBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
        g_app.isSelecting = true;
        g_app.startPoint.x = GET_X_LPARAM(lParam);
        g_app.startPoint.y = GET_Y_LPARAM(lParam);
        g_app.endPoint = g_app.startPoint;
        g_app.selectionRect = { g_app.startPoint.x, g_app.startPoint.y,
                                g_app.startPoint.x, g_app.startPoint.y };
        return 0;

    case WM_MOUSEMOVE:
        if (g_app.isSelecting) {
            g_app.endPoint.x = GET_X_LPARAM(lParam);
            g_app.endPoint.y = GET_Y_LPARAM(lParam);
            g_app.selectionRect = { g_app.startPoint.x, g_app.startPoint.y,
                                    g_app.endPoint.x, g_app.endPoint.y };
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (g_app.isSelecting) {
            g_app.isSelecting = false;
            g_app.endPoint.x = GET_X_LPARAM(lParam);
            g_app.endPoint.y = GET_Y_LPARAM(lParam);
            g_app.selectionRect = { g_app.startPoint.x, g_app.startPoint.y,
                                    g_app.endPoint.x, g_app.endPoint.y };
            NormalizeRect(g_app.selectionRect);

            int w = g_app.selectionRect.right - g_app.selectionRect.left;
            int h = g_app.selectionRect.bottom - g_app.selectionRect.top;

            if (w > 5 && h > 5) {
                HideOverlay();

                std::wstring autoPath = GenerateAutoFilename();
                wchar_t filepath[MAX_PATH];
                wcscpy_s(filepath, autoPath.c_str());

                if (g_app.settings.autoSave) {
                    if (SaveScreenshot(g_app.selectionRect, filepath)) {
                        // Success notification could go here
                    } else {
                        MessageBoxW(g_app.mainWnd, L"Failed to save screenshot", L"Error", MB_ICONERROR);
                    }
                } else {
                    if (ShowSaveDialog(filepath, MAX_PATH)) {
                        if (!SaveScreenshot(g_app.selectionRect, filepath)) {
                            MessageBoxW(g_app.mainWnd, L"Failed to save screenshot", L"Error", MB_ICONERROR);
                        }
                    }
                }
            }
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            HideOverlay();
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

//------------------------------------------------------------------------------
// Capture Functions
//------------------------------------------------------------------------------
void CaptureFullscreen() {
    ShowWindow(g_app.mainWnd, SW_HIDE);

    if (g_app.delaySeconds > 0) {
        Sleep(g_app.delaySeconds * 1000);
    } else {
        Sleep(200);
    }

    if (!CaptureScreen()) {
        MessageBoxW(g_app.mainWnd, L"Failed to capture screen", L"Error", MB_ICONERROR);
        ShowWindow(g_app.mainWnd, SW_SHOW);
        return;
    }

    ShowWindow(g_app.mainWnd, SW_SHOW);

    RECT fullscreen = { 0, 0, (LONG)g_app.screenWidth, (LONG)g_app.screenHeight };

    std::wstring autoPath = GenerateAutoFilename();
    wchar_t filepath[MAX_PATH];
    wcscpy_s(filepath, autoPath.c_str());

    if (g_app.settings.autoSave) {
        if (!SaveScreenshot(fullscreen, filepath)) {
            MessageBoxW(g_app.mainWnd, L"Failed to save screenshot", L"Error", MB_ICONERROR);
        }
    } else {
        if (ShowSaveDialog(filepath, MAX_PATH)) {
            if (!SaveScreenshot(fullscreen, filepath)) {
                MessageBoxW(g_app.mainWnd, L"Failed to save screenshot", L"Error", MB_ICONERROR);
            }
        }
    }
}

void StartCapture() {
    switch (g_app.captureMode) {
    case MODE_RECTANGLE:
    case MODE_WINDOW:
        ShowOverlay();
        break;
    case MODE_FULLSCREEN:
        CaptureFullscreen();
        break;
    }
}

void ShowDelayMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_app.delaySeconds == 0 ? MF_CHECKED : 0), 1, L"No delay");
    AppendMenuW(menu, MF_STRING | (g_app.delaySeconds == 3 ? MF_CHECKED : 0), 2, L"3 seconds");
    AppendMenuW(menu, MF_STRING | (g_app.delaySeconds == 5 ? MF_CHECKED : 0), 3, L"5 seconds");
    AppendMenuW(menu, MF_STRING | (g_app.delaySeconds == 10 ? MF_CHECKED : 0), 4, L"10 seconds");

    RECT btnRect = g_buttons[4].rect;
    POINT pt = { btnRect.left, btnRect.bottom };
    ClientToScreen(hwnd, &pt);

    int result = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);

    switch (result) {
    case 1: g_app.delaySeconds = 0; break;
    case 2: g_app.delaySeconds = 3; break;
    case 3: g_app.delaySeconds = 5; break;
    case 4: g_app.delaySeconds = 10; break;
    }

    DestroyMenu(menu);
    InvalidateRect(hwnd, nullptr, TRUE);
}

//------------------------------------------------------------------------------
// Settings Dialog
//------------------------------------------------------------------------------
#define IDC_PATH_EDIT           101
#define IDC_PATH_BROWSE         102
#define IDC_AUTOSAVE            103
// Rectangle hotkey
#define IDC_HOTKEY_RECT_EDIT    110
#define IDC_HOTKEY_RECT_RECORD  111
#define IDC_HOTKEY_RECT_ENABLED 112
// Window hotkey
#define IDC_HOTKEY_WIN_EDIT     120
#define IDC_HOTKEY_WIN_RECORD   121
#define IDC_HOTKEY_WIN_ENABLED  122
// Fullscreen hotkey
#define IDC_HOTKEY_FULL_EDIT    130
#define IDC_HOTKEY_FULL_RECORD  131
#define IDC_HOTKEY_FULL_ENABLED 132
// Other
#define IDC_REPLACE_WINDOWS     140
#define IDC_RUN_STARTUP         141

// Temporary hotkey configs for editing
HotkeyConfig g_tempHotkeyRect;
HotkeyConfig g_tempHotkeyWindow;
HotkeyConfig g_tempHotkeyFullscreen;

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
                // Reset other record buttons
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
                // Reset other record buttons
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
                // Reset other record buttons
                UpdateHotkeyDisplay(hwnd, IDC_HOTKEY_RECT_EDIT, IDC_HOTKEY_RECT_RECORD, g_tempHotkeyRect, false);
                UpdateHotkeyDisplay(hwnd, IDC_HOTKEY_WIN_EDIT, IDC_HOTKEY_WIN_RECORD, g_tempHotkeyWindow, false);
            } else {
                g_app.recordingHotkeyType = 0;
                UpdateHotkeyDisplay(hwnd, IDC_HOTKEY_FULL_EDIT, IDC_HOTKEY_FULL_RECORD, g_tempHotkeyFullscreen, false);
            }
            return TRUE;

        case IDOK: {
            // Save settings
            wchar_t path[MAX_PATH];
            GetDlgItemTextW(hwnd, IDC_PATH_EDIT, path, MAX_PATH);
            g_app.settings.savePath = path;
            g_app.settings.autoSave = (IsDlgButtonChecked(hwnd, IDC_AUTOSAVE) == BST_CHECKED);

            // Save hotkey settings
            g_app.settings.hotkeyRect = g_tempHotkeyRect;
            g_app.settings.hotkeyRect.enabled = (IsDlgButtonChecked(hwnd, IDC_HOTKEY_RECT_ENABLED) == BST_CHECKED);
            g_app.settings.hotkeyWindow = g_tempHotkeyWindow;
            g_app.settings.hotkeyWindow.enabled = (IsDlgButtonChecked(hwnd, IDC_HOTKEY_WIN_ENABLED) == BST_CHECKED);
            g_app.settings.hotkeyFullscreen = g_tempHotkeyFullscreen;
            g_app.settings.hotkeyFullscreen.enabled = (IsDlgButtonChecked(hwnd, IDC_HOTKEY_FULL_ENABLED) == BST_CHECKED);

            bool newReplaceWindows = (IsDlgButtonChecked(hwnd, IDC_REPLACE_WINDOWS) == BST_CHECKED);
            bool newRunAtStartup = (IsDlgButtonChecked(hwnd, IDC_RUN_STARTUP) == BST_CHECKED);

            // Apply Windows Snipping Tool replacement if changed
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

            // Apply startup setting if changed
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

            // Skip modifier-only keys
            if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) {
                return TRUE;
            }

            // Build modifiers
            UINT mods = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
            if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
            if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
            if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) mods |= MOD_WIN;

            HotkeyConfig newHotkey = { mods, vk, true };

            // Update the appropriate temp hotkey and display
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

void ShowSettingsDialog(HWND parent) {
    // Unregister hotkeys while dialog is open
    UnregisterHotKey(g_app.mainWnd, HOTKEY_RECTANGLE);
    UnregisterHotKey(g_app.mainWnd, HOTKEY_WINDOW);
    UnregisterHotKey(g_app.mainWnd, HOTKEY_FULLSCREEN);

    // Create dialog template in memory - needs to be DWORD aligned
    #pragma pack(push, 4)
    struct {
        DWORD style;
        DWORD dwExtendedStyle;
        WORD cdit;
        short x, y, cx, cy;
        WORD menu;
        WORD windowClass;
        WCHAR title[16];
    } dlgTemplate = {
        DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0,
        0,  // No controls in template, we'll create them manually
        0, 0, 380, 340,
        0, 0,
        L"Settings"
    };
    #pragma pack(pop)

    // Create modeless dialog then make it modal
    HWND hDlg = CreateDialogIndirectParamW(
        g_app.hInstance,
        (LPCDLGTEMPLATE)&dlgTemplate,
        parent,
        SettingsDlgProc,
        0
    );

    if (!hDlg) {
        RegisterHotkeys();
        return;
    }

    // Create controls manually
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    auto CreateCtrl = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
        HWND hwnd = CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE,
            x, y, w, h, hDlg, (HMENU)(INT_PTR)id, g_app.hInstance, nullptr);
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)hFont, TRUE);
        return hwnd;
    };

    int yPos = 10;

    // Save Path section
    CreateCtrl(L"STATIC", L"Screenshot Save Path:", SS_LEFT, 10, yPos, 280, 16, -1);
    yPos += 18;
    CreateCtrl(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER, 10, yPos, 280, 22, IDC_PATH_EDIT);
    CreateCtrl(L"BUTTON", L"Browse...", BS_PUSHBUTTON, 298, yPos, 65, 22, IDC_PATH_BROWSE);
    yPos += 28;
    CreateCtrl(L"BUTTON", L"Auto-save (skip save dialog)", BS_AUTOCHECKBOX, 10, yPos, 200, 18, IDC_AUTOSAVE);
    yPos += 28;

    // Hotkeys section header
    CreateCtrl(L"STATIC", L"Hotkeys:", SS_LEFT, 10, yPos, 280, 16, -1);
    yPos += 22;

    // Rectangle hotkey
    CreateCtrl(L"STATIC", L"Rectangle:", SS_LEFT, 10, yPos + 3, 70, 16, -1);
    CreateCtrl(L"EDIT", L"", ES_READONLY | WS_BORDER, 85, yPos, 140, 22, IDC_HOTKEY_RECT_EDIT);
    CreateCtrl(L"BUTTON", L"Record", BS_PUSHBUTTON, 230, yPos, 55, 22, IDC_HOTKEY_RECT_RECORD);
    CreateCtrl(L"BUTTON", L"Enable", BS_AUTOCHECKBOX, 295, yPos + 3, 60, 18, IDC_HOTKEY_RECT_ENABLED);
    yPos += 28;

    // Window hotkey
    CreateCtrl(L"STATIC", L"Window:", SS_LEFT, 10, yPos + 3, 70, 16, -1);
    CreateCtrl(L"EDIT", L"", ES_READONLY | WS_BORDER, 85, yPos, 140, 22, IDC_HOTKEY_WIN_EDIT);
    CreateCtrl(L"BUTTON", L"Record", BS_PUSHBUTTON, 230, yPos, 55, 22, IDC_HOTKEY_WIN_RECORD);
    CreateCtrl(L"BUTTON", L"Enable", BS_AUTOCHECKBOX, 295, yPos + 3, 60, 18, IDC_HOTKEY_WIN_ENABLED);
    yPos += 28;

    // Fullscreen hotkey
    CreateCtrl(L"STATIC", L"Fullscreen:", SS_LEFT, 10, yPos + 3, 70, 16, -1);
    CreateCtrl(L"EDIT", L"", ES_READONLY | WS_BORDER, 85, yPos, 140, 22, IDC_HOTKEY_FULL_EDIT);
    CreateCtrl(L"BUTTON", L"Record", BS_PUSHBUTTON, 230, yPos, 55, 22, IDC_HOTKEY_FULL_RECORD);
    CreateCtrl(L"BUTTON", L"Enable", BS_AUTOCHECKBOX, 295, yPos + 3, 60, 18, IDC_HOTKEY_FULL_ENABLED);
    yPos += 34;

    // Windows Integration section
    CreateCtrl(L"STATIC", L"Windows Integration:", SS_LEFT, 10, yPos, 280, 16, -1);
    yPos += 20;
    CreateCtrl(L"BUTTON", L"Replace Windows Snipping Tool (Win+Shift+S)", BS_AUTOCHECKBOX, 10, yPos, 280, 18, IDC_REPLACE_WINDOWS);
    yPos += 22;
    CreateCtrl(L"BUTTON", L"Run at Windows startup", BS_AUTOCHECKBOX, 10, yPos, 200, 18, IDC_RUN_STARTUP);
    yPos += 34;

    // Buttons
    CreateCtrl(L"BUTTON", L"Save", BS_DEFPUSHBUTTON, 220, yPos, 65, 26, IDOK);
    CreateCtrl(L"BUTTON", L"Cancel", BS_PUSHBUTTON, 295, yPos, 65, 26, IDCANCEL);

    // Initialize temporary hotkey configs
    g_tempHotkeyRect = g_app.settings.hotkeyRect;
    g_tempHotkeyWindow = g_app.settings.hotkeyWindow;
    g_tempHotkeyFullscreen = g_app.settings.hotkeyFullscreen;
    g_app.recordingHotkeyType = 0;

    // Initialize control values
    SetDlgItemTextW(hDlg, IDC_PATH_EDIT, g_app.settings.savePath.c_str());
    CheckDlgButton(hDlg, IDC_AUTOSAVE, g_app.settings.autoSave ? BST_CHECKED : BST_UNCHECKED);

    // Hotkey displays
    SetDlgItemTextW(hDlg, IDC_HOTKEY_RECT_EDIT, g_app.settings.hotkeyRect.GetString().c_str());
    CheckDlgButton(hDlg, IDC_HOTKEY_RECT_ENABLED, g_app.settings.hotkeyRect.enabled ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(hDlg, IDC_HOTKEY_WIN_EDIT, g_app.settings.hotkeyWindow.GetString().c_str());
    CheckDlgButton(hDlg, IDC_HOTKEY_WIN_ENABLED, g_app.settings.hotkeyWindow.enabled ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(hDlg, IDC_HOTKEY_FULL_EDIT, g_app.settings.hotkeyFullscreen.GetString().c_str());
    CheckDlgButton(hDlg, IDC_HOTKEY_FULL_ENABLED, g_app.settings.hotkeyFullscreen.enabled ? BST_CHECKED : BST_UNCHECKED);

    CheckDlgButton(hDlg, IDC_REPLACE_WINDOWS, g_app.settings.replaceWindowsSnipping ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_RUN_STARTUP, g_app.settings.runAtStartup ? BST_CHECKED : BST_UNCHECKED);

    // Resize dialog to fit controls
    SetWindowPos(hDlg, nullptr, 0, 0, 380, yPos + 70, SWP_NOMOVE | SWP_NOZORDER);

    // Center on screen
    RECT rc;
    GetWindowRect(hDlg, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    SetWindowPos(hDlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    ShowWindow(hDlg, SW_SHOW);

    // Modal loop
    EnableWindow(parent, FALSE);
    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);

    // Re-register hotkeys
    RegisterHotkeys();
}

//------------------------------------------------------------------------------
// Main Window
//------------------------------------------------------------------------------
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        UpdateButtonRects();
        // Note: RegisterHotkeys() is called after window creation in wWinMain
        // because g_app.mainWnd is not yet assigned here
        return 0;

    case WM_HOTKEY:
        switch (wParam) {
        case HOTKEY_RECTANGLE:
            g_app.captureMode = MODE_RECTANGLE;
            ShowOverlay();
            break;
        case HOTKEY_WINDOW:
            g_app.captureMode = MODE_WINDOW;
            ShowOverlay();
            break;
        case HOTKEY_FULLSCREEN:
            g_app.captureMode = MODE_FULLSCREEN;
            CaptureFullscreen();
            break;
        }
        return 0;

    case WM_TRIGGER_CAPTURE:
        // Triggered by low-level keyboard hook (Win+Shift+S replacement)
        g_app.captureMode = (CaptureMode)wParam;
        if (g_app.captureMode == MODE_FULLSCREEN) {
            CaptureFullscreen();
        } else {
            ShowOverlay();
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        HBRUSH bgBrush = CreateSolidBrush(Colors::Background);
        FillRect(hdc, &clientRect, bgBrush);
        DeleteObject(bgBrush);

        for (int i = 0; i < NUM_BUTTONS; i++) {
            if (i == 4) {
                DrawDelayDropdown(hdc, g_buttons[i].rect, g_app.hoveredButton == g_buttons[i].id);
            } else {
                DrawToolbarButton(hdc, g_buttons[i], g_app.hoveredButton == g_buttons[i].id);
            }
        }

        HPEN divPen = CreatePen(PS_SOLID, 1, Colors::Divider);
        HPEN oldPen = (HPEN)SelectObject(hdc, divPen);

        int divX = g_buttons[0].rect.right + 10;
        MoveToEx(hdc, divX, 12, nullptr);
        LineTo(hdc, divX, TOOLBAR_HEIGHT - 12);

        divX = g_buttons[3].rect.right + 10;
        MoveToEx(hdc, divX, 12, nullptr);
        LineTo(hdc, divX, TOOLBAR_HEIGHT - 12);

        divX = g_buttons[4].rect.right + 10;
        MoveToEx(hdc, divX, 12, nullptr);
        LineTo(hdc, divX, TOOLBAR_HEIGHT - 12);

        SelectObject(hdc, oldPen);
        DeleteObject(divPen);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, Colors::Text);

        RECT newRect = g_buttons[0].rect;
        int pcx = newRect.left + 18;
        int pcy = (newRect.top + newRect.bottom) / 2;

        HPEN plusPen = CreatePen(PS_SOLID, 2, Colors::Text);
        HPEN oldPlusPen = (HPEN)SelectObject(hdc, plusPen);
        MoveToEx(hdc, pcx - 5, pcy, nullptr);
        LineTo(hdc, pcx + 6, pcy);
        MoveToEx(hdc, pcx, pcy - 5, nullptr);
        LineTo(hdc, pcx, pcy + 6);
        SelectObject(hdc, oldPlusPen);
        DeleteObject(plusPen);

        HFONT oldFont = (HFONT)SelectObject(hdc, g_app.fontSmall);
        RECT textNewRect = newRect;
        textNewRect.left += 20;
        DrawTextW(hdc, L"New", -1, &textNewRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        int newHovered = -1;
        for (int i = 0; i < NUM_BUTTONS; i++) {
            POINT pt = { x, y };
            if (PtInRect(&g_buttons[i].rect, pt)) {
                newHovered = g_buttons[i].id;
                break;
            }
        }

        if (newHovered != g_app.hoveredButton) {
            g_app.hoveredButton = newHovered;
            InvalidateRect(hwnd, nullptr, TRUE);
        }

        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        g_app.hoveredButton = -1;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        for (int i = 0; i < NUM_BUTTONS; i++) {
            POINT pt = { x, y };
            if (PtInRect(&g_buttons[i].rect, pt)) {
                switch (g_buttons[i].id) {
                case BTN_NEW:
                    StartCapture();
                    break;
                case BTN_MODE_RECT:
                    g_app.captureMode = MODE_RECTANGLE;
                    g_buttons[1].isActive = true;
                    g_buttons[2].isActive = false;
                    g_buttons[3].isActive = false;
                    InvalidateRect(hwnd, nullptr, TRUE);
                    break;
                case BTN_MODE_WINDOW:
                    g_app.captureMode = MODE_WINDOW;
                    g_buttons[1].isActive = false;
                    g_buttons[2].isActive = true;
                    g_buttons[3].isActive = false;
                    InvalidateRect(hwnd, nullptr, TRUE);
                    break;
                case BTN_MODE_FULLSCREEN:
                    g_app.captureMode = MODE_FULLSCREEN;
                    g_buttons[1].isActive = false;
                    g_buttons[2].isActive = false;
                    g_buttons[3].isActive = true;
                    InvalidateRect(hwnd, nullptr, TRUE);
                    break;
                case BTN_DELAY:
                    ShowDelayMenu(hwnd);
                    break;
                case BTN_SETTINGS:
                    ShowSettingsDialog(hwnd);
                    break;
                }
                break;
            }
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_RECTANGLE);
        UnregisterHotKey(hwnd, HOTKEY_WINDOW);
        UnregisterHotKey(hwnd, HOTKEY_FULLSCREEN);
        UninstallKeyboardHook();
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

//------------------------------------------------------------------------------
// Entry Point
//------------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

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

    WNDCLASSEXW wcMain = {};
    wcMain.cbSize = sizeof(wcMain);
    wcMain.lpfnWndProc = MainWndProc;
    wcMain.hInstance = hInstance;
    wcMain.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcMain.hbrBackground = nullptr;
    wcMain.lpszClassName = L"SnippingToolMain";
    wcMain.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wcMain);

    WNDCLASSEXW wcOverlay = {};
    wcOverlay.cbSize = sizeof(wcOverlay);
    wcOverlay.lpfnWndProc = OverlayWndProc;
    wcOverlay.hInstance = hInstance;
    wcOverlay.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wcOverlay.lpszClassName = L"SnippingToolOverlay";
    RegisterClassExW(&wcOverlay);

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

    EnableDarkMode(g_app.mainWnd);

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

    ShowWindow(g_app.mainWnd, nCmdShow);
    UpdateWindow(g_app.mainWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_app.overlayBitmap) DeleteObject(g_app.overlayBitmap);
    if (g_app.fontRegular) DeleteObject(g_app.fontRegular);
    if (g_app.fontIcon) DeleteObject(g_app.fontIcon);
    if (g_app.fontSmall) DeleteObject(g_app.fontSmall);

    CoUninitialize();
    return (int)msg.wParam;
}
