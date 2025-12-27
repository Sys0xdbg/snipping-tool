#include "gallery.h"
#include "app_state.h"
#include "drawing.h"
#include "editor.h"
#include <algorithm>
#include <thread>

GalleryState g_gallery;

HBITMAP CreateThumbnailFromFile(const wchar_t* filepath, int thumbWidth, int thumbHeight) {
    Gdiplus::Bitmap* original = Gdiplus::Bitmap::FromFile(filepath);
    if (!original || original->GetLastStatus() != Gdiplus::Ok) {
        delete original;
        return nullptr;
    }

    Gdiplus::Bitmap thumbnail(thumbWidth, thumbHeight, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&thumbnail);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // Calculate scaled dimensions maintaining aspect ratio
    float scaleX = (float)thumbWidth / original->GetWidth();
    float scaleY = (float)thumbHeight / original->GetHeight();
    float scale = (std::min)(scaleX, scaleY);
    int scaledW = (int)(original->GetWidth() * scale);
    int scaledH = (int)(original->GetHeight() * scale);
    int offsetX = (thumbWidth - scaledW) / 2;
    int offsetY = (thumbHeight - scaledH) / 2;

    // Fill background
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 30, 30, 30));
    graphics.FillRectangle(&bgBrush, 0, 0, thumbWidth, thumbHeight);

    // Draw scaled image
    graphics.DrawImage(original, offsetX, offsetY, scaledW, scaledH);
    delete original;

    HBITMAP hBitmap = nullptr;
    thumbnail.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hBitmap);
    return hBitmap;
}

void LoadGalleryThumbnails() {
    if (g_app.settings.savePath.empty()) return;

    std::wstring searchPath = g_app.settings.savePath + L"\\*.png";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) return;

    std::vector<ThumbnailEntry> newEntries;

    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            ThumbnailEntry entry;
            entry.filepath = g_app.settings.savePath + L"\\" + findData.cFileName;
            entry.filename = findData.cFileName;
            entry.modifiedTime = findData.ftLastWriteTime;
            entry.loaded = false;
            newEntries.push_back(entry);
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);

    // Sort by modified time (newest first)
    std::sort(newEntries.begin(), newEntries.end(), [](const ThumbnailEntry& a, const ThumbnailEntry& b) {
        return CompareFileTime(&a.modifiedTime, &b.modifiedTime) > 0;
    });

    // Lock and update entries
    {
        std::lock_guard<std::mutex> lock(g_gallery.cacheMutex);

        // Free old thumbnails
        for (auto& entry : g_gallery.entries) {
            if (entry.thumbnail) {
                DeleteObject(entry.thumbnail);
            }
        }

        g_gallery.entries = std::move(newEntries);
    }

    // Load thumbnails in background
    std::thread([&]() {
        for (size_t i = 0; i < g_gallery.entries.size(); i++) {
            std::lock_guard<std::mutex> lock(g_gallery.cacheMutex);
            if (i < g_gallery.entries.size() && !g_gallery.entries[i].loaded) {
                g_gallery.entries[i].thumbnail = CreateThumbnailFromFile(
                    g_gallery.entries[i].filepath.c_str(),
                    GALLERY_THUMB_WIDTH, GALLERY_THUMB_HEIGHT
                );
                g_gallery.entries[i].loaded = true;

                // Request redraw
                if (g_app.galleryWnd) {
                    PostMessage(g_app.galleryWnd, WM_GALLERY_REFRESH, 0, 0);
                }
            }
        }
    }).detach();
}

void ShowGallery() {
    if (g_app.galleryWnd) {
        SetForegroundWindow(g_app.galleryWnd);
        return;
    }

    // Get screen center
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenWidth - GALLERY_WIDTH) / 2;
    int y = (screenHeight - GALLERY_HEIGHT) / 2;

    g_app.galleryWnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"SnippingToolGallery",
        L"Screenshots",
        WS_OVERLAPPEDWINDOW,
        x, y, GALLERY_WIDTH, GALLERY_HEIGHT,
        nullptr, nullptr, g_app.hInstance, nullptr
    );

    if (!g_app.galleryWnd) return;

    // Apply dark mode styling
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(g_app.galleryWnd, 20, &darkMode, sizeof(darkMode));

    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_app.galleryWnd, 33, &corner, sizeof(corner));

    int backdropType = 2; // Mica
    DwmSetWindowAttribute(g_app.galleryWnd, 38, &backdropType, sizeof(backdropType));

    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(g_app.galleryWnd, &margins);

    ShowWindow(g_app.galleryWnd, SW_SHOW);
    UpdateWindow(g_app.galleryWnd);

    // Load thumbnails
    LoadGalleryThumbnails();
}

void CloseGallery() {
    if (g_app.galleryWnd) {
        DestroyWindow(g_app.galleryWnd);
        g_app.galleryWnd = nullptr;
    }
    FreeGalleryResources();
}

void RefreshGallery() {
    LoadGalleryThumbnails();
    if (g_app.galleryWnd) {
        InvalidateRect(g_app.galleryWnd, nullptr, TRUE);
    }
}

void FreeGalleryResources() {
    std::lock_guard<std::mutex> lock(g_gallery.cacheMutex);
    for (auto& entry : g_gallery.entries) {
        if (entry.thumbnail) {
            DeleteObject(entry.thumbnail);
            entry.thumbnail = nullptr;
        }
    }
    g_gallery.entries.clear();
    g_gallery.scrollOffset = 0;
    g_gallery.hoveredIndex = -1;
    g_gallery.selectedIndex = -1;
}

void DrawGalleryItem(HDC hdc, const ThumbnailEntry& entry, const RECT& rect, bool hovered, bool selected) {
    // Background
    COLORREF bgColor = selected ? Colors::AccentDark : (hovered ? Colors::SurfaceHover : Colors::Surface);
    HBRUSH bgBrush = CreateSolidBrush(bgColor);

    // Draw rounded rectangle background
    HPEN pen = CreatePen(PS_SOLID, 1, selected ? Colors::Accent : Colors::Border);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, bgBrush);
    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 12, 12);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(bgBrush);

    // Draw thumbnail
    if (entry.thumbnail) {
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, entry.thumbnail);

        int thumbX = rect.left + (rect.right - rect.left - GALLERY_THUMB_WIDTH) / 2;
        int thumbY = rect.top + 8;

        BitBlt(hdc, thumbX, thumbY, GALLERY_THUMB_WIDTH, GALLERY_THUMB_HEIGHT, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBmp);
        DeleteDC(memDC);
    } else {
        // Loading placeholder
        RECT thumbRect = {
            rect.left + 8, rect.top + 8,
            rect.right - 8, rect.top + 8 + GALLERY_THUMB_HEIGHT
        };
        HBRUSH placeholderBrush = CreateSolidBrush(Colors::Background);
        FillRect(hdc, &thumbRect, placeholderBrush);
        DeleteObject(placeholderBrush);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, Colors::TextDim);
        DrawTextW(hdc, L"Loading...", -1, &thumbRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Draw filename
    RECT textRect = { rect.left + 4, rect.bottom - 28, rect.right - 4, rect.bottom - 4 };
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, Colors::Text);
    HFONT oldFont = (HFONT)SelectObject(hdc, g_app.fontSmall);
    DrawTextW(hdc, entry.filename.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, oldFont);
}

LRESULT CALLBACK GalleryWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_gallery.scrollOffset = 0;
        g_gallery.hoveredIndex = -1;
        g_gallery.selectedIndex = -1;
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        int width = clientRect.right;
        int height = clientRect.bottom;

        // Double buffer
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        // Fill background
        HBRUSH bgBrush = CreateSolidBrush(Colors::Background);
        FillRect(memDC, &clientRect, bgBrush);
        DeleteObject(bgBrush);

        // Draw header
        RECT headerRect = { 0, 0, width, GALLERY_HEADER_HEIGHT };
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, Colors::Text);
        HFONT oldFont = (HFONT)SelectObject(memDC, g_app.fontRegular);

        wchar_t headerText[128];
        swprintf_s(headerText, L"  %zu Screenshots", g_gallery.entries.size());
        DrawTextW(memDC, headerText, -1, &headerRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(memDC, oldFont);

        // Draw divider
        HPEN divPen = CreatePen(PS_SOLID, 1, Colors::Divider);
        HPEN oldPen = (HPEN)SelectObject(memDC, divPen);
        MoveToEx(memDC, 0, GALLERY_HEADER_HEIGHT, nullptr);
        LineTo(memDC, width, GALLERY_HEADER_HEIGHT);
        SelectObject(memDC, oldPen);
        DeleteObject(divPen);

        // Calculate grid layout
        int itemWidth = GALLERY_THUMB_WIDTH + GALLERY_THUMB_PADDING * 2;
        int itemHeight = GALLERY_THUMB_HEIGHT + 40 + GALLERY_THUMB_PADDING;
        g_gallery.columns = (std::max)(1, (width - GALLERY_THUMB_PADDING) / itemWidth);

        // Draw thumbnails
        std::lock_guard<std::mutex> lock(g_gallery.cacheMutex);
        int startY = GALLERY_HEADER_HEIGHT + GALLERY_THUMB_PADDING - g_gallery.scrollOffset;

        for (size_t i = 0; i < g_gallery.entries.size(); i++) {
            int col = i % g_gallery.columns;
            int row = i / g_gallery.columns;

            int x = GALLERY_THUMB_PADDING + col * itemWidth;
            int y = startY + row * itemHeight;

            // Skip if not visible
            if (y + itemHeight < GALLERY_HEADER_HEIGHT || y > height) continue;

            RECT itemRect = { x, y, x + itemWidth - GALLERY_THUMB_PADDING, y + itemHeight - GALLERY_THUMB_PADDING };

            bool hovered = (int)i == g_gallery.hoveredIndex;
            bool selected = (int)i == g_gallery.selectedIndex;

            DrawGalleryItem(memDC, g_gallery.entries[i], itemRect, hovered, selected);
        }

        // Calculate max scroll
        int rows = ((int)g_gallery.entries.size() + g_gallery.columns - 1) / g_gallery.columns;
        int contentHeight = rows * itemHeight + GALLERY_HEADER_HEIGHT + GALLERY_THUMB_PADDING;
        g_gallery.maxScroll = (std::max)(0, contentHeight - height);

        // Blit to screen
        BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_GALLERY_REFRESH:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        int width = clientRect.right;

        int itemWidth = GALLERY_THUMB_WIDTH + GALLERY_THUMB_PADDING * 2;
        int itemHeight = GALLERY_THUMB_HEIGHT + 40 + GALLERY_THUMB_PADDING;
        g_gallery.columns = (std::max)(1, (width - GALLERY_THUMB_PADDING) / itemWidth);

        int startY = GALLERY_HEADER_HEIGHT + GALLERY_THUMB_PADDING - g_gallery.scrollOffset;

        int newHovered = -1;
        if (y > GALLERY_HEADER_HEIGHT) {
            int col = (x - GALLERY_THUMB_PADDING) / itemWidth;
            int row = (y - startY) / itemHeight;

            if (col >= 0 && col < g_gallery.columns) {
                int index = row * g_gallery.columns + col;
                if (index >= 0 && index < (int)g_gallery.entries.size()) {
                    newHovered = index;
                }
            }
        }

        if (newHovered != g_gallery.hoveredIndex) {
            g_gallery.hoveredIndex = newHovered;
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_gallery.hoveredIndex != -1) {
            g_gallery.hoveredIndex = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        if (g_gallery.hoveredIndex >= 0 && g_gallery.hoveredIndex < (int)g_gallery.entries.size()) {
            g_gallery.selectedIndex = g_gallery.hoveredIndex;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        if (g_gallery.hoveredIndex >= 0 && g_gallery.hoveredIndex < (int)g_gallery.entries.size()) {
            OpenImageInEditor(g_gallery.entries[g_gallery.hoveredIndex].filepath);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_gallery.scrollOffset -= delta / 2;
        g_gallery.scrollOffset = (std::max)(0, (std::min)(g_gallery.scrollOffset, g_gallery.maxScroll));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            CloseGallery();
        } else if (wParam == VK_RETURN || wParam == VK_SPACE) {
            if (g_gallery.selectedIndex >= 0 && g_gallery.selectedIndex < (int)g_gallery.entries.size()) {
                OpenImageInEditor(g_gallery.entries[g_gallery.selectedIndex].filepath);
            }
        } else if (wParam == VK_DELETE) {
            if (g_gallery.selectedIndex >= 0 && g_gallery.selectedIndex < (int)g_gallery.entries.size()) {
                std::wstring filepath = g_gallery.entries[g_gallery.selectedIndex].filepath;
                if (MessageBoxW(hwnd, L"Delete this screenshot?", L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    DeleteFileW(filepath.c_str());
                    RefreshGallery();
                }
            }
        }
        return 0;

    case WM_CONTEXTMENU: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (g_gallery.hoveredIndex >= 0 && g_gallery.hoveredIndex < (int)g_gallery.entries.size()) {
            g_gallery.selectedIndex = g_gallery.hoveredIndex;
            InvalidateRect(hwnd, nullptr, FALSE);

            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"Open in Editor");
            AppendMenuW(menu, MF_STRING, 2, L"Open Folder");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 3, L"Delete");

            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, x, y, 0, hwnd, nullptr);
            DestroyMenu(menu);

            std::wstring filepath = g_gallery.entries[g_gallery.selectedIndex].filepath;

            switch (cmd) {
            case 1:
                OpenImageInEditor(filepath);
                break;
            case 2: {
                std::wstring explorerCmd = L"/select,\"" + filepath + L"\"";
                ShellExecuteW(nullptr, L"open", L"explorer.exe", explorerCmd.c_str(), nullptr, SW_SHOWNORMAL);
                break;
            }
            case 3:
                if (MessageBoxW(hwnd, L"Delete this screenshot?", L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    DeleteFileW(filepath.c_str());
                    RefreshGallery();
                }
                break;
            }
        }
        return 0;
    }

    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_CLOSE:
        CloseGallery();
        return 0;

    case WM_DESTROY:
        g_app.galleryWnd = nullptr;
        FreeGalleryResources();
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
