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
#include <objidl.h>
#include <gdiplus.h>
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
#pragma comment(lib, "gdiplus.lib")

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

// Colors - Modern Windows 11 style (refined)
namespace Colors {
    const COLORREF Background = RGB(30, 30, 30);
    const COLORREF Surface = RGB(45, 45, 45);
    const COLORREF SurfaceHover = RGB(60, 60, 60);
    const COLORREF SurfaceActive = RGB(70, 70, 70);
    const COLORREF Accent = RGB(76, 194, 255);       // Lighter blue accent
    const COLORREF AccentHover = RGB(96, 205, 255);
    const COLORREF AccentDark = RGB(0, 120, 212);    // For active states
    const COLORREF Text = RGB(255, 255, 255);
    const COLORREF TextSecondary = RGB(160, 160, 160);
    const COLORREF TextDim = RGB(120, 120, 120);
    const COLORREF Border = RGB(55, 55, 55);
    const COLORREF Divider = RGB(50, 50, 50);
    const COLORREF Success = RGB(108, 203, 95);
}

// Hotkey IDs
#define HOTKEY_RECTANGLE  1
#define HOTKEY_WINDOW     2
#define HOTKEY_FULLSCREEN 3
#define HOTKEY_PRINTSCREEN 4

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
    HWND modePickerWnd = nullptr;
    HWND tooltipWnd = nullptr;
    HINSTANCE hInstance = nullptr;

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

    // GDI+ token
    ULONG_PTR gdiplusToken = 0;
} g_app;

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
    BTN_NEW = 1,
    BTN_MODE_RECT,
    BTN_MODE_WINDOW,
    BTN_MODE_FULLSCREEN,
    BTN_DELAY,
    BTN_SETTINGS
};

ToolbarButton g_buttons[] = {
    { BTN_NEW, L"New", L"Start a new screenshot capture", {}, false, false },
    { BTN_MODE_RECT, L"Rectangle", L"Draw a rectangle to capture a region", {}, true, true },
    { BTN_MODE_WINDOW, L"Window", L"Click a window to capture it", {}, true, false },
    { BTN_MODE_FULLSCREEN, L"Fullscreen", L"Capture the entire screen", {}, true, false },
    { BTN_DELAY, L"Delay", L"Set a timer before capture starts", {}, false, false },
    { BTN_SETTINGS, L"Settings", L"Configure hotkeys and preferences", {}, false, false },
};

const int NUM_BUTTONS = sizeof(g_buttons) / sizeof(g_buttons[0]);
const int TOOLBAR_HEIGHT = 48;
const int BUTTON_SIZE = 36;
const int BUTTON_MARGIN = 6;
const int WINDOW_WIDTH = 420;
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

bool IsWinShiftS(const HotkeyConfig& hk) {
    return (hk.modifiers == (MOD_WIN | MOD_SHIFT) && hk.vk == 'S');
}

void RegisterHotkeys() {
    UnregisterHotKey(g_app.mainWnd, HOTKEY_RECTANGLE);
    UnregisterHotKey(g_app.mainWnd, HOTKEY_WINDOW);
    UnregisterHotKey(g_app.mainWnd, HOTKEY_FULLSCREEN);
    UnregisterHotKey(g_app.mainWnd, HOTKEY_PRINTSCREEN);

    // Print Screen hotkey - always enabled
    RegisterHotKey(g_app.mainWnd, HOTKEY_PRINTSCREEN, MOD_NOREPEAT, VK_SNAPSHOT);

    // Rectangle hotkey - skip if it's Win+Shift+S and we're using the keyboard hook
    if (g_app.settings.hotkeyRect.enabled && g_app.settings.hotkeyRect.vk != 0) {
        // Don't register Win+Shift+S - it's handled by keyboard hook when replacing Windows Snipping Tool
        if (g_app.settings.replaceWindowsSnipping && IsWinShiftS(g_app.settings.hotkeyRect)) {
            // Skip - keyboard hook handles this
        } else {
            BOOL result = RegisterHotKey(g_app.mainWnd, HOTKEY_RECTANGLE,
                g_app.settings.hotkeyRect.modifiers | MOD_NOREPEAT,
                g_app.settings.hotkeyRect.vk);
            if (!result) {
                MessageBoxW(nullptr, L"Failed to register Rectangle hotkey.\nIt may already be in use by another application.",
                    L"Hotkey Registration Failed", MB_ICONWARNING);
            }
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

void DrawIcon(HDC hdc, int id, const RECT& rect, COLORREF color) {
    int cx = (rect.left + rect.right) / 2;
    int cy = (rect.top + rect.bottom) / 2;

    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HPEN penThick = CreatePen(PS_SOLID, 2, color);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    switch (id) {
    case BTN_MODE_RECT: {
        // Clean crop/selection icon - corner brackets only
        int s = 7;  // size
        int c = 4;  // corner length
        SelectObject(hdc, penThick);
        // Top-left corner
        MoveToEx(hdc, cx - s, cy - s + c, nullptr); LineTo(hdc, cx - s, cy - s); LineTo(hdc, cx - s + c, cy - s);
        // Top-right corner
        MoveToEx(hdc, cx + s - c, cy - s, nullptr); LineTo(hdc, cx + s, cy - s); LineTo(hdc, cx + s, cy - s + c);
        // Bottom-right corner
        MoveToEx(hdc, cx + s, cy + s - c, nullptr); LineTo(hdc, cx + s, cy + s); LineTo(hdc, cx + s - c, cy + s);
        // Bottom-left corner
        MoveToEx(hdc, cx - s + c, cy + s, nullptr); LineTo(hdc, cx - s, cy + s); LineTo(hdc, cx - s, cy + s - c);
        break;
    }
    case BTN_MODE_WINDOW: {
        // Simple window icon
        SelectObject(hdc, penThick);
        RoundRect(hdc, cx - 8, cy - 6, cx + 8, cy + 7, 3, 3);
        SelectObject(hdc, pen);
        MoveToEx(hdc, cx - 7, cy - 2, nullptr);
        LineTo(hdc, cx + 7, cy - 2);
        // Window buttons (small dots)
        HBRUSH dotBrush = CreateSolidBrush(color);
        RECT dot1 = { cx + 3, cy - 5, cx + 5, cy - 3 };
        RECT dot2 = { cx + 5, cy - 5, cx + 7, cy - 3 };
        FillRect(hdc, &dot1, dotBrush);
        FillRect(hdc, &dot2, dotBrush);
        DeleteObject(dotBrush);
        break;
    }
    case BTN_MODE_FULLSCREEN: {
        // Monitor/display icon
        SelectObject(hdc, penThick);
        RoundRect(hdc, cx - 9, cy - 6, cx + 9, cy + 4, 2, 2);
        SelectObject(hdc, pen);
        // Stand
        MoveToEx(hdc, cx - 3, cy + 4, nullptr);
        LineTo(hdc, cx - 3, cy + 7);
        LineTo(hdc, cx + 3, cy + 7);
        LineTo(hdc, cx + 3, cy + 4);
        break;
    }
    case BTN_SETTINGS: {
        // Simple gear icon
        SelectObject(hdc, penThick);
        Ellipse(hdc, cx - 3, cy - 3, cx + 4, cy + 4);
        SelectObject(hdc, pen);
        // 6 gear teeth (cleaner look)
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
    } else if (btn.id == BTN_NEW) {
        bgColor = isHovered ? Colors::AccentHover : Colors::Accent;
        iconColor = RGB(0, 0, 0);  // Dark icon on accent
    } else {
        bgColor = isHovered ? Colors::SurfaceHover : Colors::Surface;
        iconColor = isHovered ? Colors::Text : Colors::TextSecondary;
    }

    DrawRoundedRect(hdc, btn.rect, 6, bgColor);

    if (btn.id != BTN_NEW) {
        DrawIcon(hdc, btn.id, btn.rect, iconColor);
    }
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

    // Smaller, cleaner arrow
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

    // New button (wider, accent)
    g_buttons[0].rect = { x, y, x + 56, y + BUTTON_SIZE };
    x += 56 + 16;

    // Mode buttons (grouped together)
    for (int i = 1; i <= 3; i++) {
        g_buttons[i].rect = { x, y, x + BUTTON_SIZE, y + BUTTON_SIZE };
        x += BUTTON_SIZE + 4;
    }

    x += 12;

    // Delay dropdown
    g_buttons[4].rect = { x, y, x + 70, y + BUTTON_SIZE };
    x += 70 + 12;

    // Settings button
    g_buttons[5].rect = { x, y, x + BUTTON_SIZE, y + BUTTON_SIZE };
}

//------------------------------------------------------------------------------
// Tooltip Window
//------------------------------------------------------------------------------
LRESULT CALLBACK TooltipWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        // Get the tooltip text from window property
        const wchar_t* text = (const wchar_t*)GetPropW(hwnd, L"TooltipText");
        if (text) {
            Gdiplus::Graphics graphics(hdc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            // Dark background with rounded corners
            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(240, 45, 45, 45));
            Gdiplus::GraphicsPath path;
            int r = 6;
            path.AddArc(0, 0, r * 2, r * 2, 180, 90);
            path.AddArc(clientRect.right - r * 2, 0, r * 2, r * 2, 270, 90);
            path.AddArc(clientRect.right - r * 2, clientRect.bottom - r * 2, r * 2, r * 2, 0, 90);
            path.AddArc(0, clientRect.bottom - r * 2, r * 2, r * 2, 90, 90);
            path.CloseFigure();
            graphics.FillPath(&bgBrush, &path);

            // Border
            Gdiplus::Pen borderPen(Gdiplus::Color(255, 70, 70, 70), 1.0f);
            graphics.DrawPath(&borderPen, &path);

            // Text
            Gdiplus::FontFamily fontFamily(L"Segoe UI");
            Gdiplus::Font font(&fontFamily, 11, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 220, 220, 220));
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::RectF textRect(0, 0, (float)clientRect.right, (float)clientRect.bottom);
            graphics.DrawString(text, -1, &font, textRect, &format, &textBrush);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void ShowTooltip(const wchar_t* text, int x, int y) {
    // Register class if needed
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = TooltipWndProc;
        wc.hInstance = g_app.hInstance;
        wc.lpszClassName = L"SnippingToolTooltip";
        RegisterClassExW(&wc);
        registered = true;
    }

    // Measure text to size the tooltip
    HDC hdc = GetDC(nullptr);
    Gdiplus::Graphics graphics(hdc);
    Gdiplus::FontFamily fontFamily(L"Segoe UI");
    Gdiplus::Font font(&fontFamily, 11, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::RectF bounds;
    graphics.MeasureString(text, -1, &font, Gdiplus::PointF(0, 0), &bounds);
    ReleaseDC(nullptr, hdc);

    int width = (int)bounds.Width + 16;
    int height = 24;

    // Destroy existing tooltip
    if (g_app.tooltipWnd) {
        RemovePropW(g_app.tooltipWnd, L"TooltipText");
        DestroyWindow(g_app.tooltipWnd);
    }

    g_app.tooltipWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        L"SnippingToolTooltip", L"",
        WS_POPUP,
        x - width / 2, y, width, height,
        nullptr, nullptr, g_app.hInstance, nullptr
    );

    SetLayeredWindowAttributes(g_app.tooltipWnd, 0, 255, LWA_ALPHA);
    SetPropW(g_app.tooltipWnd, L"TooltipText", (HANDLE)text);

    HRGN rgn = CreateRoundRectRgn(0, 0, width + 1, height + 1, 6, 6);
    SetWindowRgn(g_app.tooltipWnd, rgn, TRUE);

    ShowWindow(g_app.tooltipWnd, SW_SHOWNOACTIVATE);
}

void HideTooltip() {
    if (g_app.tooltipWnd) {
        RemovePropW(g_app.tooltipWnd, L"TooltipText");
        DestroyWindow(g_app.tooltipWnd);
        g_app.tooltipWnd = nullptr;
    }
    g_app.lastTooltipButton = -1;
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
// Settings Dialog - Windows 11 Style
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

//------------------------------------------------------------------------------
// Mode Picker Overlay (shown when Print Screen is pressed)
//------------------------------------------------------------------------------
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

ModePickerBtn g_modePickerBtns[] = {
    { MODE_RECTANGLE, L"\u25AD", L"Rectangle", L"Draw a rectangle to capture a region", {} },
    { MODE_WINDOW, L"\u2750", L"Window", L"Click a window to capture it", {} },
    { MODE_FULLSCREEN, L"\u2B1C", L"Fullscreen", L"Capture the entire screen", {} },
    { -1, L"\u2715", L"Close", L"Cancel screenshot", {} },
};

void ShowModePicker();
void HideModePicker();

// Flag to track if we're in print screen mode (with frozen overlay)
bool g_printScreenMode = false;

LRESULT CALLBACK ModePickerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdcScreen = BeginPaint(hwnd, &ps);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        // Double buffering
        HDC hdc = CreateCompatibleDC(hdcScreen);
        HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, clientRect.right, clientRect.bottom);
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdc, hBitmap);

        // Background with rounded corners using GDI+
        Gdiplus::Graphics graphics(hdc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // Dark background
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(245, 40, 40, 40));
        Gdiplus::GraphicsPath bgPath;
        int radius = 12;
        bgPath.AddArc(0, 0, radius * 2, radius * 2, 180, 90);
        bgPath.AddArc(clientRect.right - radius * 2, 0, radius * 2, radius * 2, 270, 90);
        bgPath.AddArc(clientRect.right - radius * 2, clientRect.bottom - radius * 2, radius * 2, radius * 2, 0, 90);
        bgPath.AddArc(0, clientRect.bottom - radius * 2, radius * 2, radius * 2, 90, 90);
        bgPath.CloseFigure();
        graphics.FillPath(&bgBrush, &bgPath);

        // Border
        Gdiplus::Pen borderPen(Gdiplus::Color(255, 60, 60, 60), 1.0f);
        graphics.DrawPath(&borderPen, &bgPath);

        // Set up GDI+ text rendering
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        Gdiplus::FontFamily fontFamily(L"Segoe UI Symbol");
        Gdiplus::Font iconFont(&fontFamily, 14, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::FontFamily textFamily(L"Segoe UI");
        Gdiplus::Font descFont(&textFamily, 11, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

        Gdiplus::StringFormat centerFormat;
        centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
        centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        // Draw buttons
        int x = 16;
        int btnY = 10;

        for (int i = 0; i < 4; i++) {
            g_modePickerBtns[i].rect = { x, btnY, x + MODE_PICKER_BTN_SIZE, btnY + MODE_PICKER_BTN_SIZE };

            bool isHovered = (g_app.modePickerHovered == i);

            // Button background
            if (isHovered) {
                Gdiplus::SolidBrush hoverBrush(Gdiplus::Color(255, 70, 70, 70));
                graphics.FillEllipse(&hoverBrush, (float)(x + 2), (float)(btnY + 2),
                    (float)(MODE_PICKER_BTN_SIZE - 4), (float)(MODE_PICKER_BTN_SIZE - 4));
            }

            // Draw icon with GDI+
            Gdiplus::SolidBrush iconBrush(i == 3 ? Gdiplus::Color(255, 200, 200, 200) : Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::RectF iconRect((float)x, (float)btnY, (float)MODE_PICKER_BTN_SIZE, (float)MODE_PICKER_BTN_SIZE);
            graphics.DrawString(g_modePickerBtns[i].icon, -1, &iconFont, iconRect, &centerFormat, &iconBrush);

            // Add divider before close button
            if (i == 2) {
                Gdiplus::Pen divPen(Gdiplus::Color(255, 80, 80, 80), 1.0f);
                graphics.DrawLine(&divPen, x + MODE_PICKER_BTN_SIZE + 10, btnY + 8,
                    x + MODE_PICKER_BTN_SIZE + 10, btnY + MODE_PICKER_BTN_SIZE - 8);
                x += 20;
            }

            x += MODE_PICKER_BTN_SIZE + 8;
        }

        // Draw description text when hovering
        if (g_app.modePickerHovered >= 0 && g_app.modePickerHovered < 4) {
            Gdiplus::SolidBrush descBrush(Gdiplus::Color(255, 180, 180, 180));
            Gdiplus::RectF descRect(8.0f, (float)(btnY + MODE_PICKER_BTN_SIZE + 4),
                (float)(clientRect.right - 16), 20.0f);
            graphics.DrawString(g_modePickerBtns[g_app.modePickerHovered].description, -1,
                &descFont, descRect, &centerFormat, &descBrush);
        }

        // Copy to screen
        BitBlt(hdcScreen, 0, 0, clientRect.right, clientRect.bottom, hdc, 0, 0, SRCCOPY);

        SelectObject(hdc, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hdc);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        int newHovered = -1;
        for (int i = 0; i < 4; i++) {
            if (PtInRect(&g_modePickerBtns[i].rect, pt)) {
                newHovered = i;
                break;
            }
        }

        if (newHovered != g_app.modePickerHovered) {
            g_app.modePickerHovered = newHovered;
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        g_app.modePickerHovered = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        for (int i = 0; i < 4; i++) {
            if (PtInRect(&g_modePickerBtns[i].rect, pt)) {
                if (g_modePickerBtns[i].mode >= 0) {
                    // Mode selected - just destroy picker, keep overlay
                    // Set to nullptr BEFORE DestroyWindow to prevent WM_KILLFOCUS from interfering
                    g_app.modePickerWnd = nullptr;
                    DestroyWindow(hwnd);
                    PostMessageW(g_app.mainWnd, WM_USER + 101, g_modePickerBtns[i].mode, 0);
                } else {
                    // Close button - hide everything
                    HideModePicker();
                }
                return 0;
            }
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            HideModePicker();
            return 0;
        }
        break;

    case WM_KILLFOCUS:
        // When losing focus in print screen mode, default to rectangle selection
        // But only if the mode picker window still exists (wasn't destroyed by a button click)
        if (g_printScreenMode && g_app.modePickerWnd && IsWindow(g_app.modePickerWnd)) {
            DestroyWindow(g_app.modePickerWnd);
            g_app.modePickerWnd = nullptr;
            PostMessageW(g_app.mainWnd, WM_USER + 101, MODE_RECTANGLE, 0);
        } else if (!g_printScreenMode) {
            // Only hide if not in print screen mode (user clicked away without selecting)
            HideModePicker();
        }
        // If g_printScreenMode is true but window is gone, a mode was selected - do nothing
        return 0;

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowModePicker() {
    if (g_app.modePickerWnd && IsWindowVisible(g_app.modePickerWnd)) {
        HideModePicker();
        return;
    }

    // Hide main window and capture screen
    ShowWindow(g_app.mainWnd, SW_HIDE);
    Sleep(150);  // Brief delay to let window hide

    // Capture the screen
    if (!CaptureScreen()) {
        ShowWindow(g_app.mainWnd, SW_SHOW);
        return;
    }

    // Capture screen to bitmap for overlay
    if (g_app.overlayBitmap) {
        DeleteObject(g_app.overlayBitmap);
    }
    g_app.overlayBitmap = CaptureScreenToBitmap();

    // Show darkened overlay
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(g_app.overlayWnd, HWND_TOPMOST, 0, 0, width, height, SWP_SHOWWINDOW);

    g_printScreenMode = true;
    g_app.isSelecting = false;

    // Register class if needed
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ModePickerWndProc;
        wc.hInstance = g_app.hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"SnippingToolModePicker";
        RegisterClassExW(&wc);
        registered = true;
    }

    // Position at top-center of screen
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int x = (screenWidth - MODE_PICKER_WIDTH) / 2;
    int y = 40;

    g_app.modePickerHovered = -1;

    g_app.modePickerWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        L"SnippingToolModePicker", L"",
        WS_POPUP,
        x, y, MODE_PICKER_WIDTH, MODE_PICKER_HEIGHT,
        nullptr, nullptr, g_app.hInstance, nullptr
    );

    // Set layered window for transparency
    SetLayeredWindowAttributes(g_app.modePickerWnd, 0, 255, LWA_ALPHA);

    // Make it rounded
    HRGN rgn = CreateRoundRectRgn(0, 0, MODE_PICKER_WIDTH + 1, MODE_PICKER_HEIGHT + 1, 12, 12);
    SetWindowRgn(g_app.modePickerWnd, rgn, TRUE);

    ShowWindow(g_app.modePickerWnd, SW_SHOWNOACTIVATE);
    SetForegroundWindow(g_app.modePickerWnd);
    SetFocus(g_app.modePickerWnd);
}

void HideModePicker() {
    if (g_app.modePickerWnd) {
        DestroyWindow(g_app.modePickerWnd);
        g_app.modePickerWnd = nullptr;
    }

    // If in print screen mode and no capture started, hide overlay and show main window
    if (g_printScreenMode) {
        g_printScreenMode = false;
        ShowWindow(g_app.overlayWnd, SW_HIDE);
        ShowWindow(g_app.mainWnd, SW_SHOW);
    }
}

// Settings window state
struct SettingsState {
    HWND hwnd = nullptr;
    int hoveredToggle = 0;  // Which toggle is hovered (0=none)
    int scrollY = 0;
    bool isDragging = false;
    POINT dragStart = {};
} g_settings;

// Draw a Windows 11 style toggle switch with anti-aliasing
void DrawToggleSwitch(HDC hdc, int x, int y, bool isOn, bool isHovered) {
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    int width = 44;
    int height = 22;
    float radius = height / 2.0f;

    // Colors
    Gdiplus::Color trackColor = isOn ? Gdiplus::Color(255, 76, 194, 255) :
                                 (isHovered ? Gdiplus::Color(255, 70, 70, 70) : Gdiplus::Color(255, 50, 50, 50));
    Gdiplus::Color borderColor = isOn ? Gdiplus::Color(255, 76, 194, 255) : Gdiplus::Color(255, 140, 140, 140);
    Gdiplus::Color knobColor(255, 255, 255, 255);

    // Draw track (pill shape)
    Gdiplus::GraphicsPath trackPath;
    trackPath.AddArc((float)x, (float)y, (float)height, (float)height, 90, 180);
    trackPath.AddArc((float)(x + width - height), (float)y, (float)height, (float)height, 270, 180);
    trackPath.CloseFigure();

    // Fill track
    Gdiplus::SolidBrush trackBrush(trackColor);
    graphics.FillPath(&trackBrush, &trackPath);

    // Draw border
    Gdiplus::Pen borderPen(borderColor, 1.5f);
    graphics.DrawPath(&borderPen, &trackPath);

    // Draw knob (circle)
    int knobSize = 14;
    float knobX = isOn ? (float)(x + width - knobSize - 4) : (float)(x + 4);
    float knobY = (float)y + (height - knobSize) / 2.0f;
    Gdiplus::SolidBrush knobBrush(knobColor);
    graphics.FillEllipse(&knobBrush, knobX, knobY, (float)knobSize, (float)knobSize);
}

// Helper function to draw text with GDI+ for smooth rendering
void DrawTextGdiPlus(HDC hdc, const wchar_t* text, const RECT& rect, COLORREF color,
                     float fontSize = 12.0f, bool bold = false, bool center = false) {
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

// Draw a settings card/section
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

// Draw a modern button
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

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textColor);
    RECT textRect = rect;
    DrawTextW(hdc, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

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

// Settings window dimensions
const int SETTINGS_WIDTH = 420;
const int SETTINGS_HEIGHT = 640;
const int SETTINGS_PADDING = 24;
const int SETTINGS_ROW_HEIGHT = 52;

// Settings toggle IDs
#define TOGGLE_AUTOSAVE        1
#define TOGGLE_RECT_ENABLED    2
#define TOGGLE_WIN_ENABLED     3
#define TOGGLE_FULL_ENABLED    4
#define TOGGLE_REPLACE_WIN     5
#define TOGGLE_STARTUP         6

// Button IDs for settings
#define BTN_SAVE_SETTINGS      201
#define BTN_CANCEL_SETTINGS    202
#define BTN_BROWSE             203
#define BTN_RECORD_RECT        204
#define BTN_RECORD_WIN         205
#define BTN_RECORD_FULL        206

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

// Temp settings for editing
bool g_tempAutoSave;
bool g_tempRectEnabled;
bool g_tempWinEnabled;
bool g_tempFullEnabled;
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
    { BTN_SAVE_SETTINGS, L"Save", {}, true },
    { BTN_CANCEL_SETTINGS, L"Cancel", {}, false },
};

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hPathEdit = nullptr;

    switch (msg) {
    case WM_CREATE: {
        // Create path edit control (vertically centered in the path rect at y=88, height=30)
        // Center = 88 + 15 = 103, edit height = 18, so top = 103 - 9 = 94
        hPathEdit = CreateWindowExW(0, L"EDIT", g_tempSavePath.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            SETTINGS_PADDING + 10, 93, SETTINGS_WIDTH - SETTINGS_PADDING * 2 - 105, 20,
            hwnd, (HMENU)IDC_PATH_EDIT, g_app.hInstance, nullptr);
        SendMessageW(hPathEdit, WM_SETFONT, (WPARAM)g_app.fontSmall, TRUE);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdcScreen = BeginPaint(hwnd, &ps);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        // Double buffering - create memory DC
        HDC hdc = CreateCompatibleDC(hdcScreen);
        HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, clientRect.right, clientRect.bottom);
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdc, hBitmap);

        // Background
        HBRUSH bgBrush = CreateSolidBrush(Colors::Background);
        FillRect(hdc, &clientRect, bgBrush);
        DeleteObject(bgBrush);

        int y = SETTINGS_PADDING;
        int contentWidth = SETTINGS_WIDTH - SETTINGS_PADDING * 2;

        // Title
        RECT titleRect = { SETTINGS_PADDING, y, SETTINGS_WIDTH - SETTINGS_PADDING, y + 30 };
        DrawTextGdiPlus(hdc, L"Settings", titleRect, Colors::Text, 18.0f, true);
        y += 40;

        // Save Path section
        RECT pathLabel = { SETTINGS_PADDING, y, SETTINGS_WIDTH - SETTINGS_PADDING, y + 20 };
        DrawTextGdiPlus(hdc, L"Screenshot save location", pathLabel, Colors::TextSecondary, 12.0f);
        y += 24;

        // Path edit background (drawn behind the actual edit control)
        RECT pathBg = { SETTINGS_PADDING, y, SETTINGS_WIDTH - SETTINGS_PADDING - 85, y + 30 };
        DrawRoundedRect(hdc, pathBg, 4, Colors::Surface, Colors::Border, 1);

        // Browse button
        g_settingsBtns[0].rect = { SETTINGS_WIDTH - SETTINGS_PADDING - 75, y, SETTINGS_WIDTH - SETTINGS_PADDING, y + 30 };
        DrawSettingsButton(hdc, g_settingsBtns[0].rect, L"Browse", false, g_hoveredBtn == BTN_BROWSE);
        y += 46;

        // Auto-save toggle row
        DrawSettingsCard(hdc, SETTINGS_PADDING, y, contentWidth, SETTINGS_ROW_HEIGHT);
        RECT autoLabel = { SETTINGS_PADDING + 12, y + 8, SETTINGS_WIDTH - 80, y + 26 };
        DrawTextGdiPlus(hdc, L"Auto-save screenshots", autoLabel, Colors::Text, 13.0f);
        RECT autoDesc = { SETTINGS_PADDING + 12, y + 26, SETTINGS_WIDTH - 80, y + 42 };
        DrawTextGdiPlus(hdc, L"Skip the save dialog", autoDesc, Colors::TextSecondary, 11.0f);
        DrawToggleSwitch(hdc, SETTINGS_WIDTH - SETTINGS_PADDING - 52, y + 14, g_tempAutoSave, g_hoveredToggle == TOGGLE_AUTOSAVE);
        y += SETTINGS_ROW_HEIGHT + 12;

        // Hotkeys section header
        RECT hkHeader = { SETTINGS_PADDING, y, SETTINGS_WIDTH - SETTINGS_PADDING, y + 20 };
        DrawTextGdiPlus(hdc, L"Keyboard shortcuts", hkHeader, Colors::TextSecondary, 12.0f);
        y += 28;

        // Rectangle hotkey
        DrawSettingsCard(hdc, SETTINGS_PADDING, y, contentWidth, SETTINGS_ROW_HEIGHT);
        RECT rectLabel = { SETTINGS_PADDING + 12, y + 6, 140, y + 24 };
        DrawTextGdiPlus(hdc, L"Rectangle", rectLabel, Colors::Text, 13.0f);
        RECT rectHk = { SETTINGS_PADDING + 12, y + 24, 200, y + 42 };
        DrawTextGdiPlus(hdc, g_tempHotkeyRect.GetString().c_str(), rectHk, Colors::TextSecondary, 11.0f);
        g_settingsBtns[1].rect = { SETTINGS_WIDTH - SETTINGS_PADDING - 115, y + 10, SETTINGS_WIDTH - SETTINGS_PADDING - 60, y + 38 };
        DrawSettingsButton(hdc, g_settingsBtns[1].rect, g_app.recordingHotkeyType == 1 ? L"..." : L"Record", false, g_hoveredBtn == BTN_RECORD_RECT);
        DrawToggleSwitch(hdc, SETTINGS_WIDTH - SETTINGS_PADDING - 52, y + 14, g_tempRectEnabled, g_hoveredToggle == TOGGLE_RECT_ENABLED);
        y += SETTINGS_ROW_HEIGHT + 4;

        // Window hotkey
        DrawSettingsCard(hdc, SETTINGS_PADDING, y, contentWidth, SETTINGS_ROW_HEIGHT);
        RECT winLabel = { SETTINGS_PADDING + 12, y + 6, 140, y + 24 };
        DrawTextGdiPlus(hdc, L"Window", winLabel, Colors::Text, 13.0f);
        RECT winHk = { SETTINGS_PADDING + 12, y + 24, 200, y + 42 };
        DrawTextGdiPlus(hdc, g_tempHotkeyWindow.GetString().c_str(), winHk, Colors::TextSecondary, 11.0f);
        g_settingsBtns[2].rect = { SETTINGS_WIDTH - SETTINGS_PADDING - 115, y + 10, SETTINGS_WIDTH - SETTINGS_PADDING - 60, y + 38 };
        DrawSettingsButton(hdc, g_settingsBtns[2].rect, g_app.recordingHotkeyType == 2 ? L"..." : L"Record", false, g_hoveredBtn == BTN_RECORD_WIN);
        DrawToggleSwitch(hdc, SETTINGS_WIDTH - SETTINGS_PADDING - 52, y + 14, g_tempWinEnabled, g_hoveredToggle == TOGGLE_WIN_ENABLED);
        y += SETTINGS_ROW_HEIGHT + 4;

        // Fullscreen hotkey
        DrawSettingsCard(hdc, SETTINGS_PADDING, y, contentWidth, SETTINGS_ROW_HEIGHT);
        RECT fullLabel = { SETTINGS_PADDING + 12, y + 6, 140, y + 24 };
        DrawTextGdiPlus(hdc, L"Fullscreen", fullLabel, Colors::Text, 13.0f);
        RECT fullHk = { SETTINGS_PADDING + 12, y + 24, 200, y + 42 };
        DrawTextGdiPlus(hdc, g_tempHotkeyFullscreen.GetString().c_str(), fullHk, Colors::TextSecondary, 11.0f);
        g_settingsBtns[3].rect = { SETTINGS_WIDTH - SETTINGS_PADDING - 115, y + 10, SETTINGS_WIDTH - SETTINGS_PADDING - 60, y + 38 };
        DrawSettingsButton(hdc, g_settingsBtns[3].rect, g_app.recordingHotkeyType == 3 ? L"..." : L"Record", false, g_hoveredBtn == BTN_RECORD_FULL);
        DrawToggleSwitch(hdc, SETTINGS_WIDTH - SETTINGS_PADDING - 52, y + 14, g_tempFullEnabled, g_hoveredToggle == TOGGLE_FULL_ENABLED);
        y += SETTINGS_ROW_HEIGHT + 12;

        // System section header
        RECT sysHeader = { SETTINGS_PADDING, y, SETTINGS_WIDTH - SETTINGS_PADDING, y + 20 };
        DrawTextGdiPlus(hdc, L"System", sysHeader, Colors::TextSecondary, 12.0f);
        y += 28;

        // Replace Windows Snipping Tool
        DrawSettingsCard(hdc, SETTINGS_PADDING, y, contentWidth, SETTINGS_ROW_HEIGHT);
        RECT replLabel = { SETTINGS_PADDING + 12, y + 8, SETTINGS_WIDTH - 80, y + 26 };
        DrawTextGdiPlus(hdc, L"Replace Windows Snipping Tool", replLabel, Colors::Text, 13.0f);
        RECT replDesc = { SETTINGS_PADDING + 12, y + 26, SETTINGS_WIDTH - 80, y + 42 };
        DrawTextGdiPlus(hdc, L"Capture Win+Shift+S", replDesc, Colors::TextSecondary, 11.0f);
        DrawToggleSwitch(hdc, SETTINGS_WIDTH - SETTINGS_PADDING - 52, y + 14, g_tempReplaceWin, g_hoveredToggle == TOGGLE_REPLACE_WIN);
        y += SETTINGS_ROW_HEIGHT + 4;

        // Run at startup
        DrawSettingsCard(hdc, SETTINGS_PADDING, y, contentWidth, SETTINGS_ROW_HEIGHT);
        RECT startLabel = { SETTINGS_PADDING + 12, y + 8, SETTINGS_WIDTH - 80, y + 26 };
        DrawTextGdiPlus(hdc, L"Start with Windows", startLabel, Colors::Text, 13.0f);
        RECT startDesc = { SETTINGS_PADDING + 12, y + 26, SETTINGS_WIDTH - 80, y + 42 };
        DrawTextGdiPlus(hdc, L"Launch automatically", startDesc, Colors::TextSecondary, 11.0f);
        DrawToggleSwitch(hdc, SETTINGS_WIDTH - SETTINGS_PADDING - 52, y + 14, g_tempStartup, g_hoveredToggle == TOGGLE_STARTUP);
        y += SETTINGS_ROW_HEIGHT + 20;

        // Bottom buttons
        g_settingsBtns[4].rect = { SETTINGS_WIDTH - SETTINGS_PADDING - 140, y, SETTINGS_WIDTH - SETTINGS_PADDING - 75, y + 32 };
        g_settingsBtns[5].rect = { SETTINGS_WIDTH - SETTINGS_PADDING - 70, y, SETTINGS_WIDTH - SETTINGS_PADDING, y + 32 };
        DrawSettingsButton(hdc, g_settingsBtns[4].rect, L"Save", true, g_hoveredBtn == BTN_SAVE_SETTINGS);
        DrawSettingsButton(hdc, g_settingsBtns[5].rect, L"Cancel", false, g_hoveredBtn == BTN_CANCEL_SETTINGS);

        // Copy buffer to screen
        BitBlt(hdcScreen, 0, 0, clientRect.right, clientRect.bottom, hdc, 0, 0, SRCCOPY);

        // Cleanup double buffer
        SelectObject(hdc, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hdc);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        int newHoveredBtn = 0;
        for (int i = 0; i < 6; i++) {
            if (PtInRect(&g_settingsBtns[i].rect, pt)) {
                newHoveredBtn = g_settingsBtns[i].id;
                break;
            }
        }

        // Check toggle hover (simplified - check right side of rows)
        int newHoveredToggle = 0;
        int toggleX = SETTINGS_WIDTH - SETTINGS_PADDING - 56;
        if (x >= toggleX && x <= toggleX + 50) {
            if (y >= 130 && y < 130 + SETTINGS_ROW_HEIGHT) newHoveredToggle = TOGGLE_AUTOSAVE;
            else if (y >= 222 && y < 222 + SETTINGS_ROW_HEIGHT) newHoveredToggle = TOGGLE_RECT_ENABLED;
            else if (y >= 278 && y < 278 + SETTINGS_ROW_HEIGHT) newHoveredToggle = TOGGLE_WIN_ENABLED;
            else if (y >= 334 && y < 334 + SETTINGS_ROW_HEIGHT) newHoveredToggle = TOGGLE_FULL_ENABLED;
            else if (y >= 426 && y < 426 + SETTINGS_ROW_HEIGHT) newHoveredToggle = TOGGLE_REPLACE_WIN;
            else if (y >= 482 && y < 482 + SETTINGS_ROW_HEIGHT) newHoveredToggle = TOGGLE_STARTUP;
        }

        if (newHoveredBtn != g_hoveredBtn || newHoveredToggle != g_hoveredToggle) {
            g_hoveredBtn = newHoveredBtn;
            g_hoveredToggle = newHoveredToggle;
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        g_hoveredBtn = 0;
        g_hoveredToggle = 0;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        // Check button clicks
        for (int i = 0; i < 6; i++) {
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
                            SetWindowTextW(hPathEdit, path);
                        }
                        CoTaskMemFree(pidl);
                    }
                    break;
                }
                case BTN_RECORD_RECT:
                    g_app.recordingHotkeyType = (g_app.recordingHotkeyType == 1) ? 0 : 1;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                case BTN_RECORD_WIN:
                    g_app.recordingHotkeyType = (g_app.recordingHotkeyType == 2) ? 0 : 2;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                case BTN_RECORD_FULL:
                    g_app.recordingHotkeyType = (g_app.recordingHotkeyType == 3) ? 0 : 3;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                case BTN_SAVE_SETTINGS: {
                    // Get path from edit
                    wchar_t path[MAX_PATH];
                    GetWindowTextW(hPathEdit, path, MAX_PATH);
                    g_app.settings.savePath = path;
                    g_app.settings.autoSave = g_tempAutoSave;
                    g_app.settings.hotkeyRect = g_tempHotkeyRect;
                    g_app.settings.hotkeyRect.enabled = g_tempRectEnabled;
                    g_app.settings.hotkeyWindow = g_tempHotkeyWindow;
                    g_app.settings.hotkeyWindow.enabled = g_tempWinEnabled;
                    g_app.settings.hotkeyFullscreen = g_tempHotkeyFullscreen;
                    g_app.settings.hotkeyFullscreen.enabled = g_tempFullEnabled;

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

        // Check toggle clicks
        int toggleX = SETTINGS_WIDTH - SETTINGS_PADDING - 56;
        if (x >= toggleX && x <= toggleX + 50) {
            if (y >= 130 && y < 130 + SETTINGS_ROW_HEIGHT) { g_tempAutoSave = !g_tempAutoSave; InvalidateRect(hwnd, nullptr, FALSE); }
            else if (y >= 222 && y < 222 + SETTINGS_ROW_HEIGHT) { g_tempRectEnabled = !g_tempRectEnabled; InvalidateRect(hwnd, nullptr, FALSE); }
            else if (y >= 278 && y < 278 + SETTINGS_ROW_HEIGHT) { g_tempWinEnabled = !g_tempWinEnabled; InvalidateRect(hwnd, nullptr, FALSE); }
            else if (y >= 334 && y < 334 + SETTINGS_ROW_HEIGHT) { g_tempFullEnabled = !g_tempFullEnabled; InvalidateRect(hwnd, nullptr, FALSE); }
            else if (y >= 426 && y < 426 + SETTINGS_ROW_HEIGHT) { g_tempReplaceWin = !g_tempReplaceWin; InvalidateRect(hwnd, nullptr, FALSE); }
            else if (y >= 482 && y < 482 + SETTINGS_ROW_HEIGHT) { g_tempStartup = !g_tempStartup; InvalidateRect(hwnd, nullptr, FALSE); }
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

            switch (g_app.recordingHotkeyType) {
            case 1: g_tempHotkeyRect = newHotkey; break;
            case 2: g_tempHotkeyWindow = newHotkey; break;
            case 3: g_tempHotkeyFullscreen = newHotkey; break;
            }

            g_app.recordingHotkeyType = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        return 1;

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
    // Unregister hotkeys while dialog is open
    UnregisterHotKey(g_app.mainWnd, HOTKEY_RECTANGLE);
    UnregisterHotKey(g_app.mainWnd, HOTKEY_WINDOW);
    UnregisterHotKey(g_app.mainWnd, HOTKEY_FULLSCREEN);

    // Initialize temp values
    g_tempAutoSave = g_app.settings.autoSave;
    g_tempRectEnabled = g_app.settings.hotkeyRect.enabled;
    g_tempWinEnabled = g_app.settings.hotkeyWindow.enabled;
    g_tempFullEnabled = g_app.settings.hotkeyFullscreen.enabled;
    g_tempReplaceWin = g_app.settings.replaceWindowsSnipping;
    g_tempStartup = g_app.settings.runAtStartup;
    g_tempSavePath = g_app.settings.savePath;
    g_tempHotkeyRect = g_app.settings.hotkeyRect;
    g_tempHotkeyWindow = g_app.settings.hotkeyWindow;
    g_tempHotkeyFullscreen = g_app.settings.hotkeyFullscreen;
    g_app.recordingHotkeyType = 0;
    g_hoveredBtn = 0;
    g_hoveredToggle = 0;

    // Register settings window class if needed
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

    // Create settings window
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
    ShowWindow(g_settings.hwnd, SW_SHOW);

    // Modal loop
    EnableWindow(parent, FALSE);
    MSG msg;
    while (IsWindow(g_settings.hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
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
        case HOTKEY_PRINTSCREEN:
            ShowModePicker();
            break;
        }
        return 0;

    case WM_USER + 101: {
        // Mode selected from mode picker
        g_app.captureMode = (CaptureMode)wParam;

        // Close the mode picker window but keep overlay if in print screen mode
        if (g_app.modePickerWnd) {
            DestroyWindow(g_app.modePickerWnd);
            g_app.modePickerWnd = nullptr;
        }

        if (g_printScreenMode) {
            // Already have frozen screen, just start capture
            g_printScreenMode = false;
            if (g_app.captureMode == MODE_FULLSCREEN) {
                // For fullscreen, save the already captured full screen
                ShowWindow(g_app.overlayWnd, SW_HIDE);

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
                ShowWindow(g_app.mainWnd, SW_SHOW);
            } else {
                // Enable selection on the overlay
                SetForegroundWindow(g_app.overlayWnd);
                SetCapture(g_app.overlayWnd);
            }
        } else {
            if (g_app.captureMode == MODE_FULLSCREEN) {
                CaptureFullscreen();
            } else {
                ShowOverlay();
            }
        }
        return 0;
    }

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
        HDC hdcScreen = BeginPaint(hwnd, &ps);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        // Double buffering
        HDC hdc = CreateCompatibleDC(hdcScreen);
        HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, clientRect.right, clientRect.bottom);
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdc, hBitmap);

        // Background
        HBRUSH bgBrush = CreateSolidBrush(Colors::Background);
        FillRect(hdc, &clientRect, bgBrush);
        DeleteObject(bgBrush);

        // Draw all buttons
        for (int i = 0; i < NUM_BUTTONS; i++) {
            if (i == 4) {
                DrawDelayDropdown(hdc, g_buttons[i].rect, g_app.hoveredButton == g_buttons[i].id);
            } else {
                DrawToolbarButton(hdc, g_buttons[i], g_app.hoveredButton == g_buttons[i].id);
            }
        }

        // Draw subtle divider only between mode buttons and delay
        HPEN divPen = CreatePen(PS_SOLID, 1, Colors::Divider);
        HPEN oldPen = (HPEN)SelectObject(hdc, divPen);
        int divX = g_buttons[3].rect.right + 6;
        int divTop = TOOLBAR_HEIGHT / 2 - 10;
        int divBottom = TOOLBAR_HEIGHT / 2 + 10;
        MoveToEx(hdc, divX, divTop, nullptr);
        LineTo(hdc, divX, divBottom);
        SelectObject(hdc, oldPen);
        DeleteObject(divPen);

        // New button content - just "New" text centered
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0, 0, 0));  // Dark text on accent button

        HFONT oldFont = (HFONT)SelectObject(hdc, g_app.fontSmall);
        RECT newRect = g_buttons[0].rect;
        DrawTextW(hdc, L"+ New", -1, &newRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);

        // Copy to screen
        BitBlt(hdcScreen, 0, 0, clientRect.right, clientRect.bottom, hdc, 0, 0, SRCCOPY);

        SelectObject(hdc, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hdc);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        int newHovered = -1;
        int hoveredIndex = -1;
        for (int i = 0; i < NUM_BUTTONS; i++) {
            POINT pt = { x, y };
            if (PtInRect(&g_buttons[i].rect, pt)) {
                newHovered = g_buttons[i].id;
                hoveredIndex = i;
                break;
            }
        }

        if (newHovered != g_app.hoveredButton) {
            g_app.hoveredButton = newHovered;
            InvalidateRect(hwnd, nullptr, FALSE);

            // Show/hide tooltip
            if (newHovered >= 0 && hoveredIndex >= 0 && g_buttons[hoveredIndex].description) {
                RECT btnRect = g_buttons[hoveredIndex].rect;
                POINT screenPt = { (btnRect.left + btnRect.right) / 2, btnRect.bottom + 8 };
                ClientToScreen(hwnd, &screenPt);
                ShowTooltip(g_buttons[hoveredIndex].description, screenPt.x, screenPt.y);
                g_app.lastTooltipButton = newHovered;
            } else {
                HideTooltip();
            }
        }

        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        g_app.hoveredButton = -1;
        HideTooltip();
        InvalidateRect(hwnd, nullptr, FALSE);
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

    // Shutdown GDI+
    Gdiplus::GdiplusShutdown(g_app.gdiplusToken);

    CoUninitialize();
    return (int)msg.wParam;
}
