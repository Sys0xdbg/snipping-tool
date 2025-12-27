#pragma once

#include "common.h"
#include <vector>
#include <string>
#include <mutex>

// Thumbnail entry for gallery
struct ThumbnailEntry {
    std::wstring filepath;
    std::wstring filename;
    HBITMAP thumbnail = nullptr;
    FILETIME modifiedTime = {};
    int originalWidth = 0;
    int originalHeight = 0;
    bool loaded = false;
};

// Gallery window state
struct GalleryState {
    std::vector<ThumbnailEntry> entries;
    int scrollOffset = 0;
    int maxScroll = 0;
    int hoveredIndex = -1;
    int selectedIndex = -1;
    int columns = 4;
    bool loading = false;
    std::mutex cacheMutex;
};

extern GalleryState g_gallery;

// Gallery functions
void ShowGallery();
void CloseGallery();
void RefreshGallery();
void FreeGalleryResources();
LRESULT CALLBACK GalleryWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Helper functions
HBITMAP CreateThumbnailFromFile(const wchar_t* filepath, int thumbWidth, int thumbHeight);
void LoadGalleryThumbnails();
void OpenImageInEditor(const std::wstring& filepath);
