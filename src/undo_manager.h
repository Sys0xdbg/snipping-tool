#pragma once

#include "common.h"
#include <vector>
#include <memory>

// Forward declarations
struct EditorState;
struct EditorObject;

// Base command class
class EditorCommand {
public:
    virtual ~EditorCommand() = default;
    virtual void Execute() = 0;
    virtual void Undo() = 0;
};

// Add object command
class AddObjectCommand : public EditorCommand {
    EditorState* state;
    std::unique_ptr<EditorObject> object;
    bool executed = false;
public:
    AddObjectCommand(EditorState* s, std::unique_ptr<EditorObject> obj);
    void Execute() override;
    void Undo() override;
};

// Delete object command
class DeleteObjectCommand : public EditorCommand {
    EditorState* state;
    std::unique_ptr<EditorObject> object;
    size_t index;
public:
    DeleteObjectCommand(EditorState* s, size_t idx);
    void Execute() override;
    void Undo() override;
};

// Apply blur command (stores original pixels)
class ApplyBlurCommand : public EditorCommand {
    EditorState* state;
    RECT region;
    int blockSize;
    std::vector<BYTE> originalPixels;
    int width, height;
    bool hasOriginal = false;
public:
    ApplyBlurCommand(EditorState* s, const RECT& r, int bs);
    void Execute() override;
    void Undo() override;
private:
    void SaveOriginalPixels();
    void RestoreOriginalPixels();
};

// Crop command
class CropCommand : public EditorCommand {
    EditorState* state;
    RECT cropRect;
    Gdiplus::Bitmap* previousImage = nullptr;
    std::vector<std::unique_ptr<EditorObject>> previousObjects;
public:
    CropCommand(EditorState* s, const RECT& r);
    ~CropCommand();
    void Execute() override;
    void Undo() override;
};

// Undo manager
class UndoManager {
    std::vector<std::unique_ptr<EditorCommand>> undoStack;
    std::vector<std::unique_ptr<EditorCommand>> redoStack;
    static const int MAX_UNDO = 50;
public:
    void Execute(std::unique_ptr<EditorCommand> cmd);
    void Undo();
    void Redo();
    bool CanUndo() const { return !undoStack.empty(); }
    bool CanRedo() const { return !redoStack.empty(); }
    void Clear();
};
