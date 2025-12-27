#include "editor.h"
#include "undo_manager.h"
#include "app_state.h"
#include "drawing.h"
#include <map>
#include <cmath>

// Map to store editor states for each window
static std::map<HWND, std::unique_ptr<EditorState>> g_editorStates;

EditorState* GetEditorState(HWND hwnd) {
    auto it = g_editorStates.find(hwnd);
    return it != g_editorStates.end() ? it->second.get() : nullptr;
}

void OpenImageInEditor(const std::wstring& filepath) {
    OpenEditor(filepath);
}

// Helper to load image without file locking
static Gdiplus::Bitmap* LoadImageWithoutLock(const wchar_t* filepath) {
    // Load file into memory first
    HANDLE hFile = CreateFileW(filepath, GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return nullptr;

    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return nullptr;
    }

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, fileSize);
    if (!hMem) {
        CloseHandle(hFile);
        return nullptr;
    }

    void* pMem = GlobalLock(hMem);
    DWORD bytesRead;
    ReadFile(hFile, pMem, fileSize, &bytesRead, nullptr);
    GlobalUnlock(hMem);
    CloseHandle(hFile);

    IStream* pStream = nullptr;
    CreateStreamOnHGlobal(hMem, TRUE, &pStream);  // TRUE = free hMem when stream released

    Gdiplus::Bitmap* temp = Gdiplus::Bitmap::FromStream(pStream);
    if (!temp || temp->GetLastStatus() != Gdiplus::Ok) {
        pStream->Release();
        delete temp;
        return nullptr;
    }

    // Clone to detach from stream
    Gdiplus::Bitmap* result = temp->Clone(0, 0, temp->GetWidth(), temp->GetHeight(), PixelFormat32bppARGB);
    delete temp;
    pStream->Release();

    return result;
}

HWND OpenEditor(const std::wstring& filepath) {
    // Create editor state
    auto state = std::make_unique<EditorState>();
    state->filepath = filepath;

    // Load image without locking file
    state->originalImage = LoadImageWithoutLock(filepath.c_str());
    if (!state->originalImage || state->originalImage->GetLastStatus() != Gdiplus::Ok) {
        MessageBoxW(nullptr, L"Failed to load image", L"Error", MB_ICONERROR);
        delete state->originalImage;
        return nullptr;
    }

    // Create display copy
    state->displayImage = state->originalImage->Clone(
        0, 0, state->originalImage->GetWidth(), state->originalImage->GetHeight(),
        PixelFormat32bppARGB
    );

    // Create undo manager
    state->undoManager = new UndoManager();

    // Calculate window size
    int imgWidth = state->originalImage->GetWidth();
    int imgHeight = state->originalImage->GetHeight();

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    int winWidth = (std::min)(imgWidth + 100, screenWidth - 100);
    int winHeight = (std::min)(imgHeight + EDITOR_TOOLBAR_HEIGHT + 80, screenHeight - 100);

    winWidth = (std::max)(winWidth, EDITOR_MIN_WIDTH);
    winHeight = (std::max)(winHeight, EDITOR_MIN_HEIGHT);

    int x = (screenWidth - winWidth) / 2;
    int y = (screenHeight - winHeight) / 2;

    // Extract filename for title
    std::wstring filename = filepath.substr(filepath.find_last_of(L"\\") + 1);
    std::wstring title = L"Edit - " + filename;

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"SnippingToolEditor",
        title.c_str(),
        WS_OVERLAPPEDWINDOW,
        x, y, winWidth, winHeight,
        nullptr, nullptr, g_app.hInstance, nullptr
    );

    if (!hwnd) {
        delete state->originalImage;
        delete state->displayImage;
        delete state->undoManager;
        return nullptr;
    }

    state->hwnd = hwnd;
    g_editorStates[hwnd] = std::move(state);

    // Apply dark mode styling
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode));

    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));

    int backdropType = 2;
    DwmSetWindowAttribute(hwnd, 38, &backdropType, sizeof(backdropType));

    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    g_app.editorWindows.push_back(hwnd);

    return hwnd;
}

void CloseEditor(HWND hwnd) {
    auto it = g_editorStates.find(hwnd);
    if (it != g_editorStates.end()) {
        EditorState* state = it->second.get();

        if (state->unsavedChanges) {
            int result = MessageBoxW(hwnd, L"Save changes before closing?", L"Unsaved Changes",
                MB_YESNOCANCEL | MB_ICONQUESTION);
            if (result == IDYES) {
                SaveEditorImage(state, state->filepath.c_str());
            } else if (result == IDCANCEL) {
                return;
            }
        }

        delete state->originalImage;
        delete state->displayImage;
        delete state->undoManager;
        g_editorStates.erase(it);
    }

    // Remove from editor windows list
    auto& windows = g_app.editorWindows;
    windows.erase(std::remove(windows.begin(), windows.end(), hwnd), windows.end());

    DestroyWindow(hwnd);
}

void CloseAllEditors() {
    auto windows = g_app.editorWindows;  // Copy since CloseEditor modifies the list
    for (HWND hwnd : windows) {
        CloseEditor(hwnd);
    }
}

POINT ScreenToCanvas(EditorState* state, POINT screenPt) {
    if (!state->displayImage) return { 0, 0 };

    int imgWidth = state->displayImage->GetWidth();
    int imgHeight = state->displayImage->GetHeight();
    int canvasWidth = state->canvasRect.right - state->canvasRect.left;
    int canvasHeight = state->canvasRect.bottom - state->canvasRect.top;

    // Calculate the same centering offset used in DrawEditorCanvas
    int centerX = (canvasWidth - (int)(imgWidth * state->zoom)) / 2 + state->panOffset.x;
    int centerY = (canvasHeight - (int)(imgHeight * state->zoom)) / 2 + state->panOffset.y;

    POINT canvasPt;
    canvasPt.x = (int)((screenPt.x - state->canvasRect.left - centerX) / state->zoom);
    canvasPt.y = (int)((screenPt.y - state->canvasRect.top - centerY) / state->zoom);
    return canvasPt;
}

POINT CanvasToScreen(EditorState* state, POINT canvasPt) {
    if (!state->displayImage) return { 0, 0 };

    int imgWidth = state->displayImage->GetWidth();
    int imgHeight = state->displayImage->GetHeight();
    int canvasWidth = state->canvasRect.right - state->canvasRect.left;
    int canvasHeight = state->canvasRect.bottom - state->canvasRect.top;

    int centerX = (canvasWidth - (int)(imgWidth * state->zoom)) / 2 + state->panOffset.x;
    int centerY = (canvasHeight - (int)(imgHeight * state->zoom)) / 2 + state->panOffset.y;

    POINT screenPt;
    screenPt.x = (int)(canvasPt.x * state->zoom + state->canvasRect.left + centerX);
    screenPt.y = (int)(canvasPt.y * state->zoom + state->canvasRect.top + centerY);
    return screenPt;
}

// Tool names for tooltips
static const wchar_t* TOOL_NAMES[] = {
    L"Select (1)",
    L"Arrow (2)",
    L"Rectangle (3)",
    L"Ellipse (4)",
    L"Pen (5)",
    L"Highlighter (6)",
    L"Text (7)",
    L"Blur (8)",
    L"Crop (9)"
};

// Crop handle indices:
// 0=TopLeft, 1=TopCenter, 2=TopRight
// 3=MiddleLeft, 4=MiddleRight
// 5=BottomLeft, 6=BottomCenter, 7=BottomRight
// -1 = inside crop area (for moving), -2 = outside
static const int HANDLE_SIZE = 10;

static int GetCropHandleAtPoint(EditorState* state, int screenX, int screenY) {
    if (!state->cropActive || !state->displayImage) return -2;

    int imgWidth = state->displayImage->GetWidth();
    int imgHeight = state->displayImage->GetHeight();
    int canvasWidth = state->canvasRect.right - state->canvasRect.left;
    int canvasHeight = state->canvasRect.bottom - state->canvasRect.top;
    int centerX = (canvasWidth - (int)(imgWidth * state->zoom)) / 2 + state->panOffset.x;
    int centerY = (canvasHeight - (int)(imgHeight * state->zoom)) / 2 + state->panOffset.y;

    // Convert crop rect to screen coordinates
    int cx = (int)(state->cropRect.left * state->zoom) + state->canvasRect.left + centerX;
    int cy = (int)(state->cropRect.top * state->zoom) + state->canvasRect.top + centerY;
    int cw = (int)((state->cropRect.right - state->cropRect.left) * state->zoom);
    int ch = (int)((state->cropRect.bottom - state->cropRect.top) * state->zoom);

    int hs = HANDLE_SIZE;

    // Define handle positions
    RECT handles[8] = {
        { cx - hs/2, cy - hs/2, cx + hs/2, cy + hs/2 },                     // 0: TopLeft
        { cx + cw/2 - hs/2, cy - hs/2, cx + cw/2 + hs/2, cy + hs/2 },       // 1: TopCenter
        { cx + cw - hs/2, cy - hs/2, cx + cw + hs/2, cy + hs/2 },           // 2: TopRight
        { cx - hs/2, cy + ch/2 - hs/2, cx + hs/2, cy + ch/2 + hs/2 },       // 3: MiddleLeft
        { cx + cw - hs/2, cy + ch/2 - hs/2, cx + cw + hs/2, cy + ch/2 + hs/2 }, // 4: MiddleRight
        { cx - hs/2, cy + ch - hs/2, cx + hs/2, cy + ch + hs/2 },           // 5: BottomLeft
        { cx + cw/2 - hs/2, cy + ch - hs/2, cx + cw/2 + hs/2, cy + ch + hs/2 }, // 6: BottomCenter
        { cx + cw - hs/2, cy + ch - hs/2, cx + cw + hs/2, cy + ch + hs/2 }  // 7: BottomRight
    };

    POINT pt = { screenX, screenY };
    for (int i = 0; i < 8; i++) {
        if (PtInRect(&handles[i], pt)) return i;
    }

    // Check if inside crop area
    RECT cropScreen = { cx, cy, cx + cw, cy + ch };
    if (PtInRect(&cropScreen, pt)) return -1;  // Inside, can move

    return -2;  // Outside
}

static HCURSOR GetCropCursor(int handle) {
    switch (handle) {
        case 0: case 7: return LoadCursor(nullptr, IDC_SIZENWSE);  // TopLeft, BottomRight
        case 2: case 5: return LoadCursor(nullptr, IDC_SIZENESW);  // TopRight, BottomLeft
        case 1: case 6: return LoadCursor(nullptr, IDC_SIZENS);    // Top, Bottom
        case 3: case 4: return LoadCursor(nullptr, IDC_SIZEWE);    // Left, Right
        case -1: return LoadCursor(nullptr, IDC_SIZEALL);          // Move
        default: return LoadCursor(nullptr, IDC_ARROW);
    }
}

void ApplyBlurToRegion(Gdiplus::Bitmap* bitmap, const RECT& region, int blockSize) {
    if (!bitmap) return;

    int imgWidth = bitmap->GetWidth();
    int imgHeight = bitmap->GetHeight();

    // Clamp region to image bounds
    int x1 = (std::max)(0, (int)region.left);
    int y1 = (std::max)(0, (int)region.top);
    int x2 = (std::min)(imgWidth, (int)region.right);
    int y2 = (std::min)(imgHeight, (int)region.bottom);

    if (x2 <= x1 || y2 <= y1) return;

    Gdiplus::Rect lockRect(x1, y1, x2 - x1, y2 - y1);
    Gdiplus::BitmapData data;

    if (bitmap->LockBits(&lockRect, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
        PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        return;
    }

    BYTE* pixels = (BYTE*)data.Scan0;
    int stride = data.Stride;
    int width = data.Width;
    int height = data.Height;

    // Pixelation: average each block
    for (int by = 0; by < height; by += blockSize) {
        for (int bx = 0; bx < width; bx += blockSize) {
            int r = 0, g = 0, b = 0, a = 0, count = 0;

            // Calculate average
            for (int y = by; y < by + blockSize && y < height; y++) {
                for (int x = bx; x < bx + blockSize && x < width; x++) {
                    BYTE* p = pixels + y * stride + x * 4;
                    b += p[0]; g += p[1]; r += p[2]; a += p[3];
                    count++;
                }
            }

            if (count > 0) {
                r /= count; g /= count; b /= count; a /= count;

                // Apply average to all pixels in block
                for (int y = by; y < by + blockSize && y < height; y++) {
                    for (int x = bx; x < bx + blockSize && x < width; x++) {
                        BYTE* p = pixels + y * stride + x * 4;
                        p[0] = (BYTE)b; p[1] = (BYTE)g; p[2] = (BYTE)r; p[3] = (BYTE)a;
                    }
                }
            }
        }
    }

    bitmap->UnlockBits(&data);
}

// Object drawing implementations
void ArrowObject::Draw(Gdiplus::Graphics& g, float zoom, POINT offset) {
    Gdiplus::Pen pen(color, thickness * zoom);
    pen.SetEndCap(Gdiplus::LineCapArrowAnchor);
    pen.SetStartCap(Gdiplus::LineCapRound);

    float x1 = start.x * zoom + offset.x;
    float y1 = start.y * zoom + offset.y;
    float x2 = end.x * zoom + offset.x;
    float y2 = end.y * zoom + offset.y;

    // Draw line
    g.DrawLine(&pen, x1, y1, x2, y2);

    // Draw arrowhead manually for better control
    float angle = atan2f(y2 - y1, x2 - x1);
    float headLen = headSize * zoom;

    Gdiplus::PointF arrowPts[3];
    arrowPts[0] = Gdiplus::PointF(x2, y2);
    arrowPts[1] = Gdiplus::PointF(x2 - headLen * cosf(angle - 0.4f), y2 - headLen * sinf(angle - 0.4f));
    arrowPts[2] = Gdiplus::PointF(x2 - headLen * cosf(angle + 0.4f), y2 - headLen * sinf(angle + 0.4f));

    Gdiplus::SolidBrush brush(color);
    g.FillPolygon(&brush, arrowPts, 3);
}

RECT ArrowObject::GetBounds() const {
    RECT r;
    r.left = (std::min)(start.x, end.x) - headSize;
    r.top = (std::min)(start.y, end.y) - headSize;
    r.right = (std::max)(start.x, end.x) + headSize;
    r.bottom = (std::max)(start.y, end.y) + headSize;
    return r;
}

void ShapeObject::Draw(Gdiplus::Graphics& g, float zoom, POINT offset) {
    float x = bounds.left * zoom + offset.x;
    float y = bounds.top * zoom + offset.y;
    float w = (bounds.right - bounds.left) * zoom;
    float h = (bounds.bottom - bounds.top) * zoom;

    if (filled) {
        Gdiplus::SolidBrush brush(fillColor);
        if (isEllipse) {
            g.FillEllipse(&brush, x, y, w, h);
        } else {
            g.FillRectangle(&brush, x, y, w, h);
        }
    }

    Gdiplus::Pen pen(strokeColor, thickness * zoom);
    if (isEllipse) {
        g.DrawEllipse(&pen, x, y, w, h);
    } else {
        g.DrawRectangle(&pen, x, y, w, h);
    }
}

void PathObject::Draw(Gdiplus::Graphics& g, float zoom, POINT offset) {
    if (points.size() < 2) return;

    Gdiplus::Pen pen(color, thickness * zoom);
    pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    if (isHighlighter) {
        Gdiplus::Color hlColor(128, color.GetR(), color.GetG(), color.GetB());
        pen.SetColor(hlColor);
        pen.SetWidth(thickness * zoom * 3);
    }

    std::vector<Gdiplus::PointF> gdipPoints;
    for (const auto& pt : points) {
        gdipPoints.push_back(Gdiplus::PointF(pt.x * zoom + offset.x, pt.y * zoom + offset.y));
    }

    g.DrawLines(&pen, gdipPoints.data(), (INT)gdipPoints.size());
}

RECT PathObject::GetBounds() const {
    if (points.empty()) return {};

    RECT r = { points[0].x, points[0].y, points[0].x, points[0].y };
    for (const auto& pt : points) {
        r.left = (std::min)(r.left, (LONG)pt.x);
        r.top = (std::min)(r.top, (LONG)pt.y);
        r.right = (std::max)(r.right, (LONG)pt.x);
        r.bottom = (std::max)(r.bottom, (LONG)pt.y);
    }
    int pad = (int)(thickness / 2) + 1;
    r.left -= pad; r.top -= pad; r.right += pad; r.bottom += pad;
    return r;
}

void TextObject::Draw(Gdiplus::Graphics& g, float zoom, POINT offset) {
    Gdiplus::FontFamily family(fontName.c_str());
    Gdiplus::Font font(&family, fontSize * zoom, bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular);
    Gdiplus::SolidBrush brush(color);

    float x = position.x * zoom + offset.x;
    float y = position.y * zoom + offset.y;

    g.DrawString(text.c_str(), -1, &font, Gdiplus::PointF(x, y), &brush);
}

RECT TextObject::GetBounds() const {
    // Approximate bounds
    int w = (int)(text.length() * fontSize * 0.6f);
    int h = (int)(fontSize * 1.2f);
    return { position.x, position.y, position.x + w, position.y + h };
}

void BlurRegion::Draw(Gdiplus::Graphics& g, float zoom, POINT offset) {
    // The blur is applied directly to the image, so just draw a subtle indicator
    float x = bounds.left * zoom + offset.x;
    float y = bounds.top * zoom + offset.y;
    float w = (bounds.right - bounds.left) * zoom;
    float h = (bounds.bottom - bounds.top) * zoom;

    Gdiplus::Pen pen(Gdiplus::Color(100, 255, 255, 255), 1);
    pen.SetDashStyle(Gdiplus::DashStyleDash);
    g.DrawRectangle(&pen, x, y, w, h);
}

void DrawEditorToolbar(HDC hdc, EditorState* state) {
    RECT& toolbar = state->toolbarRect;

    // Background
    HBRUSH bgBrush = CreateSolidBrush(Colors::Background);
    FillRect(hdc, &toolbar, bgBrush);
    DeleteObject(bgBrush);

    // Divider line
    HPEN divPen = CreatePen(PS_SOLID, 1, Colors::Divider);
    SelectObject(hdc, divPen);
    MoveToEx(hdc, toolbar.left, toolbar.bottom - 1, nullptr);
    LineTo(hdc, toolbar.right, toolbar.bottom - 1);
    DeleteObject(divPen);

    int x = 12;
    int y = (EDITOR_TOOLBAR_HEIGHT - EDITOR_TOOL_SIZE) / 2;

    // Draw tool buttons
    for (int i = 0; i <= (int)EditorTool::Crop; i++) {
        RECT btnRect = { x, y, x + EDITOR_TOOL_SIZE, y + EDITOR_TOOL_SIZE };

        bool isActive = (int)state->currentTool == i;
        bool isHovered = state->hoveredTool == i;

        COLORREF bgColor = isActive ? Colors::AccentDark : (isHovered ? Colors::SurfaceHover : Colors::Surface);
        DrawRoundedRect(hdc, btnRect, 6, bgColor);

        // Draw icon
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, isActive ? Colors::Text : Colors::TextSecondary);
        HFONT oldFont = (HFONT)SelectObject(hdc, g_app.fontIcon);
        DrawTextW(hdc, TOOL_ICONS[i], -1, &btnRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);

        x += EDITOR_TOOL_SIZE + 4;
    }

    // Divider after tools
    x += 8;
    HPEN sepPen = CreatePen(PS_SOLID, 1, Colors::Divider);
    SelectObject(hdc, sepPen);
    MoveToEx(hdc, x, y + 4, nullptr);
    LineTo(hdc, x, y + EDITOR_TOOL_SIZE - 4);
    DeleteObject(sepPen);
    x += 12;

    // Color picker
    state->colorPickerRect = { x, y, x + (NUM_EDITOR_COLORS * 24) + 8, y + EDITOR_TOOL_SIZE };

    for (int i = 0; i < NUM_EDITOR_COLORS; i++) {
        RECT colorRect = { x + i * 24 + 4, y + 6, x + i * 24 + 24, y + EDITOR_TOOL_SIZE - 6 };

        bool isSelected = (state->currentColor.GetValue() == EDITOR_COLORS[i].GetValue());
        bool isHovered = state->hoveredColorIndex == i;

        if (isSelected || isHovered) {
            RECT borderRect = { colorRect.left - 2, colorRect.top - 2, colorRect.right + 2, colorRect.bottom + 2 };
            DrawRoundedRect(hdc, borderRect, 4, isSelected ? Colors::Accent : Colors::TextDim);
        }

        HBRUSH colorBrush = CreateSolidBrush(RGB(
            EDITOR_COLORS[i].GetR(), EDITOR_COLORS[i].GetG(), EDITOR_COLORS[i].GetB()));
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, colorBrush);
        RoundRect(hdc, colorRect.left, colorRect.top, colorRect.right, colorRect.bottom, 4, 4);
        SelectObject(hdc, oldBrush);
        DeleteObject(colorBrush);
    }

    x = state->colorPickerRect.right + 12;

    // Draw separator
    HPEN sepPen2 = CreatePen(PS_SOLID, 1, Colors::Divider);
    SelectObject(hdc, sepPen2);
    MoveToEx(hdc, x, y + 4, nullptr);
    LineTo(hdc, x, y + EDITOR_TOOL_SIZE - 4);
    DeleteObject(sepPen2);
    x += 12;

    // Undo/Redo buttons - store positions in state for click handling
    state->undoRect = { x, y, x + EDITOR_TOOL_SIZE, y + EDITOR_TOOL_SIZE };
    bool canUndo = state->undoManager && state->undoManager->CanUndo();
    DrawRoundedRect(hdc, state->undoRect, 6, canUndo ? Colors::Surface : Colors::Background);
    SetTextColor(hdc, canUndo ? Colors::Text : Colors::TextDim);
    HFONT oldFont = (HFONT)SelectObject(hdc, g_app.fontIcon);
    DrawTextW(hdc, L"\u21B6", -1, &state->undoRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
    x += EDITOR_TOOL_SIZE + 4;

    state->redoRect = { x, y, x + EDITOR_TOOL_SIZE, y + EDITOR_TOOL_SIZE };
    bool canRedo = state->undoManager && state->undoManager->CanRedo();
    DrawRoundedRect(hdc, state->redoRect, 6, canRedo ? Colors::Surface : Colors::Background);
    SetTextColor(hdc, canRedo ? Colors::Text : Colors::TextDim);
    oldFont = (HFONT)SelectObject(hdc, g_app.fontIcon);
    DrawTextW(hdc, L"\u21B7", -1, &state->redoRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
    x += EDITOR_TOOL_SIZE + 20;

    // Save button
    state->saveRect = { x, y, x + 60, y + EDITOR_TOOL_SIZE };
    DrawRoundedRect(hdc, state->saveRect, 6, Colors::AccentDark);
    SetTextColor(hdc, Colors::Text);
    oldFont = (HFONT)SelectObject(hdc, g_app.fontSmall);
    DrawTextW(hdc, L"Save", -1, &state->saveRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

void DrawEditorCanvas(HDC hdc, EditorState* state) {
    RECT& canvas = state->canvasRect;

    // Background (checkerboard for transparency)
    HBRUSH bgBrush = CreateSolidBrush(RGB(40, 40, 40));
    FillRect(hdc, &canvas, bgBrush);
    DeleteObject(bgBrush);

    if (!state->displayImage) return;

    // Create GDI+ graphics
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

    // Calculate image position (centered with pan offset)
    int imgWidth = state->displayImage->GetWidth();
    int imgHeight = state->displayImage->GetHeight();
    int canvasWidth = canvas.right - canvas.left;
    int canvasHeight = canvas.bottom - canvas.top;

    int centerX = (canvasWidth - (int)(imgWidth * state->zoom)) / 2 + state->panOffset.x;
    int centerY = (canvasHeight - (int)(imgHeight * state->zoom)) / 2 + state->panOffset.y;

    // Clip to canvas
    g.SetClip(Gdiplus::Rect(canvas.left, canvas.top, canvasWidth, canvasHeight));

    // Draw image
    g.DrawImage(state->displayImage,
        canvas.left + centerX, canvas.top + centerY,
        (int)(imgWidth * state->zoom), (int)(imgHeight * state->zoom));

    // Draw objects
    POINT drawOffset = { canvas.left + centerX, canvas.top + centerY };
    for (const auto& obj : state->objects) {
        obj->Draw(g, state->zoom, drawOffset);
    }

    // Draw active object being created
    if (state->isDrawing && state->activeObject) {
        state->activeObject->Draw(g, state->zoom, drawOffset);
    }

    // Draw crop overlay
    if (state->cropActive) {
        // Dim outside crop area
        Gdiplus::SolidBrush dimBrush(Gdiplus::Color(150, 0, 0, 0));

        int cx = (int)(state->cropRect.left * state->zoom) + canvas.left + centerX;
        int cy = (int)(state->cropRect.top * state->zoom) + canvas.top + centerY;
        int cw = (int)((state->cropRect.right - state->cropRect.left) * state->zoom);
        int ch = (int)((state->cropRect.bottom - state->cropRect.top) * state->zoom);

        // Top
        g.FillRectangle(&dimBrush, canvas.left, canvas.top, canvasWidth, cy - canvas.top);
        // Bottom
        g.FillRectangle(&dimBrush, canvas.left, cy + ch, canvasWidth, canvas.bottom - (cy + ch));
        // Left
        g.FillRectangle(&dimBrush, canvas.left, cy, cx - canvas.left, ch);
        // Right
        g.FillRectangle(&dimBrush, cx + cw, cy, canvas.right - (cx + cw), ch);

        // Crop border
        Gdiplus::Pen cropPen(Gdiplus::Color(255, 255, 255, 255), 2);
        g.DrawRectangle(&cropPen, cx, cy, cw, ch);

        // Draw all 8 crop handles
        int hs = HANDLE_SIZE;
        Gdiplus::SolidBrush handleBrush(Gdiplus::Color(255, 76, 194, 255));
        Gdiplus::Pen handlePen(Gdiplus::Color(255, 255, 255, 255), 1);

        // Corner handles
        g.FillRectangle(&handleBrush, cx - hs/2, cy - hs/2, hs, hs);                 // TopLeft
        g.FillRectangle(&handleBrush, cx + cw - hs/2, cy - hs/2, hs, hs);            // TopRight
        g.FillRectangle(&handleBrush, cx - hs/2, cy + ch - hs/2, hs, hs);            // BottomLeft
        g.FillRectangle(&handleBrush, cx + cw - hs/2, cy + ch - hs/2, hs, hs);       // BottomRight

        // Edge handles
        g.FillRectangle(&handleBrush, cx + cw/2 - hs/2, cy - hs/2, hs, hs);          // TopCenter
        g.FillRectangle(&handleBrush, cx + cw/2 - hs/2, cy + ch - hs/2, hs, hs);     // BottomCenter
        g.FillRectangle(&handleBrush, cx - hs/2, cy + ch/2 - hs/2, hs, hs);          // MiddleLeft
        g.FillRectangle(&handleBrush, cx + cw - hs/2, cy + ch/2 - hs/2, hs, hs);     // MiddleRight

        // Draw handle borders
        g.DrawRectangle(&handlePen, cx - hs/2, cy - hs/2, hs, hs);
        g.DrawRectangle(&handlePen, cx + cw - hs/2, cy - hs/2, hs, hs);
        g.DrawRectangle(&handlePen, cx - hs/2, cy + ch - hs/2, hs, hs);
        g.DrawRectangle(&handlePen, cx + cw - hs/2, cy + ch - hs/2, hs, hs);
        g.DrawRectangle(&handlePen, cx + cw/2 - hs/2, cy - hs/2, hs, hs);
        g.DrawRectangle(&handlePen, cx + cw/2 - hs/2, cy + ch - hs/2, hs, hs);
        g.DrawRectangle(&handlePen, cx - hs/2, cy + ch/2 - hs/2, hs, hs);
        g.DrawRectangle(&handlePen, cx + cw - hs/2, cy + ch/2 - hs/2, hs, hs);

        // Show "Press Enter to apply crop" hint
        Gdiplus::FontFamily family(L"Segoe UI");
        Gdiplus::Font hintFont(&family, 12);
        Gdiplus::SolidBrush hintBrush(Gdiplus::Color(255, 255, 255, 255));
        g.DrawString(L"Press Enter to apply crop, Esc to cancel", -1, &hintFont,
            Gdiplus::PointF((float)(cx + 5), (float)(cy + ch + 5)), &hintBrush);
    }

    // Draw text input cursor
    if (state->textInputActive) {
        int tx = (int)(state->textPosition.x * state->zoom) + canvas.left + centerX;
        int ty = (int)(state->textPosition.y * state->zoom) + canvas.top + centerY;

        Gdiplus::FontFamily family(L"Segoe UI");
        Gdiplus::Font font(&family, state->currentTextSize * state->zoom);
        Gdiplus::SolidBrush brush(state->currentColor);

        if (!state->textBuffer.empty()) {
            g.DrawString(state->textBuffer.c_str(), -1, &font, Gdiplus::PointF((float)tx, (float)ty), &brush);
        }

        // Cursor
        Gdiplus::Pen cursorPen(state->currentColor, 2);
        Gdiplus::RectF bounds;
        g.MeasureString(state->textBuffer.c_str(), -1, &font, Gdiplus::PointF((float)tx, (float)ty), &bounds);
        float cursorX = tx + bounds.Width;
        g.DrawLine(&cursorPen, cursorX, (float)ty, cursorX, (float)ty + 20 * state->zoom);
    }
}

void RenderObjectsToImage(EditorState* state) {
    if (!state->displayImage) return;

    Gdiplus::Graphics g(state->displayImage);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    POINT offset = { 0, 0 };
    for (const auto& obj : state->objects) {
        obj->Draw(g, 1.0f, offset);
    }
}

bool SaveEditorImage(EditorState* state, const wchar_t* filepath) {
    if (!state->displayImage) return false;

    // Render all objects to image
    RenderObjectsToImage(state);

    // Create a copy of the image in memory (to avoid file lock issues)
    int width = state->displayImage->GetWidth();
    int height = state->displayImage->GetHeight();

    Gdiplus::Bitmap* saveBitmap = new Gdiplus::Bitmap(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics g(saveBitmap);
    g.DrawImage(state->displayImage, 0, 0, width, height);

    // Get PNG encoder CLSID
    CLSID pngClsid;
    UINT numEncoders, size;
    Gdiplus::GetImageEncodersSize(&numEncoders, &size);

    Gdiplus::ImageCodecInfo* encoders = (Gdiplus::ImageCodecInfo*)malloc(size);
    Gdiplus::GetImageEncoders(numEncoders, size, encoders);

    bool foundEncoder = false;
    for (UINT i = 0; i < numEncoders; i++) {
        if (wcscmp(encoders[i].MimeType, L"image/png") == 0) {
            pngClsid = encoders[i].Clsid;
            foundEncoder = true;
            break;
        }
    }
    free(encoders);

    if (!foundEncoder) {
        delete saveBitmap;
        return false;
    }

    // Save to file
    Gdiplus::Status status = saveBitmap->Save(filepath, &pngClsid);
    delete saveBitmap;

    if (status == Gdiplus::Ok) {
        state->unsavedChanges = false;
        state->objects.clear();  // Clear objects since they're now baked in

        // Reload the image from the saved file (without locking)
        delete state->displayImage;
        state->displayImage = LoadImageWithoutLock(filepath);

        if (!state->displayImage) {
            // Fallback: create empty bitmap with same dimensions
            state->displayImage = new Gdiplus::Bitmap(width, height, PixelFormat32bppARGB);
        }

        return true;
    }

    return false;
}

LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    EditorState* state = GetEditorState(hwnd);

    switch (msg) {
    case WM_CREATE:
        return 0;

    case WM_SIZE: {
        if (!state) return 0;
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        state->toolbarRect = { 0, 0, clientRect.right, EDITOR_TOOLBAR_HEIGHT };
        state->canvasRect = { 0, EDITOR_TOOLBAR_HEIGHT, clientRect.right, clientRect.bottom };

        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_PAINT: {
        if (!state) return DefWindowProcW(hwnd, msg, wParam, lParam);

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

        DrawEditorToolbar(memDC, state);
        DrawEditorCanvas(memDC, state);

        // Draw tooltip on top of everything
        if (state->hoveredTool >= 0 && state->hoveredTool <= (int)EditorTool::Crop) {
            const wchar_t* tooltipText = TOOL_NAMES[state->hoveredTool];

            // Calculate button position for tooltip
            int btnX = 12 + state->hoveredTool * (EDITOR_TOOL_SIZE + 4);
            int btnY = (EDITOR_TOOLBAR_HEIGHT - EDITOR_TOOL_SIZE) / 2;

            // Draw highlight box around hovered button
            HPEN highlightPen = CreatePen(PS_SOLID, 2, RGB(76, 194, 255));
            HPEN oldPen = (HPEN)SelectObject(memDC, highlightPen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
            RoundRect(memDC, btnX - 2, btnY - 2, btnX + EDITOR_TOOL_SIZE + 2, btnY + EDITOR_TOOL_SIZE + 2, 8, 8);
            SelectObject(memDC, oldPen);
            SelectObject(memDC, oldBrush);
            DeleteObject(highlightPen);

            // Measure tooltip text
            HFONT oldFont = (HFONT)SelectObject(memDC, g_app.fontSmall);
            SIZE textSize;
            GetTextExtentPoint32W(memDC, tooltipText, (int)wcslen(tooltipText), &textSize);

            int padding = 8;
            int tipX = btnX;
            int tipY = btnY + EDITOR_TOOL_SIZE + 8;

            // Make sure tooltip doesn't go off screen
            if (tipX + textSize.cx + padding * 2 > width) {
                tipX = width - textSize.cx - padding * 2 - 5;
            }

            RECT tipRect = {
                tipX,
                tipY,
                tipX + textSize.cx + padding * 2,
                tipY + textSize.cy + padding
            };

            // Draw tooltip background with shadow
            RECT shadowRect = { tipRect.left + 2, tipRect.top + 2, tipRect.right + 2, tipRect.bottom + 2 };
            HBRUSH shadowBrush = CreateSolidBrush(RGB(20, 20, 20));
            FillRect(memDC, &shadowRect, shadowBrush);
            DeleteObject(shadowBrush);

            // Draw tooltip background
            HBRUSH tipBrush = CreateSolidBrush(RGB(60, 60, 65));
            FillRect(memDC, &tipRect, tipBrush);
            DeleteObject(tipBrush);

            // Draw tooltip border
            HPEN tipPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 105));
            oldPen = (HPEN)SelectObject(memDC, tipPen);
            oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
            Rectangle(memDC, tipRect.left, tipRect.top, tipRect.right, tipRect.bottom);
            SelectObject(memDC, oldPen);
            SelectObject(memDC, oldBrush);
            DeleteObject(tipPen);

            // Draw tooltip text
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, RGB(255, 255, 255));
            tipRect.left += padding;
            tipRect.top += padding / 2;
            DrawTextW(memDC, tooltipText, -1, &tipRect, DT_LEFT | DT_TOP);
            SelectObject(memDC, oldFont);
        }

        // Draw slider popup if visible (using GDI+ for smooth rendering)
        if (state->sliderVisible) {
            Gdiplus::Graphics gSlider(memDC);
            gSlider.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            gSlider.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            RECT& sr = state->sliderRect;

            // Shadow
            Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(100, 0, 0, 0));
            gSlider.FillRectangle(&shadowBrush, sr.left + 3, sr.top + 3, sr.right - sr.left, sr.bottom - sr.top);

            // Background with rounded corners
            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 45, 45, 50));
            Gdiplus::Pen borderPen(Gdiplus::Color(255, 80, 80, 85), 1);

            int radius = 8;
            Gdiplus::GraphicsPath path;
            path.AddArc(sr.left, sr.top, radius * 2, radius * 2, 180, 90);
            path.AddArc(sr.right - radius * 2, sr.top, radius * 2, radius * 2, 270, 90);
            path.AddArc(sr.right - radius * 2, sr.bottom - radius * 2, radius * 2, radius * 2, 0, 90);
            path.AddArc(sr.left, sr.bottom - radius * 2, radius * 2, radius * 2, 90, 90);
            path.CloseFigure();

            gSlider.FillPath(&bgBrush, &path);
            gSlider.DrawPath(&borderPen, &path);

            // Label
            const wchar_t* label = L"Size";
            if (state->sliderToolIndex == (int)EditorTool::Blur) label = L"Blur";
            else if (state->sliderToolIndex == (int)EditorTool::Text) label = L"Font Size";

            Gdiplus::FontFamily family(L"Segoe UI");
            Gdiplus::Font labelFont(&family, 11);
            Gdiplus::SolidBrush labelBrush(Gdiplus::Color(255, 180, 180, 180));
            gSlider.DrawString(label, -1, &labelFont, Gdiplus::PointF((float)sr.left + 12, (float)sr.top + 8), &labelBrush);

            // Value
            wchar_t valueStr[32];
            swprintf_s(valueStr, L"%.0f", state->sliderValue);
            Gdiplus::SolidBrush valueBrush(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::RectF valueRect((float)sr.left + 12, (float)sr.top + 8, (float)(sr.right - sr.left - 24), 20);
            Gdiplus::StringFormat rightAlign;
            rightAlign.SetAlignment(Gdiplus::StringAlignmentFar);
            gSlider.DrawString(valueStr, -1, &labelFont, valueRect, &rightAlign, &valueBrush);

            // Slider track
            int trackY = sr.top + 38;
            int trackLeft = sr.left + 14;
            int trackRight = sr.right - 14;
            int trackWidth = trackRight - trackLeft;
            int trackHeight = 4;

            Gdiplus::SolidBrush trackBrush(Gdiplus::Color(255, 35, 35, 40));
            gSlider.FillRectangle(&trackBrush, trackLeft, trackY, trackWidth, trackHeight);

            // Slider fill
            float percent = (state->sliderValue - state->sliderMin) / (state->sliderMax - state->sliderMin);
            percent = (std::max)(0.0f, (std::min)(1.0f, percent));
            int fillWidth = (int)(trackWidth * percent);
            Gdiplus::SolidBrush fillBrush(Gdiplus::Color(255, 76, 194, 255));
            gSlider.FillRectangle(&fillBrush, trackLeft, trackY, fillWidth, trackHeight);

            // Slider thumb (circle)
            int thumbX = trackLeft + fillWidth;
            int thumbRadius = 7;
            Gdiplus::SolidBrush thumbBrush(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::Pen thumbPen(Gdiplus::Color(255, 76, 194, 255), 2);
            gSlider.FillEllipse(&thumbBrush, thumbX - thumbRadius, trackY + trackHeight/2 - thumbRadius, thumbRadius * 2, thumbRadius * 2);
            gSlider.DrawEllipse(&thumbPen, thumbX - thumbRadius, trackY + trackHeight/2 - thumbRadius, thumbRadius * 2, thumbRadius * 2);
        }

        BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!state) return 0;

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        // Check tool hover
        int newHoveredTool = -1;
        int toolX = 12;
        int toolY = (EDITOR_TOOLBAR_HEIGHT - EDITOR_TOOL_SIZE) / 2;

        for (int i = 0; i <= (int)EditorTool::Crop; i++) {
            RECT btnRect = { toolX, toolY, toolX + EDITOR_TOOL_SIZE, toolY + EDITOR_TOOL_SIZE };
            POINT pt = { x, y };
            if (PtInRect(&btnRect, pt)) {
                newHoveredTool = i;
                break;
            }
            toolX += EDITOR_TOOL_SIZE + 4;
        }

        if (newHoveredTool != state->hoveredTool) {
            state->hoveredTool = newHoveredTool;
            InvalidateRect(hwnd, &state->toolbarRect, FALSE);
        }

        // Check color hover
        int newHoveredColor = -1;
        if (y >= state->colorPickerRect.top && y <= state->colorPickerRect.bottom) {
            int colorX = state->colorPickerRect.left;
            for (int i = 0; i < NUM_EDITOR_COLORS; i++) {
                RECT colorRect = { colorX + i * 24 + 4, state->colorPickerRect.top,
                                   colorX + i * 24 + 24, state->colorPickerRect.bottom };
                POINT pt = { x, y };
                if (PtInRect(&colorRect, pt)) {
                    newHoveredColor = i;
                    break;
                }
            }
        }

        if (newHoveredColor != state->hoveredColorIndex) {
            state->hoveredColorIndex = newHoveredColor;
            InvalidateRect(hwnd, &state->toolbarRect, FALSE);
        }

        // Handle slider dragging
        if (state->sliderDragging && state->sliderVisible) {
            RECT& sr = state->sliderRect;
            int trackLeft = sr.left + 14;
            int trackRight = sr.right - 14;
            int trackWidth = trackRight - trackLeft;

            float percent = (float)(x - trackLeft) / (float)trackWidth;
            percent = (std::max)(0.0f, (std::min)(1.0f, percent));
            state->sliderValue = state->sliderMin + percent * (state->sliderMax - state->sliderMin);

            // Apply value to appropriate setting
            switch (state->sliderToolIndex) {
                case (int)EditorTool::Arrow:
                case (int)EditorTool::Rectangle:
                case (int)EditorTool::Ellipse:
                case (int)EditorTool::Pen:
                case (int)EditorTool::Highlighter:
                    state->currentThickness = state->sliderValue;
                    break;
                case (int)EditorTool::Blur:
                    state->currentBlurSize = (int)state->sliderValue;
                    break;
                case (int)EditorTool::Text:
                    state->currentTextSize = state->sliderValue;
                    break;
            }

            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Handle drawing
        if (state->isDrawing && y > EDITOR_TOOLBAR_HEIGHT) {
            POINT canvasPt = ScreenToCanvas(state, { x, y });
            state->drawEnd = canvasPt;

            if (state->currentTool == EditorTool::Pen || state->currentTool == EditorTool::Highlighter) {
                if (state->activeObject) {
                    PathObject* path = dynamic_cast<PathObject*>(state->activeObject.get());
                    if (path) {
                        path->points.push_back(canvasPt);
                    }
                }
            } else if (state->activeObject) {
                // Update shape bounds
                if (auto* arrow = dynamic_cast<ArrowObject*>(state->activeObject.get())) {
                    arrow->end = canvasPt;
                } else if (auto* shape = dynamic_cast<ShapeObject*>(state->activeObject.get())) {
                    shape->bounds.right = canvasPt.x;
                    shape->bounds.bottom = canvasPt.y;
                } else if (auto* blur = dynamic_cast<BlurRegion*>(state->activeObject.get())) {
                    blur->bounds.right = canvasPt.x;
                    blur->bounds.bottom = canvasPt.y;
                }
            }

            // Handle crop resize/move
            if (state->cropActive && state->cropHandle >= -1) {
                int dx = canvasPt.x - state->drawStart.x;
                int dy = canvasPt.y - state->drawStart.y;
                state->drawStart = canvasPt;

                int imgWidth = state->displayImage ? state->displayImage->GetWidth() : 1;
                int imgHeight = state->displayImage ? state->displayImage->GetHeight() : 1;

                RECT& crop = state->cropRect;

                switch (state->cropHandle) {
                    case -1:  // Move entire crop area
                        crop.left += dx;
                        crop.right += dx;
                        crop.top += dy;
                        crop.bottom += dy;
                        break;
                    case 0:  // TopLeft
                        crop.left += dx;
                        crop.top += dy;
                        break;
                    case 1:  // TopCenter
                        crop.top += dy;
                        break;
                    case 2:  // TopRight
                        crop.right += dx;
                        crop.top += dy;
                        break;
                    case 3:  // MiddleLeft
                        crop.left += dx;
                        break;
                    case 4:  // MiddleRight
                        crop.right += dx;
                        break;
                    case 5:  // BottomLeft
                        crop.left += dx;
                        crop.bottom += dy;
                        break;
                    case 6:  // BottomCenter
                        crop.bottom += dy;
                        break;
                    case 7:  // BottomRight
                        crop.right += dx;
                        crop.bottom += dy;
                        break;
                }

                // Ensure minimum size
                if (crop.right - crop.left < 10) crop.right = crop.left + 10;
                if (crop.bottom - crop.top < 10) crop.bottom = crop.top + 10;

                // Clamp to image bounds
                if (crop.left < 0) { crop.right -= crop.left; crop.left = 0; }
                if (crop.top < 0) { crop.bottom -= crop.top; crop.top = 0; }
                if (crop.right > imgWidth) { crop.left -= (crop.right - imgWidth); crop.right = imgWidth; }
                if (crop.bottom > imgHeight) { crop.top -= (crop.bottom - imgHeight); crop.bottom = imgHeight; }
                crop.left = (std::max)(0L, crop.left);
                crop.top = (std::max)(0L, crop.top);
                crop.right = (std::min)((LONG)imgWidth, crop.right);
                crop.bottom = (std::min)((LONG)imgHeight, crop.bottom);
            }

            InvalidateRect(hwnd, &state->canvasRect, FALSE);
        }

        // Update cursor for crop handles when not drawing
        if (state->cropActive && !state->isDrawing && y > EDITOR_TOOLBAR_HEIGHT) {
            int handle = GetCropHandleAtPoint(state, x, y);
            SetCursor(GetCropCursor(handle));
        }

        // Handle panning
        if (state->isPanning) {
            state->panOffset.x += x - state->panStart.x;
            state->panOffset.y += y - state->panStart.y;
            state->panStart = { x, y };
            InvalidateRect(hwnd, &state->canvasRect, FALSE);
        }

        return 0;
    }

    case WM_LBUTTONDOWN: {
        if (!state) return 0;

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        // Check if clicking on slider popup FIRST (before toolbar/canvas checks)
        // Slider popup extends below toolbar, so we check it independently
        if (state->sliderVisible) {
            if (PtInRect(&state->sliderRect, pt)) {
                // Start dragging slider
                RECT& sr = state->sliderRect;
                int trackLeft = sr.left + 14;
                int trackRight = sr.right - 14;
                int trackWidth = trackRight - trackLeft;
                int trackY = sr.top + 38;

                // Check if clicking near the track area
                if (y >= trackY - 12 && y <= trackY + 18) {
                    state->sliderDragging = true;
                    SetCapture(hwnd);

                    float percent = (float)(x - trackLeft) / (float)trackWidth;
                    percent = (std::max)(0.0f, (std::min)(1.0f, percent));
                    state->sliderValue = state->sliderMin + percent * (state->sliderMax - state->sliderMin);

                    // Apply value
                    switch (state->sliderToolIndex) {
                        case (int)EditorTool::Arrow:
                        case (int)EditorTool::Rectangle:
                        case (int)EditorTool::Ellipse:
                        case (int)EditorTool::Pen:
                        case (int)EditorTool::Highlighter:
                            state->currentThickness = state->sliderValue;
                            break;
                        case (int)EditorTool::Blur:
                            state->currentBlurSize = (int)state->sliderValue;
                            break;
                        case (int)EditorTool::Text:
                            state->currentTextSize = state->sliderValue;
                            break;
                    }

                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            // Clicked outside slider - close it
            state->sliderVisible = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        // Tool selection
        if (y < EDITOR_TOOLBAR_HEIGHT) {
            int toolX = 12;
            int toolY = (EDITOR_TOOLBAR_HEIGHT - EDITOR_TOOL_SIZE) / 2;

            for (int i = 0; i <= (int)EditorTool::Crop; i++) {
                RECT btnRect = { toolX, toolY, toolX + EDITOR_TOOL_SIZE, toolY + EDITOR_TOOL_SIZE };
                if (PtInRect(&btnRect, pt)) {
                    // Finalize any active text input before switching tools
                    if (state->textInputActive && !state->textBuffer.empty()) {
                        auto textObj = std::make_unique<TextObject>();
                        textObj->position = state->textPosition;
                        textObj->text = state->textBuffer;
                        textObj->color = state->currentColor;
                        textObj->fontSize = state->currentTextSize;
                        state->undoManager->Execute(std::make_unique<AddObjectCommand>(state, std::move(textObj)));
                        state->unsavedChanges = true;
                    }
                    state->textInputActive = false;
                    state->textBuffer.clear();

                    state->currentTool = (EditorTool)i;

                    // Special handling for crop
                    if (state->currentTool == EditorTool::Crop && state->displayImage) {
                        state->cropActive = true;
                        state->cropRect = { 0, 0,
                            (LONG)state->displayImage->GetWidth(),
                            (LONG)state->displayImage->GetHeight() };
                    } else {
                        state->cropActive = false;
                    }

                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                toolX += EDITOR_TOOL_SIZE + 4;
            }

            // Color selection
            if (y >= state->colorPickerRect.top && y <= state->colorPickerRect.bottom) {
                int colorX = state->colorPickerRect.left;
                for (int i = 0; i < NUM_EDITOR_COLORS; i++) {
                    RECT colorRect = { colorX + i * 24 + 4, state->colorPickerRect.top,
                                       colorX + i * 24 + 24, state->colorPickerRect.bottom };
                    POINT pt = { x, y };
                    if (PtInRect(&colorRect, pt)) {
                        state->currentColor = EDITOR_COLORS[i];
                        InvalidateRect(hwnd, &state->toolbarRect, FALSE);
                        return 0;
                    }
                }
            }

            // Check undo/redo/save buttons using stored positions

            // Undo button
            if (PtInRect(&state->undoRect, pt)) {
                if (state->undoManager && state->undoManager->CanUndo()) {
                    state->undoManager->Undo();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }

            // Redo button
            if (PtInRect(&state->redoRect, pt)) {
                if (state->undoManager && state->undoManager->CanRedo()) {
                    state->undoManager->Redo();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }

            // Save button
            if (PtInRect(&state->saveRect, pt)) {
                if (SaveEditorImage(state, state->filepath.c_str())) {
                    MessageBoxW(hwnd, L"Image saved successfully!", L"Saved", MB_ICONINFORMATION);
                } else {
                    MessageBoxW(hwnd, L"Failed to save image", L"Error", MB_ICONERROR);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            return 0;
        }

        // Check crop handle interaction first
        if (state->cropActive) {
            int handle = GetCropHandleAtPoint(state, x, y);
            if (handle >= -1) {  // -1 = inside (move), 0-7 = handles
                SetCapture(hwnd);
                state->cropHandle = handle;
                state->drawStart = ScreenToCanvas(state, { x, y });
                state->isDrawing = true;
                SetCursor(GetCropCursor(handle));
                return 0;
            }
        }

        // Canvas interaction
        SetCapture(hwnd);
        POINT canvasPt = ScreenToCanvas(state, { x, y });
        state->drawStart = canvasPt;
        state->drawEnd = canvasPt;
        state->isDrawing = true;

        // Handle text tool
        if (state->currentTool == EditorTool::Text) {
            if (state->textInputActive && !state->textBuffer.empty()) {
                // Commit current text
                auto textObj = std::make_unique<TextObject>();
                textObj->position = state->textPosition;
                textObj->text = state->textBuffer;
                textObj->color = state->currentColor;
                textObj->fontSize = state->currentTextSize;

                state->undoManager->Execute(std::make_unique<AddObjectCommand>(state, std::move(textObj)));
                state->unsavedChanges = true;
                state->textBuffer.clear();
            }
            state->textInputActive = true;
            state->textPosition = canvasPt;
            state->isDrawing = false;
            InvalidateRect(hwnd, &state->canvasRect, FALSE);
            return 0;
        }

        // Create active object based on tool
        switch (state->currentTool) {
        case EditorTool::Arrow: {
            auto arrow = std::make_unique<ArrowObject>();
            arrow->start = canvasPt;
            arrow->end = canvasPt;
            arrow->color = state->currentColor;
            arrow->thickness = state->currentThickness;
            state->activeObject = std::move(arrow);
            break;
        }
        case EditorTool::Rectangle: {
            auto shape = std::make_unique<ShapeObject>();
            shape->bounds = { canvasPt.x, canvasPt.y, canvasPt.x, canvasPt.y };
            shape->strokeColor = state->currentColor;
            shape->thickness = state->currentThickness;
            shape->isEllipse = false;
            state->activeObject = std::move(shape);
            break;
        }
        case EditorTool::Ellipse: {
            auto shape = std::make_unique<ShapeObject>();
            shape->bounds = { canvasPt.x, canvasPt.y, canvasPt.x, canvasPt.y };
            shape->strokeColor = state->currentColor;
            shape->thickness = state->currentThickness;
            shape->isEllipse = true;
            state->activeObject = std::move(shape);
            break;
        }
        case EditorTool::Pen:
        case EditorTool::Highlighter: {
            auto path = std::make_unique<PathObject>();
            path->points.push_back(canvasPt);
            path->color = state->currentColor;
            path->thickness = state->currentThickness;
            path->isHighlighter = (state->currentTool == EditorTool::Highlighter);
            state->activeObject = std::move(path);
            break;
        }
        case EditorTool::Blur: {
            auto blur = std::make_unique<BlurRegion>();
            blur->bounds = { canvasPt.x, canvasPt.y, canvasPt.x, canvasPt.y };
            blur->blockSize = state->currentBlurSize;
            state->activeObject = std::move(blur);
            break;
        }
        default:
            break;
        }

        return 0;
    }

    case WM_LBUTTONUP: {
        if (!state) return 0;
        ReleaseCapture();

        // Stop slider dragging
        if (state->sliderDragging) {
            state->sliderDragging = false;
            return 0;
        }

        if (state->isDrawing && state->activeObject) {
            // Normalize shape bounds
            if (auto* shape = dynamic_cast<ShapeObject*>(state->activeObject.get())) {
                if (shape->bounds.left > shape->bounds.right)
                    std::swap(shape->bounds.left, shape->bounds.right);
                if (shape->bounds.top > shape->bounds.bottom)
                    std::swap(shape->bounds.top, shape->bounds.bottom);
            }
            if (auto* blur = dynamic_cast<BlurRegion*>(state->activeObject.get())) {
                if (blur->bounds.left > blur->bounds.right)
                    std::swap(blur->bounds.left, blur->bounds.right);
                if (blur->bounds.top > blur->bounds.bottom)
                    std::swap(blur->bounds.top, blur->bounds.bottom);

                // Apply blur directly to image
                ApplyBlurToRegion(state->displayImage, blur->bounds, blur->blockSize);
                state->activeObject.reset();
                state->unsavedChanges = true;
            } else {
                // Add object to list
                state->undoManager->Execute(std::make_unique<AddObjectCommand>(state, std::move(state->activeObject)));
                state->unsavedChanges = true;
            }
        }

        state->isDrawing = false;
        state->isPanning = false;
        state->cropHandle = -2;  // Reset crop handle
        InvalidateRect(hwnd, &state->canvasRect, FALSE);
        return 0;
    }

    case WM_RBUTTONDOWN: {
        if (!state) return 0;

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        // Check if right-clicking on a tool button to show slider
        if (y < EDITOR_TOOLBAR_HEIGHT) {
            int toolX = 12;
            int toolY = (EDITOR_TOOLBAR_HEIGHT - EDITOR_TOOL_SIZE) / 2;

            for (int i = 0; i <= (int)EditorTool::Crop; i++) {
                RECT btnRect = { toolX, toolY, toolX + EDITOR_TOOL_SIZE, toolY + EDITOR_TOOL_SIZE };
                POINT pt = { x, y };
                if (PtInRect(&btnRect, pt)) {
                    // Check if this tool has adjustable settings
                    bool hasSettings = false;
                    float minVal = 1, maxVal = 20, currentVal = 3;

                    switch ((EditorTool)i) {
                        case EditorTool::Arrow:
                        case EditorTool::Rectangle:
                        case EditorTool::Ellipse:
                        case EditorTool::Pen:
                        case EditorTool::Highlighter:
                            hasSettings = true;
                            minVal = 1; maxVal = 20;
                            currentVal = state->currentThickness;
                            break;
                        case EditorTool::Blur:
                            hasSettings = true;
                            minVal = 4; maxVal = 32;
                            currentVal = (float)state->currentBlurSize;
                            break;
                        case EditorTool::Text:
                            hasSettings = true;
                            minVal = 8; maxVal = 72;
                            currentVal = state->currentTextSize;
                            break;
                        default:
                            break;
                    }

                    if (hasSettings) {
                        // Toggle slider - close if already showing for this tool
                        if (state->sliderVisible && state->sliderToolIndex == i) {
                            state->sliderVisible = false;
                        } else {
                            // Show slider popup below the button
                            state->sliderVisible = true;
                            state->sliderToolIndex = i;
                            state->sliderMin = minVal;
                            state->sliderMax = maxVal;
                            state->sliderValue = currentVal;
                            state->sliderRect = {
                                toolX - 20,
                                toolY + EDITOR_TOOL_SIZE + 5,
                                toolX + 160,
                                toolY + EDITOR_TOOL_SIZE + 60
                            };
                        }

                        // Also select this tool
                        state->currentTool = (EditorTool)i;
                        if (state->currentTool == EditorTool::Crop && state->displayImage) {
                            state->cropActive = true;
                            state->cropRect = { 0, 0,
                                (LONG)state->displayImage->GetWidth(),
                                (LONG)state->displayImage->GetHeight() };
                        } else {
                            state->cropActive = false;
                        }

                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                }
                toolX += EDITOR_TOOL_SIZE + 4;
            }
        }

        // Start panning (if not on toolbar)
        if (y >= EDITOR_TOOLBAR_HEIGHT) {
            state->isPanning = true;
            state->panStart = { x, y };
            SetCapture(hwnd);
        }
        return 0;
    }

    case WM_RBUTTONUP:
        if (state) state->isPanning = false;
        ReleaseCapture();
        return 0;

    case WM_MOUSEWHEEL: {
        if (!state) return 0;

        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        float zoomFactor = delta > 0 ? 1.1f : 0.9f;

        state->zoom *= zoomFactor;
        state->zoom = (std::max)(0.1f, (std::min)(5.0f, state->zoom));

        InvalidateRect(hwnd, &state->canvasRect, FALSE);
        return 0;
    }

    case WM_CHAR: {
        if (!state || !state->textInputActive) return 0;

        wchar_t ch = (wchar_t)wParam;
        if (ch == VK_BACK) {
            if (!state->textBuffer.empty()) {
                state->textBuffer.pop_back();
            }
        } else if (ch == VK_RETURN) {
            // Commit text
            if (!state->textBuffer.empty()) {
                auto textObj = std::make_unique<TextObject>();
                textObj->position = state->textPosition;
                textObj->text = state->textBuffer;
                textObj->color = state->currentColor;
                textObj->fontSize = state->currentTextSize;

                state->undoManager->Execute(std::make_unique<AddObjectCommand>(state, std::move(textObj)));
                state->unsavedChanges = true;
            }
            state->textInputActive = false;
            state->textBuffer.clear();
        } else if (ch == VK_ESCAPE) {
            state->textInputActive = false;
            state->textBuffer.clear();
        } else if (ch >= 32) {
            state->textBuffer += ch;
        }

        InvalidateRect(hwnd, &state->canvasRect, FALSE);
        return 0;
    }

    case WM_KEYDOWN: {
        if (!state) return 0;

        if (wParam == VK_ESCAPE) {
            if (state->textInputActive) {
                state->textInputActive = false;
                state->textBuffer.clear();
                InvalidateRect(hwnd, &state->canvasRect, FALSE);
            } else if (state->cropActive) {
                state->cropActive = false;
                InvalidateRect(hwnd, &state->canvasRect, FALSE);
            } else {
                CloseEditor(hwnd);
            }
            return 0;
        }

        // Ctrl+Z - Undo
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'Z') {
            if (state->undoManager && state->undoManager->CanUndo()) {
                state->undoManager->Undo();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        // Ctrl+Y - Redo
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'Y') {
            if (state->undoManager && state->undoManager->CanRedo()) {
                state->undoManager->Redo();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        // Ctrl+S - Save
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'S') {
            SaveEditorImage(state, state->filepath.c_str());
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Enter - Apply crop
        if (wParam == VK_RETURN && state->cropActive) {
            state->undoManager->Execute(std::make_unique<CropCommand>(state, state->cropRect));
            state->cropActive = false;
            state->unsavedChanges = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Number keys for tools (only when not typing text)
        if (!state->textInputActive && wParam >= '1' && wParam <= '9') {
            int toolIndex = wParam - '1';
            if (toolIndex <= (int)EditorTool::Crop) {
                state->currentTool = (EditorTool)toolIndex;
                state->cropActive = (state->currentTool == EditorTool::Crop);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CLOSE:
        CloseEditor(hwnd);
        return 0;

    case WM_DESTROY:
        // Clean up state if not already done
        g_editorStates.erase(hwnd);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
