#include "undo_manager.h"
#include "editor.h"

// AddObjectCommand implementation
AddObjectCommand::AddObjectCommand(EditorState* s, std::unique_ptr<EditorObject> obj)
    : state(s), object(std::move(obj)) {}

void AddObjectCommand::Execute() {
    if (object && state) {
        state->objects.push_back(std::move(object));
        executed = true;
    }
}

void AddObjectCommand::Undo() {
    if (state && executed && !state->objects.empty()) {
        object = std::move(state->objects.back());
        state->objects.pop_back();
        executed = false;
    }
}

// DeleteObjectCommand implementation
DeleteObjectCommand::DeleteObjectCommand(EditorState* s, size_t idx)
    : state(s), index(idx) {}

void DeleteObjectCommand::Execute() {
    if (state && index < state->objects.size()) {
        object = std::move(state->objects[index]);
        state->objects.erase(state->objects.begin() + index);
    }
}

void DeleteObjectCommand::Undo() {
    if (state && object) {
        if (index <= state->objects.size()) {
            state->objects.insert(state->objects.begin() + index, std::move(object));
        }
    }
}

// ApplyBlurCommand implementation
ApplyBlurCommand::ApplyBlurCommand(EditorState* s, const RECT& r, int bs)
    : state(s), region(r), blockSize(bs) {}

void ApplyBlurCommand::SaveOriginalPixels() {
    if (!state || !state->displayImage) return;

    int imgWidth = state->displayImage->GetWidth();
    int imgHeight = state->displayImage->GetHeight();

    int x1 = (std::max)(0, (int)region.left);
    int y1 = (std::max)(0, (int)region.top);
    int x2 = (std::min)(imgWidth, (int)region.right);
    int y2 = (std::min)(imgHeight, (int)region.bottom);

    width = x2 - x1;
    height = y2 - y1;

    if (width <= 0 || height <= 0) return;

    Gdiplus::Rect lockRect(x1, y1, width, height);
    Gdiplus::BitmapData data;

    if (state->displayImage->LockBits(&lockRect, Gdiplus::ImageLockModeRead,
        PixelFormat32bppARGB, &data) == Gdiplus::Ok) {

        originalPixels.resize(width * height * 4);
        BYTE* src = (BYTE*)data.Scan0;

        for (int y = 0; y < height; y++) {
            memcpy(originalPixels.data() + y * width * 4, src + y * data.Stride, width * 4);
        }

        state->displayImage->UnlockBits(&data);
        hasOriginal = true;
    }
}

void ApplyBlurCommand::RestoreOriginalPixels() {
    if (!state || !state->displayImage || !hasOriginal) return;

    int imgWidth = state->displayImage->GetWidth();
    int imgHeight = state->displayImage->GetHeight();

    int x1 = (std::max)(0, (int)region.left);
    int y1 = (std::max)(0, (int)region.top);

    Gdiplus::Rect lockRect(x1, y1, width, height);
    Gdiplus::BitmapData data;

    if (state->displayImage->LockBits(&lockRect,
        Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) == Gdiplus::Ok) {

        BYTE* dst = (BYTE*)data.Scan0;

        for (int y = 0; y < height; y++) {
            memcpy(dst + y * data.Stride, originalPixels.data() + y * width * 4, width * 4);
        }

        state->displayImage->UnlockBits(&data);
    }
}

void ApplyBlurCommand::Execute() {
    SaveOriginalPixels();
    ApplyBlurToRegion(state->displayImage, region, blockSize);
}

void ApplyBlurCommand::Undo() {
    RestoreOriginalPixels();
}

// CropCommand implementation
CropCommand::CropCommand(EditorState* s, const RECT& r)
    : state(s), cropRect(r) {}

CropCommand::~CropCommand() {
    delete previousImage;
}

void CropCommand::Execute() {
    if (!state || !state->displayImage) return;

    // Save previous state
    previousImage = state->displayImage->Clone(
        0, 0, state->displayImage->GetWidth(), state->displayImage->GetHeight(),
        PixelFormat32bppARGB
    );

    // Save objects (they'll need position adjustment on undo)
    for (auto& obj : state->objects) {
        previousObjects.push_back(std::unique_ptr<EditorObject>(obj->Clone()));
    }

    // Calculate crop dimensions
    int x = (std::max)(0, (int)cropRect.left);
    int y = (std::max)(0, (int)cropRect.top);
    int w = (std::min)((int)state->displayImage->GetWidth() - x, (int)(cropRect.right - cropRect.left));
    int h = (std::min)((int)state->displayImage->GetHeight() - y, (int)(cropRect.bottom - cropRect.top));

    if (w <= 0 || h <= 0) return;

    // Create cropped image
    Gdiplus::Bitmap* cropped = state->displayImage->Clone(x, y, w, h, PixelFormat32bppARGB);

    delete state->displayImage;
    state->displayImage = cropped;

    // Adjust object positions
    for (auto& obj : state->objects) {
        RECT bounds = obj->GetBounds();
        // Offset adjustment would go here based on crop position
    }

    // Clear objects that are now outside bounds
    // ... simplified for now
}

void CropCommand::Undo() {
    if (!state || !previousImage) return;

    delete state->displayImage;
    state->displayImage = previousImage->Clone(
        0, 0, previousImage->GetWidth(), previousImage->GetHeight(),
        PixelFormat32bppARGB
    );

    // Restore objects
    state->objects.clear();
    for (auto& obj : previousObjects) {
        state->objects.push_back(std::unique_ptr<EditorObject>(obj->Clone()));
    }
}

// UndoManager implementation
void UndoManager::Execute(std::unique_ptr<EditorCommand> cmd) {
    cmd->Execute();
    undoStack.push_back(std::move(cmd));
    redoStack.clear();

    // Limit stack size
    while (undoStack.size() > MAX_UNDO) {
        undoStack.erase(undoStack.begin());
    }
}

void UndoManager::Undo() {
    if (undoStack.empty()) return;

    auto cmd = std::move(undoStack.back());
    undoStack.pop_back();

    cmd->Undo();
    redoStack.push_back(std::move(cmd));
}

void UndoManager::Redo() {
    if (redoStack.empty()) return;

    auto cmd = std::move(redoStack.back());
    redoStack.pop_back();

    cmd->Execute();
    undoStack.push_back(std::move(cmd));
}

void UndoManager::Clear() {
    undoStack.clear();
    redoStack.clear();
}
