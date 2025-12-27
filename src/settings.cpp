#include "settings.h"
#include "app_state.h"

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
        // Text/OCR hotkey
        file << L"HotkeyTextMod=" << g_app.settings.hotkeyText.modifiers << L"\n";
        file << L"HotkeyTextVK=" << g_app.settings.hotkeyText.vk << L"\n";
        file << L"HotkeyTextEnabled=" << (g_app.settings.hotkeyText.enabled ? L"1" : L"0") << L"\n";
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
                } else if (key == L"HotkeyTextMod") {
                    g_app.settings.hotkeyText.modifiers = std::stoi(value);
                } else if (key == L"HotkeyTextVK") {
                    g_app.settings.hotkeyText.vk = std::stoi(value);
                } else if (key == L"HotkeyTextEnabled") {
                    g_app.settings.hotkeyText.enabled = (value == L"1");
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
