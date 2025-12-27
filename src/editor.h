#pragma once

#include "common.h"
#include <vector>
#include <memory>
#include <string>

// Editor tool types
enum class EditorTool {
    Select = 0,
    Arrow,
    Line,
    Rectangle,
    Ellipse,
    Pen,
    Highlighter,
    Text,
    Blur,
    Crop,
    COUNT  // Number of tools
};

// Base class for drawable objects
struct EditorObject {
    virtual ~EditorObject() = default;
    virtual void Draw(Gdiplus::Graphics& g, float zoom, POINT offset) = 0;
    virtual RECT GetBounds() const = 0;
    virtual EditorObject* Clone() const = 0;
    virtual bool HitTest(POINT pt) const;  // Returns true if point is on object
    virtual void Move(int dx, int dy) = 0;  // Move object by delta
    virtual void SetBounds(const RECT& newBounds) = 0;  // Resize to new bounds
};

// Arrow annotation
struct ArrowObject : EditorObject {
    POINT start = {};
    POINT end = {};
    Gdiplus::Color color;
    float thickness = 3.0f;
    int headSize = 15;

    void Draw(Gdiplus::Graphics& g, float zoom, POINT offset) override;
    RECT GetBounds() const override;
    EditorObject* Clone() const override { return new ArrowObject(*this); }
    bool HitTest(POINT pt) const override;
    void Move(int dx, int dy) override;
    void SetBounds(const RECT& newBounds) override;
};

// Line annotation (no arrowhead)
struct LineObject : EditorObject {
    POINT start = {};
    POINT end = {};
    Gdiplus::Color color;
    float thickness = 3.0f;

    void Draw(Gdiplus::Graphics& g, float zoom, POINT offset) override;
    RECT GetBounds() const override;
    EditorObject* Clone() const override { return new LineObject(*this); }
    bool HitTest(POINT pt) const override;
    void Move(int dx, int dy) override;
    void SetBounds(const RECT& newBounds) override;
};

// Shape annotation (rectangle or ellipse)
struct ShapeObject : EditorObject {
    RECT bounds = {};
    Gdiplus::Color strokeColor;
    Gdiplus::Color fillColor;
    float thickness = 2.0f;
    bool filled = false;
    bool isEllipse = false;

    void Draw(Gdiplus::Graphics& g, float zoom, POINT offset) override;
    RECT GetBounds() const override { return bounds; }
    EditorObject* Clone() const override { return new ShapeObject(*this); }
    bool HitTest(POINT pt) const override;
    void Move(int dx, int dy) override;
    void SetBounds(const RECT& newBounds) override;
};

// Freehand path (pen/highlighter)
struct PathObject : EditorObject {
    std::vector<POINT> points;
    Gdiplus::Color color;
    float thickness = 3.0f;
    bool isHighlighter = false;

    void Draw(Gdiplus::Graphics& g, float zoom, POINT offset) override;
    RECT GetBounds() const override;
    EditorObject* Clone() const override { return new PathObject(*this); }
    bool HitTest(POINT pt) const override;
    void Move(int dx, int dy) override;
    void SetBounds(const RECT& newBounds) override;
};

// Text annotation
struct TextObject : EditorObject {
    RECT bounds = {};  // Text box bounds
    std::wstring text;
    Gdiplus::Color color;
    std::wstring fontName = L"Segoe UI";
    float fontSize = 16.0f;
    bool bold = false;

    void Draw(Gdiplus::Graphics& g, float zoom, POINT offset) override;
    RECT GetBounds() const override { return bounds; }
    EditorObject* Clone() const override { return new TextObject(*this); }
    bool HitTest(POINT pt) const override;
    void Move(int dx, int dy) override;
    void SetBounds(const RECT& newBounds) override;
};

// Blur region
struct BlurRegion : EditorObject {
    RECT bounds = {};
    int blockSize = 12;

    void Draw(Gdiplus::Graphics& g, float zoom, POINT offset) override;
    RECT GetBounds() const override { return bounds; }
    EditorObject* Clone() const override { return new BlurRegion(*this); }
    bool HitTest(POINT pt) const override;
    void Move(int dx, int dy) override;
    void SetBounds(const RECT& newBounds) override;
};

// Forward declaration
class UndoManager;

// Editor window state
struct EditorState {
    HWND hwnd = nullptr;
    std::wstring filepath;
    Gdiplus::Bitmap* originalImage = nullptr;
    Gdiplus::Bitmap* displayImage = nullptr;

    // Annotations
    std::vector<std::unique_ptr<EditorObject>> objects;

    // Current tool state
    EditorTool currentTool = EditorTool::Arrow;
    Gdiplus::Color currentColor = Gdiplus::Color(255, 255, 0, 0);  // Red
    float currentThickness = 3.0f;
    int currentBlurSize = 12;
    float currentTextSize = 24.0f;
    bool fillShapes = false;  // Fill rectangles/ellipses
    BYTE currentOpacity = 255;  // 0-255 opacity

    // Clipboard for copy/paste
    std::unique_ptr<EditorObject> clipboardObject;

    // Drawing state
    bool isDrawing = false;
    POINT drawStart = {};
    POINT drawEnd = {};
    std::unique_ptr<EditorObject> activeObject;

    // Text input state
    bool textInputActive = false;
    std::wstring textBuffer;
    RECT textBounds = {};  // Text box being edited
    int editingTextIndex = -1;  // Index of text object being edited (-1 = new)

    // Crop state
    bool cropActive = false;
    RECT cropRect = {};
    int cropHandle = -1;  // Which handle is being dragged

    // View state
    float zoom = 1.0f;
    POINT panOffset = {};
    bool isPanning = false;
    POINT panStart = {};

    // Selection
    int selectedObject = -1;
    int selectionHandle = -1;  // -1=none, -2=move, 0-7=resize handles
    POINT selectionStart = {};  // For tracking drag start
    RECT originalBounds = {};   // Original bounds before resize/move

    // UI state
    int hoveredTool = -1;
    int hoveredColorIndex = -1;
    bool unsavedChanges = false;

    // Slider popup state
    bool sliderVisible = false;
    RECT sliderRect = {};
    int sliderToolIndex = -1;  // Which tool's slider is shown
    float sliderValue = 0;
    float sliderMin = 1;
    float sliderMax = 20;
    bool sliderDragging = false;

    // Undo/Redo
    UndoManager* undoManager = nullptr;

    // Layout rectangles
    RECT toolbarRect = {};
    RECT canvasRect = {};
    RECT statusBarRect = {};
    RECT colorPickerRect = {};
    RECT fillToggleRect = {};
    RECT customColorRect = {};
    RECT undoRect = {};
    RECT redoRect = {};
    RECT saveRect = {};
    RECT zoomFitRect = {};
    RECT zoom100Rect = {};
    RECT zoomInRect = {};
    RECT zoomOutRect = {};
};

// Predefined colors for picker
const Gdiplus::Color EDITOR_COLORS[] = {
    Gdiplus::Color(255, 255, 0, 0),      // Red
    Gdiplus::Color(255, 255, 165, 0),    // Orange
    Gdiplus::Color(255, 255, 255, 0),    // Yellow
    Gdiplus::Color(255, 0, 255, 0),      // Green
    Gdiplus::Color(255, 0, 191, 255),    // Deep Sky Blue
    Gdiplus::Color(255, 0, 0, 255),      // Blue
    Gdiplus::Color(255, 128, 0, 255),    // Purple
    Gdiplus::Color(255, 255, 255, 255),  // White
    Gdiplus::Color(255, 0, 0, 0),        // Black
};
const int NUM_EDITOR_COLORS = 9;

// Tool icons (Unicode symbols)
const wchar_t* const TOOL_ICONS[] = {
    L"\u2316",  // Select (crosshair)
    L"\u2794",  // Arrow
    L"\u2571",  // Line (diagonal)
    L"\u25AD",  // Rectangle
    L"\u25CB",  // Ellipse
    L"\u270F",  // Pen
    L"\u2591",  // Highlighter
    L"T",       // Text
    L"\u2592",  // Blur
    L"\u2702",  // Crop (scissors)
};

// Tool names for tooltips
const wchar_t* const TOOL_NAMES[] = {
    L"Select (1)",
    L"Arrow (2)",
    L"Line (3)",
    L"Rectangle (4)",
    L"Ellipse (5)",
    L"Pen (6)",
    L"Highlighter (7)",
    L"Text (8)",
    L"Blur (9)",
    L"Crop (0)",
};

// Status bar height
const int EDITOR_STATUS_HEIGHT = 24;

// Editor functions
HWND OpenEditor(const std::wstring& filepath);
void CloseEditor(HWND hwnd);
void CloseAllEditors();
EditorState* GetEditorState(HWND hwnd);
LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Drawing helpers
void DrawEditorToolbar(HDC hdc, EditorState* state);
void DrawEditorCanvas(HDC hdc, EditorState* state);
void RenderObjectsToImage(EditorState* state);
bool SaveEditorImage(EditorState* state, const wchar_t* filepath);

// Tool helpers
void ApplyBlurToRegion(Gdiplus::Bitmap* bitmap, const RECT& region, int blockSize);
POINT ScreenToCanvas(EditorState* state, POINT screenPt);
POINT CanvasToScreen(EditorState* state, POINT canvasPt);

// Selection helpers
int GetSelectionHandleAtPoint(EditorState* state, POINT canvasPt);
void DrawSelectionHandles(Gdiplus::Graphics& g, const RECT& bounds, float zoom, POINT offset);
HCURSOR GetSelectionCursor(int handle);

// Open image in editor (called from gallery)
void OpenImageInEditor(const std::wstring& filepath);
