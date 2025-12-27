#pragma once

#include "common.h"

bool InitializeD3D();
bool CaptureScreen();
bool SaveScreenshot(const RECT& region, const wchar_t* filename);
HBITMAP CaptureScreenToBitmap();
std::wstring GenerateAutoFilename();
bool ShowSaveDialog(wchar_t* filepath, int maxLen);
