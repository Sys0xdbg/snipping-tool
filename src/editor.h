#pragma once

#include "common.h"
#include <vector>
#include <memory>
#include <string>
#include <array>

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
    NumberedStep,
    Callout,
    Stamp,
    Eraser,
    Eyedropper,
    Magnifier,
    COUNT  // Number of tools
};

// Line styles
enum class LineStyle {
    Solid = 0,
    Dashed,
    Dotted
};

// Arrow head styles
enum class ArrowHeadStyle {
    Filled = 0,
    Open,
    Diamond,
    None
};

// Stamp types
enum class StampType {
    Checkmark = 0,
    Cross,
    Star,
    Question,
    Exclamation,
    Heart,
    COUNT
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
    LineStyle lineStyle = LineStyle::Solid;
    ArrowHeadStyle headStyle = ArrowHeadStyle::Filled;
    bool doubleEnded = false;

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
    LineStyle lineStyle = LineStyle::Solid;

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
    LineStyle lineStyle = LineStyle::Solid;

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
    Gdiplus::Color backgroundColor = Gdiplus::Color(0, 255, 255, 255);  // Transparent by default
    std::wstring fontName = L"Segoe UI";
    float fontSize = 16.0f;
    bool bold = false;
    bool italic = false;
    bool hasBackground = false;

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

// Numbered step annotation (circled number)
struct NumberedStepObject : EditorObject {
    POINT center = {};
    int number = 1;
    int radius = 20;
    Gdiplus::Color color;
    Gdiplus::Color textColor = Gdiplus::Color(255, 255, 255, 255);  // White text

    void Draw(Gdiplus::Graphics& g, float zoom, POINT offset) override;
    RECT GetBounds() const override;
    EditorObject* Clone() const override { return new NumberedStepObject(*this); }
    bool HitTest(POINT pt) const override;
    void Move(int dx, int dy) override;
    void SetBounds(const RECT& newBounds) override;
};

// Callout/Speech bubble
struct CalloutObject : EditorObject {
    RECT bounds = {};
    POINT tailPoint = {};  // Where the tail points to
    std::wstring text;
    Gdiplus::Color fillColor = Gdiplus::Color(255, 255, 255, 200);
    Gdiplus::Color strokeColor = Gdiplus::Color(255, 0, 0, 0);
    Gdiplus::Color textColor = Gdiplus::Color(255, 0, 0, 0);
    float fontSize = 14.0f;

    void Draw(Gdiplus::Graphics& g, float zoom, POINT offset) override;
    RECT GetBounds() const override;
    EditorObject* Clone() const override { return new CalloutObject(*this); }
    bool HitTest(POINT pt) const override;
    void Move(int dx, int dy) override;
    void SetBounds(const RECT& newBounds) override;
};

// Stamp annotation
struct StampObject : EditorObject {
    POINT center = {};
    StampType stampType = StampType::Checkmark;
    int size = 32;
    Gdiplus::Color color;

    void Draw(Gdiplus::Graphics& g, float zoom, POINT offset) override;
    RECT GetBounds() const override;
    EditorObject* Clone() const override { return new StampObject(*this); }
    bool HitTest(POINT pt) const override;
    void Move(int dx, int dy) override;
    void SetBounds(const RECT& newBounds) override;
};

// Magnifier region
struct MagnifierObject : EditorObject {
    POINT center = {};
    int radius = 50;
    float magnification = 2.0f;

    void Draw(Gdiplus::Graphics& g, float zoom, POINT offset) override;
    RECT GetBounds() const override;
    EditorObject* Clone() const override { return new MagnifierObject(*this); }
    bool HitTest(POINT pt) const override;
    void Move(int dx, int dy) override;
    void SetBounds(const RECT& newBounds) override;
};

// Forward declaration
class UndoManager;

// Max recent colors
const int MAX_RECENT_COLORS = 6;

// Available fonts
const wchar_t* const AVAILABLE_FONTS[] = {
    L"Segoe UI",
    L"Arial",
    L"Times New Roman",
    L"Courier New",
    L"Comic Sans MS",
    L"Impact",
    L"Georgia",
    L"Verdana",
};
const int NUM_AVAILABLE_FONTS = 8;

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

    // Line and arrow styles
    LineStyle currentLineStyle = LineStyle::Solid;
    ArrowHeadStyle currentArrowStyle = ArrowHeadStyle::Filled;
    bool doubleEndedArrow = false;

    // Text options
    bool textBold = false;
    bool textItalic = false;
    bool textHasBackground = false;
    Gdiplus::Color textBackgroundColor = Gdiplus::Color(255, 255, 255, 200);
    int currentFontIndex = 0;

    // Stamp options
    StampType currentStampType = StampType::Checkmark;
    int currentStampSize = 32;

    // Step counter for numbered steps
    int stepCounter = 1;

    // Recent colors
    std::array<Gdiplus::Color, MAX_RECENT_COLORS> recentColors;
    int numRecentColors = 0;

    // Image adjustments
    int brightness = 0;  // -100 to 100
    int contrast = 0;    // -100 to 100

    // Border settings
    bool hasBorder = false;
    int borderWidth = 5;
    Gdiplus::Color borderColor = Gdiplus::Color(255, 0, 0, 0);

    // Clipboard for copy/paste
    std::unique_ptr<EditorObject> clipboardObject;

    // Drawing state
    bool isDrawing = false;
    POINT drawStart = {};
    POINT drawEnd = {};
    std::unique_ptr<EditorObject> activeObject;
    bool shiftHeld = false;  // For angle snapping

    // Text input state
    bool textInputActive = false;
    std::wstring textBuffer;
    RECT textBounds = {};  // Text box being edited
    int editingTextIndex = -1;  // Index of text object being edited (-1 = new)

    // Callout input state
    bool calloutInputActive = false;
    std::wstring calloutBuffer;
    int editingCalloutIndex = -1;

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
    bool draggingCalloutTail = false;  // Dragging a callout's tail point

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

    // Dropdown state (for font picker, stamp picker, etc.)
    bool dropdownVisible = false;
    RECT dropdownRect = {};
    int dropdownType = 0;  // 0=font, 1=stamp, 2=arrow style, 3=line style

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
    RECT recentColorsRect = {};
    RECT boldRect = {};
    RECT italicRect = {};
    RECT fontPickerRect = {};
    RECT textBgRect = {};
    RECT lineStyleRect = {};
    RECT arrowStyleRect = {};
    RECT doubleArrowRect = {};
    RECT rotateLeftRect = {};
    RECT rotateRightRect = {};
    RECT flipHRect = {};
    RECT flipVRect = {};
    RECT brightnessRect = {};
    RECT contrastRect = {};
    RECT borderRect = {};
    RECT printRect = {};
    RECT stampPickerRect = {};
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
    L"\u2780",  // Numbered step (circled 1)
    L"\u25ED",  // Callout (speech bubble)
    L"\u2605",  // Stamp (star)
    L"\u2421",  // Eraser
    L"\u25C9",  // Eyedropper
    L"\u2315",  // Magnifier
};

// Tool names for tooltips
const wchar_t* const TOOL_NAMES[] = {
    L"Select (V)",
    L"Arrow (A)",
    L"Line (L)",
    L"Rectangle (R)",
    L"Ellipse (E)",
    L"Pen (P)",
    L"Highlighter (H)",
    L"Text (T)",
    L"Blur (B)",
    L"Crop (C)",
    L"Numbered Step (N)",
    L"Callout (K)",
    L"Stamp (S)",
    L"Eraser (X)",
    L"Eyedropper (I)",
    L"Magnifier (M)",
};

// Stamp icons
const wchar_t* const STAMP_ICONS[] = {
    L"\u2713",  // Checkmark
    L"\u2717",  // Cross/X
    L"\u2605",  // Star
    L"?",       // Question
    L"!",       // Exclamation
    L"\u2665",  // Heart
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
bool SaveEditorImageEx(EditorState* state, const wchar_t* filepath, int format, int quality);

// Tool helpers
void ApplyBlurToRegion(Gdiplus::Bitmap* bitmap, const RECT& region, int blockSize);
POINT ScreenToCanvas(EditorState* state, POINT screenPt);
POINT CanvasToScreen(EditorState* state, POINT canvasPt);

// Selection helpers
int GetSelectionHandleAtPoint(EditorState* state, POINT canvasPt);
void DrawSelectionHandles(Gdiplus::Graphics& g, const RECT& bounds, float zoom, POINT offset);
HCURSOR GetSelectionCursor(int handle);

// Image manipulation
void RotateImage(EditorState* state, bool clockwise);
void FlipImage(EditorState* state, bool horizontal);
void ResizeImage(EditorState* state, int newWidth, int newHeight);
void ApplyBrightnessContrast(EditorState* state);
void AddBorder(EditorState* state, int width, Gdiplus::Color color);

// Color helpers
void AddRecentColor(EditorState* state, Gdiplus::Color color);
Gdiplus::Color PickColorFromImage(EditorState* state, POINT canvasPt);

// Layer ordering
void BringToFront(EditorState* state, int objectIndex);
void SendToBack(EditorState* state, int objectIndex);
void BringForward(EditorState* state, int objectIndex);
void SendBackward(EditorState* state, int objectIndex);

// Clipboard
void CopyImageToClipboard(EditorState* state);

// Print
void PrintImage(EditorState* state);

// Open image in editor (called from gallery)
void OpenImageInEditor(const std::wstring& filepath);

// Angle snapping helper
POINT SnapToAngle(POINT start, POINT end, bool snapEnabled);
