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

// Helper to apply line style to pen
static void ApplyLineStyle(Gdiplus::Pen& pen, LineStyle style) {
    switch (style) {
        case LineStyle::Dashed:
            pen.SetDashStyle(Gdiplus::DashStyleDash);
            break;
        case LineStyle::Dotted:
            pen.SetDashStyle(Gdiplus::DashStyleDot);
            break;
        case LineStyle::Solid:
        default:
            pen.SetDashStyle(Gdiplus::DashStyleSolid);
            break;
    }
}

// Helper to draw an arrowhead
static void DrawArrowHead(Gdiplus::Graphics& g, float x, float y, float angle, float headLen,
                          Gdiplus::Color color, ArrowHeadStyle style) {
    if (style == ArrowHeadStyle::None) return;

    Gdiplus::PointF arrowPts[4];
    Gdiplus::SolidBrush brush(color);
    Gdiplus::Pen pen(color, 2);

    switch (style) {
        case ArrowHeadStyle::Filled:
            arrowPts[0] = Gdiplus::PointF(x, y);
            arrowPts[1] = Gdiplus::PointF(x - headLen * cosf(angle - 0.4f), y - headLen * sinf(angle - 0.4f));
            arrowPts[2] = Gdiplus::PointF(x - headLen * cosf(angle + 0.4f), y - headLen * sinf(angle + 0.4f));
            g.FillPolygon(&brush, arrowPts, 3);
            break;
        case ArrowHeadStyle::Open:
            g.DrawLine(&pen, x, y, x - headLen * cosf(angle - 0.4f), y - headLen * sinf(angle - 0.4f));
            g.DrawLine(&pen, x, y, x - headLen * cosf(angle + 0.4f), y - headLen * sinf(angle + 0.4f));
            break;
        case ArrowHeadStyle::Diamond:
            arrowPts[0] = Gdiplus::PointF(x, y);
            arrowPts[1] = Gdiplus::PointF(x - headLen * 0.5f * cosf(angle - 0.8f), y - headLen * 0.5f * sinf(angle - 0.8f));
            arrowPts[2] = Gdiplus::PointF(x - headLen * cosf(angle), y - headLen * sinf(angle));
            arrowPts[3] = Gdiplus::PointF(x - headLen * 0.5f * cosf(angle + 0.8f), y - headLen * 0.5f * sinf(angle + 0.8f));
            g.FillPolygon(&brush, arrowPts, 4);
            break;
        default:
            break;
    }
}

void ArrowObject::Draw(Gdiplus::Graphics& g, float zoom, POINT offset) {
    Gdiplus::Pen pen(color, thickness * zoom);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    ApplyLineStyle(pen, lineStyle);

    float x1 = start.x * zoom + offset.x;
    float y1 = start.y * zoom + offset.y;
    float x2 = end.x * zoom + offset.x;
    float y2 = end.y * zoom + offset.y;

    // Draw line
    g.DrawLine(&pen, x1, y1, x2, y2);

    // Draw arrowhead at end
    float angle = atan2f(y2 - y1, x2 - x1);
    float headLen = headSize * zoom;
    DrawArrowHead(g, x2, y2, angle, headLen, color, headStyle);

    // Draw arrowhead at start if double-ended
    if (doubleEnded) {
        float startAngle = atan2f(y1 - y2, x1 - x2);
        DrawArrowHead(g, x1, y1, startAngle, headLen, color, headStyle);
    }
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
    ApplyLineStyle(pen, lineStyle);
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
    int fontStyle = Gdiplus::FontStyleRegular;
    if (bold) fontStyle |= Gdiplus::FontStyleBold;
    if (italic) fontStyle |= Gdiplus::FontStyleItalic;
    Gdiplus::Font font(&family, fontSize * zoom, fontStyle);
    Gdiplus::SolidBrush brush(color);

    float x = bounds.left * zoom + offset.x;
    float y = bounds.top * zoom + offset.y;
    float w = (bounds.right - bounds.left) * zoom;
    float h = (bounds.bottom - bounds.top) * zoom;

    // Draw background if enabled
    if (hasBackground) {
        Gdiplus::SolidBrush bgBrush(backgroundColor);
        g.FillRectangle(&bgBrush, x, y, w, h);
    }

    // Draw text with word wrapping within the bounds
    Gdiplus::RectF layoutRect(x, y, w, h);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentNear);
    format.SetTrimming(Gdiplus::StringTrimmingNone);

    g.DrawString(text.c_str(), -1, &font, layoutRect, &format, &brush);
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

// Default hit test implementation using bounding box
bool EditorObject::HitTest(POINT pt) const {
    RECT bounds = GetBounds();
    return pt.x >= bounds.left && pt.x <= bounds.right &&
           pt.y >= bounds.top && pt.y <= bounds.bottom;
}

// Arrow hit test - check distance to line segment
bool ArrowObject::HitTest(POINT pt) const {
    float tolerance = thickness + 5;

    // Calculate distance from point to line segment
    float dx = (float)(end.x - start.x);
    float dy = (float)(end.y - start.y);
    float len2 = dx * dx + dy * dy;

    if (len2 == 0) {
        // Start and end are same point
        float dist = sqrtf((float)((pt.x - start.x) * (pt.x - start.x) + (pt.y - start.y) * (pt.y - start.y)));
        return dist <= tolerance;
    }

    float t = (std::max)(0.0f, (std::min)(1.0f, ((pt.x - start.x) * dx + (pt.y - start.y) * dy) / len2));
    float projX = start.x + t * dx;
    float projY = start.y + t * dy;
    float dist = sqrtf((pt.x - projX) * (pt.x - projX) + (pt.y - projY) * (pt.y - projY));

    return dist <= tolerance;
}

void ArrowObject::Move(int dx, int dy) {
    start.x += dx;
    start.y += dy;
    end.x += dx;
    end.y += dy;
}

void ArrowObject::SetBounds(const RECT& newBounds) {
    RECT oldBounds = GetBounds();
    float scaleX = (float)(newBounds.right - newBounds.left) / (float)(oldBounds.right - oldBounds.left);
    float scaleY = (float)(newBounds.bottom - newBounds.top) / (float)(oldBounds.bottom - oldBounds.top);

    start.x = newBounds.left + (LONG)((start.x - oldBounds.left) * scaleX);
    start.y = newBounds.top + (LONG)((start.y - oldBounds.top) * scaleY);
    end.x = newBounds.left + (LONG)((end.x - oldBounds.left) * scaleX);
    end.y = newBounds.top + (LONG)((end.y - oldBounds.top) * scaleY);
}

// Line drawing
void LineObject::Draw(Gdiplus::Graphics& g, float zoom, POINT offset) {
    Gdiplus::Pen pen(color, thickness * zoom);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    ApplyLineStyle(pen, lineStyle);

    float x1 = start.x * zoom + offset.x;
    float y1 = start.y * zoom + offset.y;
    float x2 = end.x * zoom + offset.x;
    float y2 = end.y * zoom + offset.y;

    g.DrawLine(&pen, x1, y1, x2, y2);
}

RECT LineObject::GetBounds() const {
    RECT r;
    int pad = (int)(thickness / 2) + 2;
    r.left = (std::min)(start.x, end.x) - pad;
    r.top = (std::min)(start.y, end.y) - pad;
    r.right = (std::max)(start.x, end.x) + pad;
    r.bottom = (std::max)(start.y, end.y) + pad;
    return r;
}

bool LineObject::HitTest(POINT pt) const {
    float tolerance = thickness + 5;
    float dx = (float)(end.x - start.x);
    float dy = (float)(end.y - start.y);
    float len2 = dx * dx + dy * dy;

    if (len2 == 0) {
        float dist = sqrtf((float)((pt.x - start.x) * (pt.x - start.x) + (pt.y - start.y) * (pt.y - start.y)));
        return dist <= tolerance;
    }

    float t = (std::max)(0.0f, (std::min)(1.0f, ((pt.x - start.x) * dx + (pt.y - start.y) * dy) / len2));
    float projX = start.x + t * dx;
    float projY = start.y + t * dy;
    float dist = sqrtf((pt.x - projX) * (pt.x - projX) + (pt.y - projY) * (pt.y - projY));

    return dist <= tolerance;
}

void LineObject::Move(int dx, int dy) {
    start.x += dx;
    start.y += dy;
    end.x += dx;
    end.y += dy;
}

void LineObject::SetBounds(const RECT& newBounds) {
    RECT oldBounds = GetBounds();
    if (oldBounds.right == oldBounds.left) oldBounds.right = oldBounds.left + 1;
    if (oldBounds.bottom == oldBounds.top) oldBounds.bottom = oldBounds.top + 1;

    float scaleX = (float)(newBounds.right - newBounds.left) / (float)(oldBounds.right - oldBounds.left);
    float scaleY = (float)(newBounds.bottom - newBounds.top) / (float)(oldBounds.bottom - oldBounds.top);

    start.x = newBounds.left + (LONG)((start.x - oldBounds.left) * scaleX);
    start.y = newBounds.top + (LONG)((start.y - oldBounds.top) * scaleY);
    end.x = newBounds.left + (LONG)((end.x - oldBounds.left) * scaleX);
    end.y = newBounds.top + (LONG)((end.y - oldBounds.top) * scaleY);
}

// Shape hit test
bool ShapeObject::HitTest(POINT pt) const {
    float tolerance = thickness + 3;

    if (isEllipse) {
        // Check if point is near ellipse border
        float cx = (bounds.left + bounds.right) / 2.0f;
        float cy = (bounds.top + bounds.bottom) / 2.0f;
        float rx = (bounds.right - bounds.left) / 2.0f;
        float ry = (bounds.bottom - bounds.top) / 2.0f;

        if (rx == 0 || ry == 0) return false;

        float dx = (pt.x - cx) / rx;
        float dy = (pt.y - cy) / ry;
        float dist = sqrtf(dx * dx + dy * dy);

        // Near border or inside if filled
        return (dist >= 1.0f - tolerance / rx && dist <= 1.0f + tolerance / rx) ||
               (filled && dist <= 1.0f);
    } else {
        // Rectangle - check if near border or inside if filled
        bool nearLeft = abs(pt.x - bounds.left) <= tolerance && pt.y >= bounds.top && pt.y <= bounds.bottom;
        bool nearRight = abs(pt.x - bounds.right) <= tolerance && pt.y >= bounds.top && pt.y <= bounds.bottom;
        bool nearTop = abs(pt.y - bounds.top) <= tolerance && pt.x >= bounds.left && pt.x <= bounds.right;
        bool nearBottom = abs(pt.y - bounds.bottom) <= tolerance && pt.x >= bounds.left && pt.x <= bounds.right;

        bool inside = pt.x >= bounds.left && pt.x <= bounds.right &&
                      pt.y >= bounds.top && pt.y <= bounds.bottom;

        return nearLeft || nearRight || nearTop || nearBottom || (filled && inside);
    }
}

void ShapeObject::Move(int dx, int dy) {
    bounds.left += dx;
    bounds.right += dx;
    bounds.top += dy;
    bounds.bottom += dy;
}

void ShapeObject::SetBounds(const RECT& newBounds) {
    bounds = newBounds;
}

// Path hit test - check distance to any line segment
bool PathObject::HitTest(POINT pt) const {
    float tolerance = thickness + 5;
    if (isHighlighter) tolerance = thickness * 1.5f + 5;

    for (size_t i = 1; i < points.size(); i++) {
        POINT p1 = points[i - 1];
        POINT p2 = points[i];

        float dx = (float)(p2.x - p1.x);
        float dy = (float)(p2.y - p1.y);
        float len2 = dx * dx + dy * dy;

        if (len2 == 0) continue;

        float t = (std::max)(0.0f, (std::min)(1.0f, ((pt.x - p1.x) * dx + (pt.y - p1.y) * dy) / len2));
        float projX = p1.x + t * dx;
        float projY = p1.y + t * dy;
        float dist = sqrtf((pt.x - projX) * (pt.x - projX) + (pt.y - projY) * (pt.y - projY));

        if (dist <= tolerance) return true;
    }
    return false;
}

void PathObject::Move(int dx, int dy) {
    for (auto& pt : points) {
        pt.x += dx;
        pt.y += dy;
    }
}

void PathObject::SetBounds(const RECT& newBounds) {
    RECT oldBounds = GetBounds();
    if (oldBounds.right == oldBounds.left || oldBounds.bottom == oldBounds.top) return;

    float scaleX = (float)(newBounds.right - newBounds.left) / (float)(oldBounds.right - oldBounds.left);
    float scaleY = (float)(newBounds.bottom - newBounds.top) / (float)(oldBounds.bottom - oldBounds.top);

    for (auto& pt : points) {
        pt.x = newBounds.left + (LONG)((pt.x - oldBounds.left) * scaleX);
        pt.y = newBounds.top + (LONG)((pt.y - oldBounds.top) * scaleY);
    }
}

// Text hit test
bool TextObject::HitTest(POINT pt) const {
    return pt.x >= bounds.left && pt.x <= bounds.right &&
           pt.y >= bounds.top && pt.y <= bounds.bottom;
}

void TextObject::Move(int dx, int dy) {
    bounds.left += dx;
    bounds.right += dx;
    bounds.top += dy;
    bounds.bottom += dy;
}

void TextObject::SetBounds(const RECT& newBounds) {
    bounds = newBounds;
}

// Blur region hit test
bool BlurRegion::HitTest(POINT pt) const {
    return pt.x >= bounds.left && pt.x <= bounds.right &&
           pt.y >= bounds.top && pt.y <= bounds.bottom;
}

void BlurRegion::Move(int dx, int dy) {
    bounds.left += dx;
    bounds.right += dx;
    bounds.top += dy;
    bounds.bottom += dy;
}

void BlurRegion::SetBounds(const RECT& newBounds) {
    bounds = newBounds;
}

// NumberedStepObject implementation
void NumberedStepObject::Draw(Gdiplus::Graphics& g, float zoom, POINT offset) {
    float cx = center.x * zoom + offset.x;
    float cy = center.y * zoom + offset.y;
    float r = radius * zoom;

    // Draw filled circle
    Gdiplus::SolidBrush circleBrush(color);
    g.FillEllipse(&circleBrush, cx - r, cy - r, r * 2, r * 2);

    // Draw number in center
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, r * 0.9f, Gdiplus::FontStyleBold);
    Gdiplus::SolidBrush textBrush(textColor);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    wchar_t numStr[8];
    swprintf_s(numStr, L"%d", number);
    Gdiplus::RectF rect(cx - r, cy - r, r * 2, r * 2);
    g.DrawString(numStr, -1, &font, rect, &format, &textBrush);
}

RECT NumberedStepObject::GetBounds() const {
    return { center.x - radius, center.y - radius, center.x + radius, center.y + radius };
}

bool NumberedStepObject::HitTest(POINT pt) const {
    float dx = (float)(pt.x - center.x);
    float dy = (float)(pt.y - center.y);
    return (dx * dx + dy * dy) <= (float)(radius * radius);
}

void NumberedStepObject::Move(int dx, int dy) {
    center.x += dx;
    center.y += dy;
}

void NumberedStepObject::SetBounds(const RECT& newBounds) {
    center.x = (newBounds.left + newBounds.right) / 2;
    center.y = (newBounds.top + newBounds.bottom) / 2;
    radius = (std::min)(newBounds.right - newBounds.left, newBounds.bottom - newBounds.top) / 2;
}

// CalloutObject implementation
void CalloutObject::Draw(Gdiplus::Graphics& g, float zoom, POINT offset) {
    float x = bounds.left * zoom + offset.x;
    float y = bounds.top * zoom + offset.y;
    float w = (bounds.right - bounds.left) * zoom;
    float h = (bounds.bottom - bounds.top) * zoom;
    float tx = tailPoint.x * zoom + offset.x;
    float ty = tailPoint.y * zoom + offset.y;

    // Don't draw if too small
    if (w < 10 || h < 10) {
        // Just draw a simple rectangle outline during initial drawing
        Gdiplus::Pen pen(strokeColor, 2 * zoom);
        pen.SetDashStyle(Gdiplus::DashStyleDash);
        g.DrawRectangle(&pen, x, y, w, h);
        return;
    }

    // Create path for speech bubble
    Gdiplus::GraphicsPath path;
    float cornerRadius = (std::min)(10.0f * zoom, (std::min)(w / 4, h / 4));

    // Tail dimensions - position tail base where tail tip is (clamped to bubble bounds)
    float tailWidth = 20 * zoom;
    float tailBaseX = (std::max)(x + cornerRadius + tailWidth / 2,
                       (std::min)(tx, x + w - cornerRadius - tailWidth / 2));

    // Only draw tail if tail point is below the bubble
    bool drawTail = (ty > y + h);

    // Build the rounded rectangle with tail
    path.StartFigure();

    // Top edge (left to right)
    path.AddArc(x, y, cornerRadius * 2, cornerRadius * 2, 180, 90);
    path.AddLine(x + cornerRadius, y, x + w - cornerRadius, y);
    path.AddArc(x + w - cornerRadius * 2, y, cornerRadius * 2, cornerRadius * 2, 270, 90);

    // Right edge
    path.AddLine(x + w, y + cornerRadius, x + w, y + h - cornerRadius);
    path.AddArc(x + w - cornerRadius * 2, y + h - cornerRadius * 2, cornerRadius * 2, cornerRadius * 2, 0, 90);

    // Bottom edge with tail (right to left)
    if (drawTail) {
        path.AddLine(x + w - cornerRadius, y + h, tailBaseX + tailWidth / 2, y + h);
        path.AddLine(tailBaseX + tailWidth / 2, y + h, tx, ty);  // Tail right side
        path.AddLine(tx, ty, tailBaseX - tailWidth / 2, y + h);  // Tail left side
        path.AddLine(tailBaseX - tailWidth / 2, y + h, x + cornerRadius, y + h);
    } else {
        path.AddLine(x + w - cornerRadius, y + h, x + cornerRadius, y + h);
    }

    // Left edge
    path.AddArc(x, y + h - cornerRadius * 2, cornerRadius * 2, cornerRadius * 2, 90, 90);
    path.AddLine(x, y + h - cornerRadius, x, y + cornerRadius);

    path.CloseFigure();

    // Fill and stroke
    Gdiplus::SolidBrush brush(fillColor);
    g.FillPath(&brush, &path);
    Gdiplus::Pen pen(strokeColor, 2 * zoom);
    g.DrawPath(&pen, &path);

    // Draw text
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, fontSize * zoom);
    Gdiplus::SolidBrush textBrush(textColor);
    Gdiplus::RectF textRect(x + 8 * zoom, y + 5 * zoom, w - 16 * zoom, h - 10 * zoom);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentNear);
    g.DrawString(text.c_str(), -1, &font, textRect, &format, &textBrush);
}

RECT CalloutObject::GetBounds() const {
    RECT r = bounds;
    // Include tail point
    r.left = (std::min)(r.left, tailPoint.x);
    r.top = (std::min)(r.top, tailPoint.y);
    r.right = (std::max)(r.right, tailPoint.x);
    r.bottom = (std::max)(r.bottom, tailPoint.y);
    return r;
}

bool CalloutObject::HitTest(POINT pt) const {
    return pt.x >= bounds.left && pt.x <= bounds.right &&
           pt.y >= bounds.top && pt.y <= bounds.bottom;
}

void CalloutObject::Move(int dx, int dy) {
    bounds.left += dx;
    bounds.right += dx;
    bounds.top += dy;
    bounds.bottom += dy;
    tailPoint.x += dx;
    tailPoint.y += dy;
}

void CalloutObject::SetBounds(const RECT& newBounds) {
    int dx = newBounds.left - bounds.left;
    int dy = newBounds.top - bounds.top;
    bounds = newBounds;
    tailPoint.x += dx;
    tailPoint.y += dy;
}

// StampObject implementation
void StampObject::Draw(Gdiplus::Graphics& g, float zoom, POINT offset) {
    float cx = center.x * zoom + offset.x;
    float cy = center.y * zoom + offset.y;
    float sz = size * zoom;

    Gdiplus::FontFamily family(L"Segoe UI Symbol");
    Gdiplus::Font font(&family, sz * 0.8f, Gdiplus::FontStyleBold);
    Gdiplus::SolidBrush brush(color);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    const wchar_t* icon = STAMP_ICONS[(int)stampType];
    Gdiplus::RectF rect(cx - sz / 2, cy - sz / 2, sz, sz);
    g.DrawString(icon, -1, &font, rect, &format, &brush);
}

RECT StampObject::GetBounds() const {
    int half = size / 2;
    return { center.x - half, center.y - half, center.x + half, center.y + half };
}

bool StampObject::HitTest(POINT pt) const {
    int half = size / 2;
    return pt.x >= center.x - half && pt.x <= center.x + half &&
           pt.y >= center.y - half && pt.y <= center.y + half;
}

void StampObject::Move(int dx, int dy) {
    center.x += dx;
    center.y += dy;
}

void StampObject::SetBounds(const RECT& newBounds) {
    center.x = (newBounds.left + newBounds.right) / 2;
    center.y = (newBounds.top + newBounds.bottom) / 2;
    size = (std::min)(newBounds.right - newBounds.left, newBounds.bottom - newBounds.top);
}

// MagnifierObject implementation
void MagnifierObject::Draw(Gdiplus::Graphics& g, float zoom, POINT offset) {
    // Note: Actual magnification would need access to the image
    // For now, just draw a circle with a magnifier indicator
    float cx = center.x * zoom + offset.x;
    float cy = center.y * zoom + offset.y;
    float r = radius * zoom;

    // Draw circle outline
    Gdiplus::Pen pen(Gdiplus::Color(255, 50, 50, 50), 3 * zoom);
    g.DrawEllipse(&pen, cx - r, cy - r, r * 2, r * 2);

    // Draw magnifier handle
    float handleLen = r * 0.7f;
    float angle = 0.785f; // 45 degrees
    g.DrawLine(&pen, cx + r * cosf(angle), cy + r * sinf(angle),
               cx + (r + handleLen) * cosf(angle), cy + (r + handleLen) * sinf(angle));

    // Draw magnification indicator
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, 12 * zoom);
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, 50, 50, 50));
    wchar_t magStr[16];
    swprintf_s(magStr, L"%.1fx", magnification);
    g.DrawString(magStr, -1, &font, Gdiplus::PointF(cx - 15 * zoom, cy - 8 * zoom), &brush);
}

RECT MagnifierObject::GetBounds() const {
    int r = radius + (int)(radius * 0.7f);  // Include handle
    return { center.x - radius, center.y - radius, center.x + r, center.y + r };
}

bool MagnifierObject::HitTest(POINT pt) const {
    float dx = (float)(pt.x - center.x);
    float dy = (float)(pt.y - center.y);
    return (dx * dx + dy * dy) <= (float)(radius * radius);
}

void MagnifierObject::Move(int dx, int dy) {
    center.x += dx;
    center.y += dy;
}

void MagnifierObject::SetBounds(const RECT& newBounds) {
    center.x = (newBounds.left + newBounds.right) / 2;
    center.y = (newBounds.top + newBounds.bottom) / 2;
    radius = (std::min)(newBounds.right - newBounds.left, newBounds.bottom - newBounds.top) / 2;
}

// Selection helpers
int GetSelectionHandleAtPoint(EditorState* state, POINT canvasPt) {
    if (state->selectedObject < 0 || state->selectedObject >= (int)state->objects.size())
        return -1;

    RECT bounds = state->objects[state->selectedObject]->GetBounds();
    const int handleSize = 8;

    // Handle positions: 0=TL, 1=T, 2=TR, 3=R, 4=BR, 5=B, 6=BL, 7=L
    POINT handles[8] = {
        { bounds.left, bounds.top },                                    // 0: Top-left
        { (bounds.left + bounds.right) / 2, bounds.top },               // 1: Top
        { bounds.right, bounds.top },                                   // 2: Top-right
        { bounds.right, (bounds.top + bounds.bottom) / 2 },             // 3: Right
        { bounds.right, bounds.bottom },                                // 4: Bottom-right
        { (bounds.left + bounds.right) / 2, bounds.bottom },            // 5: Bottom
        { bounds.left, bounds.bottom },                                 // 6: Bottom-left
        { bounds.left, (bounds.top + bounds.bottom) / 2 },              // 7: Left
    };

    for (int i = 0; i < 8; i++) {
        RECT handleRect = {
            handles[i].x - handleSize / 2, handles[i].y - handleSize / 2,
            handles[i].x + handleSize / 2, handles[i].y + handleSize / 2
        };
        if (canvasPt.x >= handleRect.left && canvasPt.x <= handleRect.right &&
            canvasPt.y >= handleRect.top && canvasPt.y <= handleRect.bottom) {
            return i;
        }
    }

    // Check if inside bounds (for move)
    if (canvasPt.x >= bounds.left && canvasPt.x <= bounds.right &&
        canvasPt.y >= bounds.top && canvasPt.y <= bounds.bottom) {
        return -2;  // Inside = move
    }

    return -1;  // Not on selection
}

void DrawSelectionHandles(Gdiplus::Graphics& g, const RECT& bounds, float zoom, POINT offset) {
    const int handleSize = 8;

    // Draw selection rectangle
    float x = bounds.left * zoom + offset.x;
    float y = bounds.top * zoom + offset.y;
    float w = (bounds.right - bounds.left) * zoom;
    float h = (bounds.bottom - bounds.top) * zoom;

    Gdiplus::Pen borderPen(Gdiplus::Color(200, 0, 120, 255), 1.5f);
    borderPen.SetDashStyle(Gdiplus::DashStyleDash);
    g.DrawRectangle(&borderPen, x, y, w, h);

    // Handle positions in canvas coords, then transform
    POINT handles[8] = {
        { bounds.left, bounds.top },
        { (bounds.left + bounds.right) / 2, bounds.top },
        { bounds.right, bounds.top },
        { bounds.right, (bounds.top + bounds.bottom) / 2 },
        { bounds.right, bounds.bottom },
        { (bounds.left + bounds.right) / 2, bounds.bottom },
        { bounds.left, bounds.bottom },
        { bounds.left, (bounds.top + bounds.bottom) / 2 },
    };

    Gdiplus::SolidBrush fillBrush(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::Pen handlePen(Gdiplus::Color(255, 0, 120, 255), 1.5f);

    for (int i = 0; i < 8; i++) {
        float hx = handles[i].x * zoom + offset.x - handleSize / 2;
        float hy = handles[i].y * zoom + offset.y - handleSize / 2;
        g.FillRectangle(&fillBrush, hx, hy, (float)handleSize, (float)handleSize);
        g.DrawRectangle(&handlePen, hx, hy, (float)handleSize, (float)handleSize);
    }
}

HCURSOR GetSelectionCursor(int handle) {
    switch (handle) {
        case -2: return LoadCursor(nullptr, IDC_SIZEALL);  // Move
        case 0: case 4: return LoadCursor(nullptr, IDC_SIZENWSE);  // TL, BR
        case 2: case 6: return LoadCursor(nullptr, IDC_SIZENESW);  // TR, BL
        case 1: case 5: return LoadCursor(nullptr, IDC_SIZENS);    // T, B
        case 3: case 7: return LoadCursor(nullptr, IDC_SIZEWE);    // R, L
        default: return LoadCursor(nullptr, IDC_ARROW);
    }
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
    for (int i = 0; i < (int)EditorTool::COUNT; i++) {
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

    x = state->colorPickerRect.right + 4;

    // Custom color button (+ symbol)
    state->customColorRect = { x, y + 6, x + 20, y + EDITOR_TOOL_SIZE - 6 };
    DrawRoundedRect(hdc, state->customColorRect, 4, Colors::Surface);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, Colors::TextSecondary);
    HFONT oldFont2 = (HFONT)SelectObject(hdc, g_app.fontSmall);
    DrawTextW(hdc, L"+", -1, &state->customColorRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont2);
    x += 24;

    // Recent colors display
    if (state->numRecentColors > 0) {
        state->recentColorsRect = { x, y, x + (state->numRecentColors * 16) + 4, y + EDITOR_TOOL_SIZE };
        for (int i = 0; i < state->numRecentColors; i++) {
            RECT colorRect = { x + i * 16 + 2, y + 8, x + i * 16 + 14, y + EDITOR_TOOL_SIZE - 8 };
            Gdiplus::Color c = state->recentColors[i];
            bool isSelected = (state->currentColor.GetValue() == c.GetValue());
            if (isSelected) {
                RECT borderRect = { colorRect.left - 1, colorRect.top - 1, colorRect.right + 1, colorRect.bottom + 1 };
                DrawRoundedRect(hdc, borderRect, 3, Colors::Accent);
            }
            HBRUSH colorBrush = CreateSolidBrush(RGB(c.GetR(), c.GetG(), c.GetB()));
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, colorBrush);
            RoundRect(hdc, colorRect.left, colorRect.top, colorRect.right, colorRect.bottom, 3, 3);
            SelectObject(hdc, oldBrush);
            DeleteObject(colorBrush);
        }
        x = state->recentColorsRect.right + 4;
    } else {
        state->recentColorsRect = { 0, 0, 0, 0 };
    }

    // Fill toggle button (only show for shape tools)
    EditorTool tool = state->currentTool;
    bool isShapeTool = (tool == EditorTool::Rectangle || tool == EditorTool::Ellipse);
    if (isShapeTool) {
        state->fillToggleRect = { x, y, x + EDITOR_TOOL_SIZE, y + EDITOR_TOOL_SIZE };
        DrawRoundedRect(hdc, state->fillToggleRect, 6, state->fillShapes ? Colors::AccentDark : Colors::Surface);
        SetTextColor(hdc, state->fillShapes ? Colors::Text : Colors::TextSecondary);
        HFONT oldFont3 = (HFONT)SelectObject(hdc, g_app.fontIcon);
        DrawTextW(hdc, L"\u25A0", -1, &state->fillToggleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);  // Filled square
        SelectObject(hdc, oldFont3);
        x += EDITOR_TOOL_SIZE + 4;
    } else {
        state->fillToggleRect = { 0, 0, 0, 0 };  // Hide
    }

    // Text tool options: Bold, Italic, Font picker, Background
    bool isTextTool = (tool == EditorTool::Text || tool == EditorTool::Callout);
    if (isTextTool) {
        // Bold button
        state->boldRect = { x, y, x + EDITOR_TOOL_SIZE, y + EDITOR_TOOL_SIZE };
        DrawRoundedRect(hdc, state->boldRect, 6, state->textBold ? Colors::AccentDark : Colors::Surface);
        SetTextColor(hdc, state->textBold ? Colors::Text : Colors::TextSecondary);
        HFONT boldFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT oldF = (HFONT)SelectObject(hdc, boldFont);
        DrawTextW(hdc, L"B", -1, &state->boldRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldF);
        DeleteObject(boldFont);
        x += EDITOR_TOOL_SIZE + 2;

        // Italic button
        state->italicRect = { x, y, x + EDITOR_TOOL_SIZE, y + EDITOR_TOOL_SIZE };
        DrawRoundedRect(hdc, state->italicRect, 6, state->textItalic ? Colors::AccentDark : Colors::Surface);
        SetTextColor(hdc, state->textItalic ? Colors::Text : Colors::TextSecondary);
        HFONT italicFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        oldF = (HFONT)SelectObject(hdc, italicFont);
        DrawTextW(hdc, L"I", -1, &state->italicRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldF);
        DeleteObject(italicFont);
        x += EDITOR_TOOL_SIZE + 2;

        // Font picker button
        state->fontPickerRect = { x, y, x + 80, y + EDITOR_TOOL_SIZE };
        DrawRoundedRect(hdc, state->fontPickerRect, 6, Colors::Surface);
        SetTextColor(hdc, Colors::Text);
        HFONT oldFont3 = (HFONT)SelectObject(hdc, g_app.fontSmall);
        RECT fontTextRect = state->fontPickerRect;
        fontTextRect.right -= 16;
        DrawTextW(hdc, AVAILABLE_FONTS[state->currentFontIndex], -1, &fontTextRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        // Draw dropdown arrow
        RECT arrowRect = { state->fontPickerRect.right - 16, y, state->fontPickerRect.right, y + EDITOR_TOOL_SIZE };
        DrawTextW(hdc, L"\u25BC", -1, &arrowRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont3);
        x += 84;

        // Text background toggle button
        state->textBgRect = { x, y, x + EDITOR_TOOL_SIZE, y + EDITOR_TOOL_SIZE };
        DrawRoundedRect(hdc, state->textBgRect, 6, state->textHasBackground ? Colors::AccentDark : Colors::Surface);
        SetTextColor(hdc, state->textHasBackground ? Colors::Text : Colors::TextSecondary);
        oldFont3 = (HFONT)SelectObject(hdc, g_app.fontIcon);
        DrawTextW(hdc, L"\u2588", -1, &state->textBgRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);  // Filled block
        SelectObject(hdc, oldFont3);
        x += EDITOR_TOOL_SIZE + 4;
    } else {
        state->boldRect = { 0, 0, 0, 0 };
        state->italicRect = { 0, 0, 0, 0 };
        state->fontPickerRect = { 0, 0, 0, 0 };
        state->textBgRect = { 0, 0, 0, 0 };
    }

    // Arrow/Line tool options: Line style, Arrow head style
    bool isLineTool = (tool == EditorTool::Arrow || tool == EditorTool::Line);
    if (isLineTool) {
        // Line style button
        state->lineStyleRect = { x, y, x + EDITOR_TOOL_SIZE, y + EDITOR_TOOL_SIZE };
        DrawRoundedRect(hdc, state->lineStyleRect, 6, Colors::Surface);
        SetTextColor(hdc, Colors::Text);
        HFONT oldFont3 = (HFONT)SelectObject(hdc, g_app.fontSmall);
        const wchar_t* lineIcon = L"—";
        if (state->currentLineStyle == LineStyle::Dashed) lineIcon = L"--";
        else if (state->currentLineStyle == LineStyle::Dotted) lineIcon = L"··";
        DrawTextW(hdc, lineIcon, -1, &state->lineStyleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont3);
        x += EDITOR_TOOL_SIZE + 2;

        // Arrow head style button (only for Arrow tool)
        if (tool == EditorTool::Arrow) {
            state->arrowStyleRect = { x, y, x + EDITOR_TOOL_SIZE, y + EDITOR_TOOL_SIZE };
            DrawRoundedRect(hdc, state->arrowStyleRect, 6, Colors::Surface);
            SetTextColor(hdc, Colors::Text);
            oldFont3 = (HFONT)SelectObject(hdc, g_app.fontIcon);
            const wchar_t* arrowIcon = L"\u25B6";  // Filled triangle
            if (state->currentArrowStyle == ArrowHeadStyle::Open) arrowIcon = L"\u25B7";  // Open triangle
            else if (state->currentArrowStyle == ArrowHeadStyle::Diamond) arrowIcon = L"\u25C6";  // Diamond
            else if (state->currentArrowStyle == ArrowHeadStyle::None) arrowIcon = L"—";
            DrawTextW(hdc, arrowIcon, -1, &state->arrowStyleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oldFont3);
            x += EDITOR_TOOL_SIZE + 2;

            // Double-ended arrow toggle
            state->doubleArrowRect = { x, y, x + EDITOR_TOOL_SIZE, y + EDITOR_TOOL_SIZE };
            DrawRoundedRect(hdc, state->doubleArrowRect, 6, state->doubleEndedArrow ? Colors::AccentDark : Colors::Surface);
            SetTextColor(hdc, state->doubleEndedArrow ? Colors::Text : Colors::TextSecondary);
            oldFont3 = (HFONT)SelectObject(hdc, g_app.fontIcon);
            DrawTextW(hdc, L"\u2194", -1, &state->doubleArrowRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);  // Left-right arrow
            SelectObject(hdc, oldFont3);
            x += EDITOR_TOOL_SIZE + 2;
        } else {
            state->arrowStyleRect = { 0, 0, 0, 0 };
            state->doubleArrowRect = { 0, 0, 0, 0 };
        }
    } else {
        state->lineStyleRect = { 0, 0, 0, 0 };
        state->arrowStyleRect = { 0, 0, 0, 0 };
        state->doubleArrowRect = { 0, 0, 0, 0 };
    }

    // Stamp tool options: Stamp type picker
    if (tool == EditorTool::Stamp) {
        state->stampPickerRect = { x, y, x + EDITOR_TOOL_SIZE, y + EDITOR_TOOL_SIZE };
        DrawRoundedRect(hdc, state->stampPickerRect, 6, Colors::Surface);
        SetTextColor(hdc, Colors::Text);
        HFONT oldFont3 = (HFONT)SelectObject(hdc, g_app.fontIcon);
        DrawTextW(hdc, STAMP_ICONS[(int)state->currentStampType], -1, &state->stampPickerRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont3);
        x += EDITOR_TOOL_SIZE + 4;
    } else {
        state->stampPickerRect = { 0, 0, 0, 0 };
    }

    // Image manipulation buttons (always visible)
    x += 4;

    // Rotate left
    state->rotateLeftRect = { x, y, x + EDITOR_TOOL_SIZE, y + EDITOR_TOOL_SIZE };
    DrawRoundedRect(hdc, state->rotateLeftRect, 6, Colors::Surface);
    SetTextColor(hdc, Colors::TextSecondary);
    HFONT oldFont3 = (HFONT)SelectObject(hdc, g_app.fontIcon);
    DrawTextW(hdc, L"\u21BA", -1, &state->rotateLeftRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);  // CCW arrow
    SelectObject(hdc, oldFont3);
    x += EDITOR_TOOL_SIZE + 2;

    // Rotate right
    state->rotateRightRect = { x, y, x + EDITOR_TOOL_SIZE, y + EDITOR_TOOL_SIZE };
    DrawRoundedRect(hdc, state->rotateRightRect, 6, Colors::Surface);
    SetTextColor(hdc, Colors::TextSecondary);
    oldFont3 = (HFONT)SelectObject(hdc, g_app.fontIcon);
    DrawTextW(hdc, L"\u21BB", -1, &state->rotateRightRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);  // CW arrow
    SelectObject(hdc, oldFont3);
    x += EDITOR_TOOL_SIZE + 2;

    // Flip horizontal
    state->flipHRect = { x, y, x + EDITOR_TOOL_SIZE, y + EDITOR_TOOL_SIZE };
    DrawRoundedRect(hdc, state->flipHRect, 6, Colors::Surface);
    SetTextColor(hdc, Colors::TextSecondary);
    oldFont3 = (HFONT)SelectObject(hdc, g_app.fontIcon);
    DrawTextW(hdc, L"\u2194", -1, &state->flipHRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);  // Left-right arrow
    SelectObject(hdc, oldFont3);
    x += EDITOR_TOOL_SIZE + 2;

    // Flip vertical
    state->flipVRect = { x, y, x + EDITOR_TOOL_SIZE, y + EDITOR_TOOL_SIZE };
    DrawRoundedRect(hdc, state->flipVRect, 6, Colors::Surface);
    SetTextColor(hdc, Colors::TextSecondary);
    oldFont3 = (HFONT)SelectObject(hdc, g_app.fontIcon);
    DrawTextW(hdc, L"\u2195", -1, &state->flipVRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);  // Up-down arrow
    SelectObject(hdc, oldFont3);
    x += EDITOR_TOOL_SIZE + 4;

    // Draw separator
    x += 8;
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
    x += EDITOR_TOOL_SIZE + 12;

    // Zoom controls
    // Zoom out
    state->zoomOutRect = { x, y, x + 24, y + EDITOR_TOOL_SIZE };
    DrawRoundedRect(hdc, state->zoomOutRect, 4, Colors::Surface);
    SetTextColor(hdc, Colors::Text);
    oldFont = (HFONT)SelectObject(hdc, g_app.fontSmall);
    DrawTextW(hdc, L"-", -1, &state->zoomOutRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
    x += 26;

    // Zoom percentage
    wchar_t zoomText[32];
    swprintf_s(zoomText, L"%d%%", (int)(state->zoom * 100));
    RECT zoomTextRect = { x, y, x + 45, y + EDITOR_TOOL_SIZE };
    SetTextColor(hdc, Colors::Text);
    oldFont = (HFONT)SelectObject(hdc, g_app.fontSmall);
    DrawTextW(hdc, zoomText, -1, &zoomTextRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
    x += 47;

    // Zoom in
    state->zoomInRect = { x, y, x + 24, y + EDITOR_TOOL_SIZE };
    DrawRoundedRect(hdc, state->zoomInRect, 4, Colors::Surface);
    SetTextColor(hdc, Colors::Text);
    oldFont = (HFONT)SelectObject(hdc, g_app.fontSmall);
    DrawTextW(hdc, L"+", -1, &state->zoomInRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
    x += 28;

    // Fit button
    state->zoomFitRect = { x, y, x + 30, y + EDITOR_TOOL_SIZE };
    DrawRoundedRect(hdc, state->zoomFitRect, 4, Colors::Surface);
    SetTextColor(hdc, Colors::TextSecondary);
    oldFont = (HFONT)SelectObject(hdc, g_app.fontIcon);
    DrawTextW(hdc, L"\u2922", -1, &state->zoomFitRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);  // Expand arrows
    SelectObject(hdc, oldFont);
    x += 34;

    // 100% button
    state->zoom100Rect = { x, y, x + 36, y + EDITOR_TOOL_SIZE };
    DrawRoundedRect(hdc, state->zoom100Rect, 4, state->zoom == 1.0f ? Colors::AccentDark : Colors::Surface);
    SetTextColor(hdc, state->zoom == 1.0f ? Colors::Text : Colors::TextSecondary);
    oldFont = (HFONT)SelectObject(hdc, g_app.fontSmall);
    DrawTextW(hdc, L"1:1", -1, &state->zoom100Rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
    x += 44;

    // Save button
    state->saveRect = { x, y, x + 60, y + EDITOR_TOOL_SIZE };
    DrawRoundedRect(hdc, state->saveRect, 6, Colors::AccentDark);
    SetTextColor(hdc, Colors::Text);
    oldFont = (HFONT)SelectObject(hdc, g_app.fontSmall);
    DrawTextW(hdc, L"Save", -1, &state->saveRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

void DrawEditorStatusBar(HDC hdc, EditorState* state) {
    RECT& statusBar = state->statusBarRect;

    // Background
    HBRUSH bgBrush = CreateSolidBrush(Colors::Background);
    FillRect(hdc, &statusBar, bgBrush);
    DeleteObject(bgBrush);

    // Top divider line
    HPEN divPen = CreatePen(PS_SOLID, 1, Colors::Divider);
    SelectObject(hdc, divPen);
    MoveToEx(hdc, statusBar.left, statusBar.top, nullptr);
    LineTo(hdc, statusBar.right, statusBar.top);
    DeleteObject(divPen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, Colors::TextSecondary);
    HFONT oldFont = (HFONT)SelectObject(hdc, g_app.fontSmall);

    int x = statusBar.left + 12;

    // Current tool name
    int toolIdx = (int)state->currentTool;
    if (toolIdx >= 0 && toolIdx < (int)EditorTool::COUNT) {
        RECT toolRect = { x, statusBar.top, x + 120, statusBar.bottom };
        DrawTextW(hdc, TOOL_NAMES[toolIdx], -1, &toolRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        x += 130;
    }

    // Separator
    HPEN sepPen = CreatePen(PS_SOLID, 1, Colors::Divider);
    SelectObject(hdc, sepPen);
    MoveToEx(hdc, x, statusBar.top + 4, nullptr);
    LineTo(hdc, x, statusBar.bottom - 4);
    DeleteObject(sepPen);
    x += 12;

    // Image dimensions
    if (state->displayImage) {
        wchar_t dimText[64];
        swprintf_s(dimText, L"%d \u00D7 %d px",
            state->displayImage->GetWidth(), state->displayImage->GetHeight());
        RECT dimRect = { x, statusBar.top, x + 120, statusBar.bottom };
        DrawTextW(hdc, dimText, -1, &dimRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        x += 130;
    }

    // Object count
    if (!state->objects.empty()) {
        HPEN sepPen2 = CreatePen(PS_SOLID, 1, Colors::Divider);
        SelectObject(hdc, sepPen2);
        MoveToEx(hdc, x, statusBar.top + 4, nullptr);
        LineTo(hdc, x, statusBar.bottom - 4);
        DeleteObject(sepPen2);
        x += 12;

        wchar_t objText[32];
        swprintf_s(objText, L"%d annotations", (int)state->objects.size());
        RECT objRect = { x, statusBar.top, x + 100, statusBar.bottom };
        DrawTextW(hdc, objText, -1, &objRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // Right side: hints
    const wchar_t* hint = L"";
    if (state->currentTool == EditorTool::Select && state->selectedObject >= 0) {
        hint = L"Del: Delete  |  Ctrl+C: Copy  |  Ctrl+V: Paste";
    } else if (state->currentTool == EditorTool::Crop) {
        hint = L"Enter: Apply crop  |  Esc: Cancel";
    } else if (state->textInputActive) {
        hint = L"Enter: Confirm  |  Esc: Cancel";
    } else {
        hint = L"Right-click tool for settings  |  Scroll to zoom";
    }

    RECT hintRect = { statusBar.right - 350, statusBar.top, statusBar.right - 12, statusBar.bottom };
    DrawTextW(hdc, hint, -1, &hintRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

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
    for (size_t i = 0; i < state->objects.size(); i++) {
        // Skip text object being edited (it's drawn separately in the text input box)
        if (state->textInputActive && state->editingTextIndex == (int)i) continue;
        state->objects[i]->Draw(g, state->zoom, drawOffset);
    }

    // Draw active object being created
    if ((state->isDrawing || state->calloutInputActive) && state->activeObject) {
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

    // Draw selection handles for selected object
    if (state->selectedObject >= 0 && state->selectedObject < (int)state->objects.size()) {
        RECT bounds = state->objects[state->selectedObject]->GetBounds();
        DrawSelectionHandles(g, bounds, state->zoom, drawOffset);

        // Special tail handle for callouts
        auto* callout = dynamic_cast<CalloutObject*>(state->objects[state->selectedObject].get());
        if (callout) {
            float tx = callout->tailPoint.x * state->zoom + drawOffset.x;
            float ty = callout->tailPoint.y * state->zoom + drawOffset.y;
            // Draw tail handle as a filled diamond
            Gdiplus::PointF diamond[4] = {
                { tx, ty - 6 }, { tx + 6, ty }, { tx, ty + 6 }, { tx - 6, ty }
            };
            Gdiplus::SolidBrush fillBrush(Gdiplus::Color(255, 255, 200, 0));
            Gdiplus::Pen outlinePen(Gdiplus::Color(255, 0, 0, 0), 1);
            g.FillPolygon(&fillBrush, diamond, 4);
            g.DrawPolygon(&outlinePen, diamond, 4);
        }
    }

    // Draw text box while drawing
    if (state->isDrawing && state->currentTool == EditorTool::Text) {
        RECT tb = state->textBounds;
        // Normalize for display
        if (tb.left > tb.right) std::swap(tb.left, tb.right);
        if (tb.top > tb.bottom) std::swap(tb.top, tb.bottom);

        float x = tb.left * state->zoom + drawOffset.x;
        float y = tb.top * state->zoom + drawOffset.y;
        float w = (tb.right - tb.left) * state->zoom;
        float h = (tb.bottom - tb.top) * state->zoom;

        // Draw dashed box outline
        Gdiplus::Pen boxPen(state->currentColor, 1);
        boxPen.SetDashStyle(Gdiplus::DashStyleDash);
        g.DrawRectangle(&boxPen, x, y, w, h);
    }

    // Draw text input box and cursor
    if (state->textInputActive) {
        float x = state->textBounds.left * state->zoom + drawOffset.x;
        float y = state->textBounds.top * state->zoom + drawOffset.y;
        float w = (state->textBounds.right - state->textBounds.left) * state->zoom;
        float h = (state->textBounds.bottom - state->textBounds.top) * state->zoom;

        // Draw background if enabled
        if (state->textHasBackground) {
            Gdiplus::SolidBrush bgBrush(state->textBackgroundColor);
            g.FillRectangle(&bgBrush, x, y, w, h);
        }

        // Draw box outline
        Gdiplus::Pen boxPen(state->currentColor, 2);
        g.DrawRectangle(&boxPen, x, y, w, h);

        // Draw text within bounds using selected font and style
        Gdiplus::FontFamily family(AVAILABLE_FONTS[state->currentFontIndex]);
        int fontStyle = Gdiplus::FontStyleRegular;
        if (state->textBold) fontStyle |= Gdiplus::FontStyleBold;
        if (state->textItalic) fontStyle |= Gdiplus::FontStyleItalic;
        Gdiplus::Font font(&family, state->currentTextSize * state->zoom, fontStyle);
        Gdiplus::SolidBrush brush(state->currentColor);
        Gdiplus::RectF layoutRect(x + 4, y + 2, w - 8, h - 4);
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentNear);
        format.SetLineAlignment(Gdiplus::StringAlignmentNear);

        if (!state->textBuffer.empty()) {
            g.DrawString(state->textBuffer.c_str(), -1, &font, layoutRect, &format, &brush);
        }

        // Draw blinking cursor
        static DWORD lastBlink = 0;
        static bool cursorVisible = true;
        DWORD now = GetTickCount();
        if (now - lastBlink > 500) {
            cursorVisible = !cursorVisible;
            lastBlink = now;
        }

        if (cursorVisible) {
            Gdiplus::RectF textBounds;
            g.MeasureString(state->textBuffer.c_str(), -1, &font, layoutRect, &format, &textBounds);
            Gdiplus::Pen cursorPen(state->currentColor, 2);
            float cursorX = x + 4 + textBounds.Width;
            float cursorY = y + 2;
            float cursorHeight = state->currentTextSize * state->zoom;
            g.DrawLine(&cursorPen, cursorX, cursorY, cursorX, cursorY + cursorHeight);
        }

        // Draw hint text
        Gdiplus::FontFamily hintFamily(L"Segoe UI");
        Gdiplus::Font hintFont(&hintFamily, 10 * state->zoom);
        Gdiplus::SolidBrush hintBrush(Gdiplus::Color(180, 255, 255, 255));
        g.DrawString(L"Ctrl+Enter to finish", -1, &hintFont,
            Gdiplus::PointF(x, y + h + 4), &hintBrush);
    }

    // Draw callout input hint
    if (state->calloutInputActive && state->activeObject) {
        auto* callout = dynamic_cast<CalloutObject*>(state->activeObject.get());
        if (callout) {
            float x = callout->bounds.left * state->zoom + drawOffset.x;
            float y = callout->bounds.top * state->zoom + drawOffset.y;
            float h = (callout->bounds.bottom - callout->bounds.top) * state->zoom;

            // Draw hint text below callout
            Gdiplus::FontFamily hintFamily(L"Segoe UI");
            Gdiplus::Font hintFont(&hintFamily, 10 * state->zoom);
            Gdiplus::SolidBrush hintBrush(Gdiplus::Color(200, 255, 255, 255));
            g.DrawString(L"Type text, Ctrl+Enter to finish, Esc to cancel", -1, &hintFont,
                Gdiplus::PointF(x, y + h + 35 * state->zoom), &hintBrush);
        }
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

// Save with format selection (0=PNG, 1=JPEG, 2=BMP)
bool SaveEditorImageEx(EditorState* state, const wchar_t* filepath, int format, int quality) {
    if (!state->displayImage) return false;

    // Render all objects to image
    RenderObjectsToImage(state);

    int width = state->displayImage->GetWidth();
    int height = state->displayImage->GetHeight();

    Gdiplus::Bitmap* saveBitmap = new Gdiplus::Bitmap(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics g(saveBitmap);
    g.DrawImage(state->displayImage, 0, 0, width, height);

    // Get encoder CLSID
    CLSID encoderClsid;
    const wchar_t* mimeType = L"image/png";
    if (format == 1) mimeType = L"image/jpeg";
    else if (format == 2) mimeType = L"image/bmp";

    UINT numEncoders, size;
    Gdiplus::GetImageEncodersSize(&numEncoders, &size);
    Gdiplus::ImageCodecInfo* encoders = (Gdiplus::ImageCodecInfo*)malloc(size);
    Gdiplus::GetImageEncoders(numEncoders, size, encoders);

    bool foundEncoder = false;
    for (UINT i = 0; i < numEncoders; i++) {
        if (wcscmp(encoders[i].MimeType, mimeType) == 0) {
            encoderClsid = encoders[i].Clsid;
            foundEncoder = true;
            break;
        }
    }
    free(encoders);

    if (!foundEncoder) {
        delete saveBitmap;
        return false;
    }

    Gdiplus::Status status;
    if (format == 1) {
        // JPEG with quality parameter
        Gdiplus::EncoderParameters encoderParams;
        encoderParams.Count = 1;
        encoderParams.Parameter[0].Guid = Gdiplus::EncoderQuality;
        encoderParams.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
        encoderParams.Parameter[0].NumberOfValues = 1;
        ULONG q = quality;
        encoderParams.Parameter[0].Value = &q;
        status = saveBitmap->Save(filepath, &encoderClsid, &encoderParams);
    } else {
        status = saveBitmap->Save(filepath, &encoderClsid);
    }

    delete saveBitmap;

    if (status == Gdiplus::Ok) {
        state->unsavedChanges = false;
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
        state->statusBarRect = { 0, clientRect.bottom - EDITOR_STATUS_HEIGHT, clientRect.right, clientRect.bottom };
        state->canvasRect = { 0, EDITOR_TOOLBAR_HEIGHT, clientRect.right, clientRect.bottom - EDITOR_STATUS_HEIGHT };

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

        // Draw status bar
        DrawEditorStatusBar(memDC, state);

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

        for (int i = 0; i < (int)EditorTool::COUNT; i++) {
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
                case (int)EditorTool::Line:
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

        // Handle callout tail dragging
        if (state->isDrawing && state->draggingCalloutTail && state->selectedObject >= 0) {
            POINT canvasPt = ScreenToCanvas(state, { x, y });
            auto* callout = dynamic_cast<CalloutObject*>(
                state->objects[state->selectedObject].get());
            if (callout) {
                callout->tailPoint = canvasPt;
                InvalidateRect(hwnd, &state->canvasRect, FALSE);
            }
            return 0;
        }

        // Handle selection move/resize
        if (state->isDrawing && state->currentTool == EditorTool::Select &&
            state->selectedObject >= 0 && state->selectionHandle >= -2) {
            POINT canvasPt = ScreenToCanvas(state, { x, y });
            int dx = canvasPt.x - state->selectionStart.x;
            int dy = canvasPt.y - state->selectionStart.y;

            EditorObject* obj = state->objects[state->selectedObject].get();
            RECT newBounds = state->originalBounds;

            if (state->selectionHandle == -2) {
                // Move - use Move() method for accurate translation
                obj->Move(dx, dy);
                state->selectionStart = canvasPt;  // Update start for next delta
            } else {
                // Resize based on handle
                switch (state->selectionHandle) {
                    case 0:  // TL
                        newBounds.left += dx;
                        newBounds.top += dy;
                        break;
                    case 1:  // T
                        newBounds.top += dy;
                        break;
                    case 2:  // TR
                        newBounds.right += dx;
                        newBounds.top += dy;
                        break;
                    case 3:  // R
                        newBounds.right += dx;
                        break;
                    case 4:  // BR
                        newBounds.right += dx;
                        newBounds.bottom += dy;
                        break;
                    case 5:  // B
                        newBounds.bottom += dy;
                        break;
                    case 6:  // BL
                        newBounds.left += dx;
                        newBounds.bottom += dy;
                        break;
                    case 7:  // L
                        newBounds.left += dx;
                        break;
                }

                // Ensure minimum size
                if (newBounds.right - newBounds.left < 5) {
                    if (state->selectionHandle == 0 || state->selectionHandle == 6 || state->selectionHandle == 7)
                        newBounds.left = newBounds.right - 5;
                    else
                        newBounds.right = newBounds.left + 5;
                }
                if (newBounds.bottom - newBounds.top < 5) {
                    if (state->selectionHandle == 0 || state->selectionHandle == 1 || state->selectionHandle == 2)
                        newBounds.top = newBounds.bottom - 5;
                    else
                        newBounds.bottom = newBounds.top + 5;
                }

                obj->SetBounds(newBounds);
            }

            InvalidateRect(hwnd, &state->canvasRect, FALSE);
            return 0;
        }

        // Update cursor for selection handles and callout tail
        if (state->currentTool == EditorTool::Select && state->selectedObject >= 0 &&
            !state->isDrawing && y > EDITOR_TOOLBAR_HEIGHT) {
            POINT canvasPt = ScreenToCanvas(state, { x, y });

            // Check for callout tail first
            auto* callout = dynamic_cast<CalloutObject*>(
                state->objects[state->selectedObject].get());
            if (callout) {
                int dx = canvasPt.x - callout->tailPoint.x;
                int dy = canvasPt.y - callout->tailPoint.y;
                if (dx * dx + dy * dy < 100) {  // 10px radius
                    SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
                    return 0;
                }
            }

            int handle = GetSelectionHandleAtPoint(state, canvasPt);
            if (handle >= -2) {
                SetCursor(GetSelectionCursor(handle));
            } else {
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
            }
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
            } else if (state->currentTool == EditorTool::Text) {
                // Update text box while drawing
                state->textBounds.right = canvasPt.x;
                state->textBounds.bottom = canvasPt.y;
            } else if (state->activeObject) {
                // Update shape bounds
                if (auto* arrow = dynamic_cast<ArrowObject*>(state->activeObject.get())) {
                    // Apply angle snapping if shift is held
                    if (state->shiftHeld) {
                        arrow->end = SnapToAngle(arrow->start, canvasPt, true);
                    } else {
                        arrow->end = canvasPt;
                    }
                } else if (auto* line = dynamic_cast<LineObject*>(state->activeObject.get())) {
                    // Apply angle snapping if shift is held
                    if (state->shiftHeld) {
                        line->end = SnapToAngle(line->start, canvasPt, true);
                    } else {
                        line->end = canvasPt;
                    }
                } else if (auto* shape = dynamic_cast<ShapeObject*>(state->activeObject.get())) {
                    shape->bounds.right = canvasPt.x;
                    shape->bounds.bottom = canvasPt.y;
                } else if (auto* blur = dynamic_cast<BlurRegion*>(state->activeObject.get())) {
                    blur->bounds.right = canvasPt.x;
                    blur->bounds.bottom = canvasPt.y;
                } else if (auto* callout = dynamic_cast<CalloutObject*>(state->activeObject.get())) {
                    callout->bounds.right = canvasPt.x;
                    callout->bounds.bottom = canvasPt.y;
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
                        case (int)EditorTool::Line:
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

            for (int i = 0; i < (int)EditorTool::COUNT; i++) {
                RECT btnRect = { toolX, toolY, toolX + EDITOR_TOOL_SIZE, toolY + EDITOR_TOOL_SIZE };
                if (PtInRect(&btnRect, pt)) {
                    // Finalize any active text input before switching tools
                    if (state->textInputActive) {
                        if (state->editingTextIndex >= 0) {
                            // Update existing text object
                            if (!state->textBuffer.empty()) {
                                TextObject* existingObj = dynamic_cast<TextObject*>(
                                    state->objects[state->editingTextIndex].get());
                                if (existingObj) {
                                    existingObj->text = state->textBuffer;
                                    existingObj->bounds = state->textBounds;
                                    existingObj->color = state->currentColor;
                                    existingObj->fontSize = state->currentTextSize;
                                }
                            } else {
                                state->objects.erase(state->objects.begin() + state->editingTextIndex);
                            }
                            state->unsavedChanges = true;
                        } else if (!state->textBuffer.empty()) {
                            auto textObj = std::make_unique<TextObject>();
                            textObj->bounds = state->textBounds;
                            textObj->text = state->textBuffer;
                            textObj->color = state->currentColor;
                            textObj->fontSize = state->currentTextSize;
                            state->undoManager->Execute(std::make_unique<AddObjectCommand>(state, std::move(textObj)));
                            state->unsavedChanges = true;
                        }
                    }
                    state->textInputActive = false;
                    state->textBuffer.clear();
                    state->editingTextIndex = -1;

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
                    if (PtInRect(&colorRect, pt)) {
                        state->currentColor = EDITOR_COLORS[i];
                        InvalidateRect(hwnd, &state->toolbarRect, FALSE);
                        return 0;
                    }
                }
            }

            // Custom color button
            if (PtInRect(&state->customColorRect, pt)) {
                CHOOSECOLORW cc = {};
                static COLORREF customColors[16] = { 0 };
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner = hwnd;
                cc.lpCustColors = customColors;
                cc.rgbResult = RGB(state->currentColor.GetR(), state->currentColor.GetG(), state->currentColor.GetB());
                cc.Flags = CC_FULLOPEN | CC_RGBINIT;

                if (ChooseColorW(&cc)) {
                    state->currentColor = Gdiplus::Color(255, GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
                    AddRecentColor(state, state->currentColor);
                    InvalidateRect(hwnd, &state->toolbarRect, FALSE);
                }
                return 0;
            }

            // Recent colors click
            if (PtInRect(&state->recentColorsRect, pt) && state->numRecentColors > 0) {
                int relX = pt.x - state->recentColorsRect.left;
                int colorIdx = relX / 16;
                if (colorIdx >= 0 && colorIdx < state->numRecentColors) {
                    state->currentColor = state->recentColors[colorIdx];
                    InvalidateRect(hwnd, &state->toolbarRect, FALSE);
                    return 0;
                }
            }

            // Fill toggle button
            if (PtInRect(&state->fillToggleRect, pt)) {
                state->fillShapes = !state->fillShapes;
                InvalidateRect(hwnd, &state->toolbarRect, FALSE);
                return 0;
            }

            // Bold button
            if (PtInRect(&state->boldRect, pt)) {
                state->textBold = !state->textBold;
                InvalidateRect(hwnd, &state->toolbarRect, FALSE);
                return 0;
            }

            // Italic button
            if (PtInRect(&state->italicRect, pt)) {
                state->textItalic = !state->textItalic;
                InvalidateRect(hwnd, &state->toolbarRect, FALSE);
                return 0;
            }

            // Font picker button - cycle through fonts
            if (PtInRect(&state->fontPickerRect, pt)) {
                state->currentFontIndex = (state->currentFontIndex + 1) % NUM_AVAILABLE_FONTS;
                InvalidateRect(hwnd, &state->toolbarRect, FALSE);
                return 0;
            }

            // Text background toggle button
            if (PtInRect(&state->textBgRect, pt)) {
                state->textHasBackground = !state->textHasBackground;
                InvalidateRect(hwnd, &state->toolbarRect, FALSE);
                return 0;
            }

            // Line style button - cycle through styles
            if (PtInRect(&state->lineStyleRect, pt)) {
                int style = (int)state->currentLineStyle;
                state->currentLineStyle = (LineStyle)((style + 1) % 3);
                InvalidateRect(hwnd, &state->toolbarRect, FALSE);
                return 0;
            }

            // Arrow head style button - cycle through styles
            if (PtInRect(&state->arrowStyleRect, pt)) {
                int style = (int)state->currentArrowStyle;
                state->currentArrowStyle = (ArrowHeadStyle)((style + 1) % 4);
                InvalidateRect(hwnd, &state->toolbarRect, FALSE);
                return 0;
            }

            // Double-ended arrow toggle
            if (PtInRect(&state->doubleArrowRect, pt)) {
                state->doubleEndedArrow = !state->doubleEndedArrow;
                InvalidateRect(hwnd, &state->toolbarRect, FALSE);
                return 0;
            }

            // Stamp picker button - cycle through stamps
            if (PtInRect(&state->stampPickerRect, pt)) {
                int stamp = (int)state->currentStampType;
                state->currentStampType = (StampType)((stamp + 1) % (int)StampType::COUNT);
                InvalidateRect(hwnd, &state->toolbarRect, FALSE);
                return 0;
            }

            // Rotate left button
            if (PtInRect(&state->rotateLeftRect, pt)) {
                RotateImage(state, false);  // Counter-clockwise
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            // Rotate right button
            if (PtInRect(&state->rotateRightRect, pt)) {
                RotateImage(state, true);  // Clockwise
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            // Flip horizontal button
            if (PtInRect(&state->flipHRect, pt)) {
                FlipImage(state, true);  // Horizontal
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            // Flip vertical button
            if (PtInRect(&state->flipVRect, pt)) {
                FlipImage(state, false);  // Vertical
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            // Zoom controls
            if (PtInRect(&state->zoomOutRect, pt)) {
                state->zoom = (std::max)(0.1f, state->zoom * 0.9f);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (PtInRect(&state->zoomInRect, pt)) {
                state->zoom = (std::min)(5.0f, state->zoom * 1.1f);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (PtInRect(&state->zoom100Rect, pt)) {
                state->zoom = 1.0f;
                state->panOffset = { 0, 0 };
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (PtInRect(&state->zoomFitRect, pt) && state->displayImage) {
                // Fit image to canvas
                RECT& canvas = state->canvasRect;
                int canvasW = canvas.right - canvas.left - 40;
                int canvasH = canvas.bottom - canvas.top - 40;
                int imgW = state->displayImage->GetWidth();
                int imgH = state->displayImage->GetHeight();

                float scaleX = (float)canvasW / imgW;
                float scaleY = (float)canvasH / imgH;
                state->zoom = (std::min)(scaleX, scaleY);
                state->zoom = (std::max)(0.1f, (std::min)(5.0f, state->zoom));
                state->panOffset = { 0, 0 };
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
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
        POINT canvasPt = ScreenToCanvas(state, { x, y });

        // Handle Select tool
        if (state->currentTool == EditorTool::Select) {
            // First check if clicking on a callout's tail handle
            if (state->selectedObject >= 0) {
                auto* callout = dynamic_cast<CalloutObject*>(
                    state->objects[state->selectedObject].get());
                if (callout) {
                    // Check if clicking near tail point (within 10 pixels)
                    int dx = canvasPt.x - callout->tailPoint.x;
                    int dy = canvasPt.y - callout->tailPoint.y;
                    if (dx * dx + dy * dy < 100) {  // 10px radius
                        SetCapture(hwnd);
                        state->draggingCalloutTail = true;
                        state->isDrawing = true;
                        SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
                        return 0;
                    }
                }
            }

            // Check if clicking on a handle of selected object
            if (state->selectedObject >= 0) {
                int handle = GetSelectionHandleAtPoint(state, canvasPt);
                if (handle >= -2) {  // -2=move, 0-7=resize
                    SetCapture(hwnd);
                    state->selectionHandle = handle;
                    state->selectionStart = canvasPt;
                    state->originalBounds = state->objects[state->selectedObject]->GetBounds();
                    state->isDrawing = true;
                    SetCursor(GetSelectionCursor(handle));
                    return 0;
                }
            }

            // Check if clicking on any object (iterate backwards to get topmost first)
            int clickedObj = -1;
            for (int i = (int)state->objects.size() - 1; i >= 0; i--) {
                if (state->objects[i]->HitTest(canvasPt)) {
                    clickedObj = i;
                    break;
                }
            }

            if (clickedObj >= 0) {
                state->selectedObject = clickedObj;
                // Start move immediately
                SetCapture(hwnd);
                state->selectionHandle = -2;  // Move
                state->selectionStart = canvasPt;
                state->originalBounds = state->objects[clickedObj]->GetBounds();
                state->isDrawing = true;
            } else {
                // Clicked on empty space - deselect
                state->selectedObject = -1;
            }
            InvalidateRect(hwnd, &state->canvasRect, FALSE);
            return 0;
        }

        // Clear selection when using other tools
        state->selectedObject = -1;

        SetCapture(hwnd);
        state->drawStart = canvasPt;
        state->drawEnd = canvasPt;
        state->isDrawing = true;

        // Handle text tool - draw a box first
        if (state->currentTool == EditorTool::Text) {
            if (state->textInputActive) {
                // Commit current text
                if (state->editingTextIndex >= 0) {
                    // Update existing text object
                    if (!state->textBuffer.empty()) {
                        TextObject* existingObj = dynamic_cast<TextObject*>(
                            state->objects[state->editingTextIndex].get());
                        if (existingObj) {
                            existingObj->text = state->textBuffer;
                            existingObj->bounds = state->textBounds;
                            existingObj->color = state->currentColor;
                            existingObj->fontSize = state->currentTextSize;
                        }
                    } else {
                        // Empty text - delete the object
                        state->objects.erase(state->objects.begin() + state->editingTextIndex);
                    }
                    state->unsavedChanges = true;
                } else if (!state->textBuffer.empty()) {
                    // Create new text object
                    auto textObj = std::make_unique<TextObject>();
                    textObj->bounds = state->textBounds;
                    textObj->text = state->textBuffer;
                    textObj->color = state->currentColor;
                    textObj->fontSize = state->currentTextSize;

                    state->undoManager->Execute(std::make_unique<AddObjectCommand>(state, std::move(textObj)));
                    state->unsavedChanges = true;
                }
            }
            // Start drawing a new text box
            state->textInputActive = false;
            state->textBuffer.clear();
            state->editingTextIndex = -1;
            state->textBounds = { canvasPt.x, canvasPt.y, canvasPt.x, canvasPt.y };
            // isDrawing is already true, continue to draw the text box
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
            arrow->lineStyle = state->currentLineStyle;
            arrow->headStyle = state->currentArrowStyle;
            arrow->doubleEnded = state->doubleEndedArrow;
            state->activeObject = std::move(arrow);
            break;
        }
        case EditorTool::Line: {
            auto line = std::make_unique<LineObject>();
            line->start = canvasPt;
            line->end = canvasPt;
            line->color = state->currentColor;
            line->thickness = state->currentThickness;
            line->lineStyle = state->currentLineStyle;
            state->activeObject = std::move(line);
            break;
        }
        case EditorTool::Rectangle: {
            auto shape = std::make_unique<ShapeObject>();
            shape->bounds = { canvasPt.x, canvasPt.y, canvasPt.x, canvasPt.y };
            shape->strokeColor = state->currentColor;
            shape->fillColor = state->currentColor;
            shape->thickness = state->currentThickness;
            shape->filled = state->fillShapes;
            shape->isEllipse = false;
            shape->lineStyle = state->currentLineStyle;
            state->activeObject = std::move(shape);
            break;
        }
        case EditorTool::Ellipse: {
            auto shape = std::make_unique<ShapeObject>();
            shape->bounds = { canvasPt.x, canvasPt.y, canvasPt.x, canvasPt.y };
            shape->strokeColor = state->currentColor;
            shape->fillColor = state->currentColor;
            shape->thickness = state->currentThickness;
            shape->filled = state->fillShapes;
            shape->isEllipse = true;
            shape->lineStyle = state->currentLineStyle;
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
        case EditorTool::NumberedStep: {
            // Place numbered step immediately on click
            auto step = std::make_unique<NumberedStepObject>();
            step->center = canvasPt;
            step->number = state->stepCounter++;
            step->color = state->currentColor;
            step->radius = 20;
            state->undoManager->Execute(std::make_unique<AddObjectCommand>(state, std::move(step)));
            state->unsavedChanges = true;
            state->isDrawing = false;
            InvalidateRect(hwnd, &state->canvasRect, FALSE);
            return 0;
        }
        case EditorTool::Callout: {
            // Start callout text input mode - draw box first
            state->calloutInputActive = false;
            state->calloutBuffer.clear();
            state->editingCalloutIndex = -1;
            // Create callout object with initial bounds
            auto callout = std::make_unique<CalloutObject>();
            callout->bounds = { canvasPt.x, canvasPt.y, canvasPt.x, canvasPt.y };
            callout->tailPoint = { canvasPt.x - 30, canvasPt.y + 50 };
            callout->fillColor = Gdiplus::Color(255, 255, 255, 230);
            callout->strokeColor = state->currentColor;
            callout->textColor = Gdiplus::Color(255, 0, 0, 0);
            state->activeObject = std::move(callout);
            break;
        }
        case EditorTool::Stamp: {
            // Place stamp immediately on click
            auto stamp = std::make_unique<StampObject>();
            stamp->center = canvasPt;
            stamp->stampType = state->currentStampType;
            stamp->size = state->currentStampSize;
            stamp->color = state->currentColor;
            state->undoManager->Execute(std::make_unique<AddObjectCommand>(state, std::move(stamp)));
            state->unsavedChanges = true;
            state->isDrawing = false;
            InvalidateRect(hwnd, &state->canvasRect, FALSE);
            return 0;
        }
        case EditorTool::Eraser: {
            // Find and delete object under cursor
            for (int i = (int)state->objects.size() - 1; i >= 0; i--) {
                if (state->objects[i]->HitTest(canvasPt)) {
                    state->undoManager->Execute(std::make_unique<DeleteObjectCommand>(state, i));
                    state->unsavedChanges = true;
                    state->selectedObject = -1;
                    break;
                }
            }
            state->isDrawing = false;
            InvalidateRect(hwnd, &state->canvasRect, FALSE);
            return 0;
        }
        case EditorTool::Eyedropper: {
            // Pick color from image
            Gdiplus::Color pickedColor = PickColorFromImage(state, canvasPt);
            state->currentColor = pickedColor;
            AddRecentColor(state, pickedColor);
            // Switch back to previous drawing tool (e.g., Arrow)
            state->currentTool = EditorTool::Arrow;
            state->isDrawing = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case EditorTool::Magnifier: {
            // Place magnifier immediately on click
            auto mag = std::make_unique<MagnifierObject>();
            mag->center = canvasPt;
            mag->radius = 50;
            mag->magnification = 2.0f;
            state->undoManager->Execute(std::make_unique<AddObjectCommand>(state, std::move(mag)));
            state->unsavedChanges = true;
            state->isDrawing = false;
            InvalidateRect(hwnd, &state->canvasRect, FALSE);
            return 0;
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

        // Stop callout tail dragging
        if (state->draggingCalloutTail) {
            state->draggingCalloutTail = false;
            state->isDrawing = false;
            state->unsavedChanges = true;
            InvalidateRect(hwnd, &state->canvasRect, FALSE);
            return 0;
        }

        // Finish selection move/resize
        if (state->isDrawing && state->currentTool == EditorTool::Select &&
            state->selectionHandle >= -2) {
            state->selectionHandle = -1;
            state->isDrawing = false;
            state->unsavedChanges = true;
            InvalidateRect(hwnd, &state->canvasRect, FALSE);
            return 0;
        }

        // Finish text box drawing and activate text input
        if (state->isDrawing && state->currentTool == EditorTool::Text) {
            // Normalize text box bounds
            if (state->textBounds.left > state->textBounds.right)
                std::swap(state->textBounds.left, state->textBounds.right);
            if (state->textBounds.top > state->textBounds.bottom)
                std::swap(state->textBounds.top, state->textBounds.bottom);

            // Only activate text input if box is large enough (at least 20x20 pixels)
            int boxWidth = state->textBounds.right - state->textBounds.left;
            int boxHeight = state->textBounds.bottom - state->textBounds.top;
            if (boxWidth >= 20 && boxHeight >= 20) {
                state->textInputActive = true;
                state->textBuffer.clear();
            }
            state->isDrawing = false;
            InvalidateRect(hwnd, &state->canvasRect, FALSE);
            return 0;
        }

        // Finish callout box drawing and activate callout text input
        if (state->isDrawing && state->currentTool == EditorTool::Callout && state->activeObject) {
            auto* callout = dynamic_cast<CalloutObject*>(state->activeObject.get());
            if (callout) {
                // Normalize callout bounds
                if (callout->bounds.left > callout->bounds.right)
                    std::swap(callout->bounds.left, callout->bounds.right);
                if (callout->bounds.top > callout->bounds.bottom)
                    std::swap(callout->bounds.top, callout->bounds.bottom);

                // Only activate callout input if box is large enough
                int boxWidth = callout->bounds.right - callout->bounds.left;
                int boxHeight = callout->bounds.bottom - callout->bounds.top;
                if (boxWidth >= 30 && boxHeight >= 20) {
                    // Position tail centered below the callout
                    callout->tailPoint = { (callout->bounds.left + callout->bounds.right) / 2,
                                           callout->bounds.bottom + 40 };
                    state->calloutInputActive = true;
                    state->calloutBuffer.clear();
                    state->textBounds = callout->bounds;  // Reuse textBounds for callout editing
                }
            }
            state->isDrawing = false;
            InvalidateRect(hwnd, &state->canvasRect, FALSE);
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

    case WM_LBUTTONDBLCLK: {
        if (!state) return 0;

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        // Only handle double-click on canvas
        if (y <= EDITOR_TOOLBAR_HEIGHT) return 0;

        POINT canvasPt = ScreenToCanvas(state, { x, y });

        // Check if double-clicking on a text object
        for (int i = (int)state->objects.size() - 1; i >= 0; i--) {
            TextObject* textObj = dynamic_cast<TextObject*>(state->objects[i].get());
            if (textObj && textObj->HitTest(canvasPt)) {
                // Load text object for editing
                state->textInputActive = true;
                state->textBuffer = textObj->text;
                state->textBounds = textObj->bounds;
                state->editingTextIndex = i;
                state->currentColor = textObj->color;
                state->currentTextSize = textObj->fontSize;
                state->currentTool = EditorTool::Text;
                InvalidateRect(hwnd, &state->canvasRect, FALSE);
                return 0;
            }
        }
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

            for (int i = 0; i < (int)EditorTool::COUNT; i++) {
                RECT btnRect = { toolX, toolY, toolX + EDITOR_TOOL_SIZE, toolY + EDITOR_TOOL_SIZE };
                POINT pt = { x, y };
                if (PtInRect(&btnRect, pt)) {
                    // Check if this tool has adjustable settings
                    bool hasSettings = false;
                    float minVal = 1, maxVal = 20, currentVal = 3;

                    switch ((EditorTool)i) {
                        case EditorTool::Arrow:
                        case EditorTool::Line:
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
        if (!state) return 0;

        // Handle callout text input
        if (state->calloutInputActive) {
            wchar_t ch = (wchar_t)wParam;
            if (ch == VK_BACK) {
                if (!state->calloutBuffer.empty()) {
                    state->calloutBuffer.pop_back();
                }
            } else if (ch == VK_RETURN) {
                // Ctrl+Enter or Shift+Enter to commit, plain Enter for newline
                if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000)) {
                    // Commit callout
                    if (state->activeObject) {
                        auto* callout = dynamic_cast<CalloutObject*>(state->activeObject.get());
                        if (callout) {
                            callout->text = state->calloutBuffer;
                            state->undoManager->Execute(std::make_unique<AddObjectCommand>(state, std::move(state->activeObject)));
                            state->unsavedChanges = true;
                        }
                    }
                    state->calloutInputActive = false;
                    state->calloutBuffer.clear();
                } else {
                    // Add newline for multi-line callout text
                    state->calloutBuffer += L'\n';
                }
            } else if (ch == VK_ESCAPE) {
                state->calloutInputActive = false;
                state->calloutBuffer.clear();
                state->activeObject.reset();
            } else if (ch >= 32) {
                state->calloutBuffer += ch;
            }

            // Update callout text preview
            if (state->activeObject) {
                auto* callout = dynamic_cast<CalloutObject*>(state->activeObject.get());
                if (callout) {
                    callout->text = state->calloutBuffer;
                }
            }

            InvalidateRect(hwnd, &state->canvasRect, FALSE);
            return 0;
        }

        if (!state->textInputActive) return 0;

        wchar_t ch = (wchar_t)wParam;
        if (ch == VK_BACK) {
            if (!state->textBuffer.empty()) {
                state->textBuffer.pop_back();
            }
        } else if (ch == VK_RETURN) {
            // Ctrl+Enter or Shift+Enter to commit, plain Enter for newline
            if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000)) {
                // Commit text
                if (state->editingTextIndex >= 0) {
                    // Editing existing text object
                    if (!state->textBuffer.empty()) {
                        TextObject* existingObj = dynamic_cast<TextObject*>(
                            state->objects[state->editingTextIndex].get());
                        if (existingObj) {
                            existingObj->text = state->textBuffer;
                            existingObj->bounds = state->textBounds;
                            existingObj->color = state->currentColor;
                            existingObj->fontSize = state->currentTextSize;
                            existingObj->bold = state->textBold;
                            existingObj->italic = state->textItalic;
                            existingObj->fontName = AVAILABLE_FONTS[state->currentFontIndex];
                            existingObj->hasBackground = state->textHasBackground;
                            existingObj->backgroundColor = state->textBackgroundColor;
                        }
                    } else {
                        // Empty text - delete the object
                        state->objects.erase(state->objects.begin() + state->editingTextIndex);
                    }
                    state->unsavedChanges = true;
                } else if (!state->textBuffer.empty()) {
                    // Creating new text object
                    auto textObj = std::make_unique<TextObject>();
                    textObj->bounds = state->textBounds;
                    textObj->text = state->textBuffer;
                    textObj->color = state->currentColor;
                    textObj->fontSize = state->currentTextSize;
                    textObj->bold = state->textBold;
                    textObj->italic = state->textItalic;
                    textObj->fontName = AVAILABLE_FONTS[state->currentFontIndex];
                    textObj->hasBackground = state->textHasBackground;
                    textObj->backgroundColor = state->textBackgroundColor;

                    state->undoManager->Execute(std::make_unique<AddObjectCommand>(state, std::move(textObj)));
                    state->unsavedChanges = true;
                }
                state->textInputActive = false;
                state->textBuffer.clear();
                state->editingTextIndex = -1;
            } else {
                // Add newline for multi-line text
                state->textBuffer += L'\n';
            }
        } else if (ch == VK_ESCAPE) {
            state->textInputActive = false;
            state->textBuffer.clear();
            state->editingTextIndex = -1;
        } else if (ch >= 32) {
            state->textBuffer += ch;
        }

        InvalidateRect(hwnd, &state->canvasRect, FALSE);
        return 0;
    }

    case WM_KEYDOWN: {
        if (!state) return 0;

        // Track shift key for angle snapping
        if (wParam == VK_SHIFT) {
            state->shiftHeld = true;
        }

        if (wParam == VK_ESCAPE) {
            if (state->textInputActive) {
                state->textInputActive = false;
                state->textBuffer.clear();
                state->editingTextIndex = -1;
                InvalidateRect(hwnd, &state->canvasRect, FALSE);
            } else if (state->calloutInputActive) {
                state->calloutInputActive = false;
                state->calloutBuffer.clear();
                state->activeObject.reset();
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

        // Ctrl+C - Copy selected object
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'C') {
            if (state->selectedObject >= 0 && state->selectedObject < (int)state->objects.size()) {
                state->clipboardObject.reset(state->objects[state->selectedObject]->Clone());
            }
            return 0;
        }

        // Ctrl+V - Paste
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'V') {
            if (state->clipboardObject) {
                auto newObj = std::unique_ptr<EditorObject>(state->clipboardObject->Clone());
                // Offset pasted object slightly
                newObj->Move(20, 20);
                state->undoManager->Execute(std::make_unique<AddObjectCommand>(state, std::move(newObj)));
                state->selectedObject = (int)state->objects.size() - 1;
                state->unsavedChanges = true;
                InvalidateRect(hwnd, &state->canvasRect, FALSE);
            }
            return 0;
        }

        // Ctrl+D - Duplicate selected object
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'D') {
            if (state->selectedObject >= 0 && state->selectedObject < (int)state->objects.size()) {
                auto newObj = std::unique_ptr<EditorObject>(state->objects[state->selectedObject]->Clone());
                newObj->Move(20, 20);
                state->undoManager->Execute(std::make_unique<AddObjectCommand>(state, std::move(newObj)));
                state->selectedObject = (int)state->objects.size() - 1;
                state->unsavedChanges = true;
                InvalidateRect(hwnd, &state->canvasRect, FALSE);
            }
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

        // Delete - Remove selected object
        if (wParam == VK_DELETE && state->selectedObject >= 0 &&
            state->selectedObject < (int)state->objects.size()) {
            state->objects.erase(state->objects.begin() + state->selectedObject);
            state->selectedObject = -1;
            state->unsavedChanges = true;
            InvalidateRect(hwnd, &state->canvasRect, FALSE);
            return 0;
        }

        // Escape - Deselect
        if (wParam == VK_ESCAPE && state->selectedObject >= 0) {
            state->selectedObject = -1;
            InvalidateRect(hwnd, &state->canvasRect, FALSE);
            return 0;
        }

        // Letter keys for tools (only when not typing text)
        if (!state->textInputActive && !state->calloutInputActive) {
            EditorTool newTool = state->currentTool;
            switch (wParam) {
                case 'V': newTool = EditorTool::Select; break;
                case 'A': newTool = EditorTool::Arrow; break;
                case 'L': newTool = EditorTool::Line; break;
                case 'R': newTool = EditorTool::Rectangle; break;
                case 'E': newTool = EditorTool::Ellipse; break;
                case 'P': newTool = EditorTool::Pen; break;
                case 'H': newTool = EditorTool::Highlighter; break;
                case 'T': newTool = EditorTool::Text; break;
                case 'B': newTool = EditorTool::Blur; break;
                case 'C': newTool = EditorTool::Crop; break;
                case 'N': newTool = EditorTool::NumberedStep; break;
                case 'K': newTool = EditorTool::Callout; break;
                case 'S': newTool = EditorTool::Stamp; break;
                case 'X': newTool = EditorTool::Eraser; break;
                case 'I': newTool = EditorTool::Eyedropper; break;
                case 'M': newTool = EditorTool::Magnifier; break;
            }
            if (newTool != state->currentTool) {
                state->currentTool = newTool;
                state->cropActive = (state->currentTool == EditorTool::Crop);
                state->selectedObject = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        // Arrow keys to nudge selected object
        if (state->selectedObject >= 0 && state->selectedObject < (int)state->objects.size()) {
            int dx = 0, dy = 0;
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            int nudge = shift ? 10 : 1;

            switch (wParam) {
                case VK_LEFT: dx = -nudge; break;
                case VK_RIGHT: dx = nudge; break;
                case VK_UP: dy = -nudge; break;
                case VK_DOWN: dy = nudge; break;
            }

            if (dx != 0 || dy != 0) {
                state->objects[state->selectedObject]->Move(dx, dy);
                state->unsavedChanges = true;
                InvalidateRect(hwnd, &state->canvasRect, FALSE);
                return 0;
            }
        }

        // Layer ordering shortcuts (when object selected)
        if (state->selectedObject >= 0 && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (wParam == VK_OEM_6) {  // ] - Bring forward
                if (GetKeyState(VK_SHIFT) & 0x8000)
                    BringToFront(state, state->selectedObject);
                else
                    BringForward(state, state->selectedObject);
                InvalidateRect(hwnd, &state->canvasRect, FALSE);
                return 0;
            }
            if (wParam == VK_OEM_4) {  // [ - Send backward
                if (GetKeyState(VK_SHIFT) & 0x8000)
                    SendToBack(state, state->selectedObject);
                else
                    SendBackward(state, state->selectedObject);
                InvalidateRect(hwnd, &state->canvasRect, FALSE);
                return 0;
            }
        }

        // Ctrl+Shift+C - Copy image to clipboard
        if ((GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000) && wParam == 'C') {
            CopyImageToClipboard(state);
            return 0;
        }

        // Ctrl+P - Print
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'P') {
            PrintImage(state);
            return 0;
        }

        return 0;
    }

    case WM_KEYUP: {
        if (!state) return 0;
        // Track shift key release for angle snapping
        if (wParam == VK_SHIFT) {
            state->shiftHeld = false;
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

// Image manipulation functions
void RotateImage(EditorState* state, bool clockwise) {
    if (!state->displayImage) return;

    int oldWidth = state->displayImage->GetWidth();
    int oldHeight = state->displayImage->GetHeight();

    Gdiplus::Bitmap* newBitmap = new Gdiplus::Bitmap(oldHeight, oldWidth, PixelFormat32bppARGB);
    Gdiplus::Graphics g(newBitmap);

    g.TranslateTransform((float)(clockwise ? oldHeight : 0), (float)(clockwise ? 0 : oldWidth));
    g.RotateTransform(clockwise ? 90.0f : -90.0f);
    g.DrawImage(state->displayImage, 0, 0);

    delete state->displayImage;
    state->displayImage = newBitmap;
    state->unsavedChanges = true;
}

void FlipImage(EditorState* state, bool horizontal) {
    if (!state->displayImage) return;

    int width = state->displayImage->GetWidth();
    int height = state->displayImage->GetHeight();

    Gdiplus::Bitmap* newBitmap = new Gdiplus::Bitmap(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics g(newBitmap);

    if (horizontal) {
        g.ScaleTransform(-1.0f, 1.0f);
        g.TranslateTransform(-(float)width, 0);
    } else {
        g.ScaleTransform(1.0f, -1.0f);
        g.TranslateTransform(0, -(float)height);
    }
    g.DrawImage(state->displayImage, 0, 0);

    delete state->displayImage;
    state->displayImage = newBitmap;
    state->unsavedChanges = true;
}

void ResizeImage(EditorState* state, int newWidth, int newHeight) {
    if (!state->displayImage || newWidth <= 0 || newHeight <= 0) return;

    Gdiplus::Bitmap* newBitmap = new Gdiplus::Bitmap(newWidth, newHeight, PixelFormat32bppARGB);
    Gdiplus::Graphics g(newBitmap);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.DrawImage(state->displayImage, 0, 0, newWidth, newHeight);

    delete state->displayImage;
    state->displayImage = newBitmap;
    state->unsavedChanges = true;
}

void ApplyBrightnessContrast(EditorState* state) {
    if (!state->displayImage) return;

    int width = state->displayImage->GetWidth();
    int height = state->displayImage->GetHeight();

    Gdiplus::BitmapData data;
    Gdiplus::Rect rect(0, 0, width, height);
    state->displayImage->LockBits(&rect, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
                                   PixelFormat32bppARGB, &data);

    float brightness = state->brightness / 100.0f * 255.0f;
    float contrast = (100.0f + state->contrast) / 100.0f;

    BYTE* pixels = (BYTE*)data.Scan0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * data.Stride + x * 4;
            for (int c = 0; c < 3; c++) {  // B, G, R (skip A)
                float val = pixels[idx + c];
                val = ((val - 128) * contrast) + 128 + brightness;
                pixels[idx + c] = (BYTE)(std::max)(0.0f, (std::min)(255.0f, val));
            }
        }
    }

    state->displayImage->UnlockBits(&data);
    state->unsavedChanges = true;
}

void AddBorder(EditorState* state, int width, Gdiplus::Color color) {
    if (!state->displayImage || width <= 0) return;

    int oldWidth = state->displayImage->GetWidth();
    int oldHeight = state->displayImage->GetHeight();
    int newWidth = oldWidth + width * 2;
    int newHeight = oldHeight + width * 2;

    Gdiplus::Bitmap* newBitmap = new Gdiplus::Bitmap(newWidth, newHeight, PixelFormat32bppARGB);
    Gdiplus::Graphics g(newBitmap);

    // Fill with border color
    Gdiplus::SolidBrush brush(color);
    g.FillRectangle(&brush, 0, 0, newWidth, newHeight);

    // Draw original image centered
    g.DrawImage(state->displayImage, width, width);

    delete state->displayImage;
    state->displayImage = newBitmap;
    state->unsavedChanges = true;
}

// Color helpers
void AddRecentColor(EditorState* state, Gdiplus::Color color) {
    // Check if already in recent colors
    for (int i = 0; i < state->numRecentColors; i++) {
        if (state->recentColors[i].GetValue() == color.GetValue()) {
            // Move to front
            for (int j = i; j > 0; j--) {
                state->recentColors[j] = state->recentColors[j - 1];
            }
            state->recentColors[0] = color;
            return;
        }
    }

    // Add to front, shift others
    for (int i = (std::min)(state->numRecentColors, MAX_RECENT_COLORS - 1); i > 0; i--) {
        state->recentColors[i] = state->recentColors[i - 1];
    }
    state->recentColors[0] = color;
    if (state->numRecentColors < MAX_RECENT_COLORS) {
        state->numRecentColors++;
    }
}

Gdiplus::Color PickColorFromImage(EditorState* state, POINT canvasPt) {
    if (!state->displayImage) return Gdiplus::Color(255, 0, 0, 0);

    int x = canvasPt.x;
    int y = canvasPt.y;

    if (x < 0 || x >= (int)state->displayImage->GetWidth() ||
        y < 0 || y >= (int)state->displayImage->GetHeight()) {
        return Gdiplus::Color(255, 0, 0, 0);
    }

    Gdiplus::Color color;
    state->displayImage->GetPixel(x, y, &color);
    return color;
}

// Layer ordering
void BringToFront(EditorState* state, int objectIndex) {
    if (objectIndex < 0 || objectIndex >= (int)state->objects.size() - 1) return;

    auto obj = std::move(state->objects[objectIndex]);
    state->objects.erase(state->objects.begin() + objectIndex);
    state->objects.push_back(std::move(obj));
    state->selectedObject = (int)state->objects.size() - 1;
}

void SendToBack(EditorState* state, int objectIndex) {
    if (objectIndex <= 0 || objectIndex >= (int)state->objects.size()) return;

    auto obj = std::move(state->objects[objectIndex]);
    state->objects.erase(state->objects.begin() + objectIndex);
    state->objects.insert(state->objects.begin(), std::move(obj));
    state->selectedObject = 0;
}

void BringForward(EditorState* state, int objectIndex) {
    if (objectIndex < 0 || objectIndex >= (int)state->objects.size() - 1) return;

    std::swap(state->objects[objectIndex], state->objects[objectIndex + 1]);
    state->selectedObject = objectIndex + 1;
}

void SendBackward(EditorState* state, int objectIndex) {
    if (objectIndex <= 0 || objectIndex >= (int)state->objects.size()) return;

    std::swap(state->objects[objectIndex], state->objects[objectIndex - 1]);
    state->selectedObject = objectIndex - 1;
}

// Clipboard - copy image to system clipboard
void CopyImageToClipboard(EditorState* state) {
    if (!state->displayImage) return;

    // Render objects to image first
    RenderObjectsToImage(state);

    int width = state->displayImage->GetWidth();
    int height = state->displayImage->GetHeight();

    // Create DIB
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;  // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);

    if (hBitmap && bits) {
        Gdiplus::Bitmap bmp(width, height, width * 4, PixelFormat32bppARGB, (BYTE*)bits);
        Gdiplus::Graphics g(&bmp);
        g.DrawImage(state->displayImage, 0, 0);

        if (OpenClipboard(state->hwnd)) {
            EmptyClipboard();
            SetClipboardData(CF_BITMAP, hBitmap);
            CloseClipboard();
        }
    }

    ReleaseDC(nullptr, hdc);
}

// Print support
void PrintImage(EditorState* state) {
    if (!state->displayImage) return;

    // Render objects to image first
    RenderObjectsToImage(state);

    PRINTDLGW pd = {};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = state->hwnd;
    pd.Flags = PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE;

    if (PrintDlgW(&pd)) {
        DOCINFOW di = {};
        di.cbSize = sizeof(di);
        di.lpszDocName = L"Screenshot";

        if (StartDocW(pd.hDC, &di) > 0) {
            StartPage(pd.hDC);

            int printerWidth = GetDeviceCaps(pd.hDC, HORZRES);
            int printerHeight = GetDeviceCaps(pd.hDC, VERTRES);

            int imgWidth = state->displayImage->GetWidth();
            int imgHeight = state->displayImage->GetHeight();

            // Scale to fit printer page
            float scale = (std::min)((float)printerWidth / imgWidth, (float)printerHeight / imgHeight);
            int destWidth = (int)(imgWidth * scale);
            int destHeight = (int)(imgHeight * scale);
            int destX = (printerWidth - destWidth) / 2;
            int destY = (printerHeight - destHeight) / 2;

            Gdiplus::Graphics g(pd.hDC);
            g.DrawImage(state->displayImage, destX, destY, destWidth, destHeight);

            EndPage(pd.hDC);
            EndDoc(pd.hDC);
        }
        DeleteDC(pd.hDC);
    }
}

// Angle snapping helper - snaps to 0, 45, 90, 135, 180, etc.
POINT SnapToAngle(POINT start, POINT end, bool snapEnabled) {
    if (!snapEnabled) return end;

    float dx = (float)(end.x - start.x);
    float dy = (float)(end.y - start.y);
    float length = sqrtf(dx * dx + dy * dy);

    if (length < 5) return end;  // Too short to snap

    float angle = atan2f(dy, dx);
    const float PI = 3.14159265f;

    // Snap to nearest 45 degrees
    float snapAngle = roundf(angle / (PI / 4)) * (PI / 4);

    POINT snapped;
    snapped.x = start.x + (LONG)(length * cosf(snapAngle));
    snapped.y = start.y + (LONG)(length * sinf(snapAngle));
    return snapped;
}
