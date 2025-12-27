#pragma once

#include "common.h"

std::wstring GetSettingsPath();
std::wstring GetDefaultSavePath();
void SaveSettings();
void LoadSettings();
std::wstring GetExecutablePath();
void SetRunAtStartup(bool enable);
