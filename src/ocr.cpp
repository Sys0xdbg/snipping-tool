#include "ocr.h"
#include "app_state.h"
#include "notification.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Security.Cryptography.h>

#pragma comment(lib, "windowsapp.lib")

bool PerformOCR(const RECT& region) {
    if (!g_app.capturedTexture) {
        ShowTextNotification(L"OCR Failed", L"No captured image available", true);
        return false;
    }

    int x = region.left;
    int y = region.top;
    int width = region.right - region.left;
    int height = region.bottom - region.top;

    // Clamp to screen bounds
    x = std::max(0, std::min(x, (int)g_app.screenWidth - 1));
    y = std::max(0, std::min(y, (int)g_app.screenHeight - 1));
    width = std::min(width, (int)g_app.screenWidth - x);
    height = std::min(height, (int)g_app.screenHeight - y);

    if (width <= 0 || height <= 0) {
        ShowTextNotification(L"OCR Failed", L"Invalid selection size", true);
        return false;
    }

    // Copy pixel data from captured texture
    std::vector<uint8_t> pixelData(width * height * 4);

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = g_app.context->Map(g_app.capturedTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        ShowTextNotification(L"OCR Failed", L"Failed to map captured texture", true);
        return false;
    }

    uint8_t* srcData = (uint8_t*)mapped.pData;
    int srcPitch = mapped.RowPitch;

    for (int row = 0; row < height; row++) {
        uint8_t* srcRow = srcData + (y + row) * srcPitch + x * 4;
        uint8_t* destRow = pixelData.data() + row * width * 4;
        memcpy(destRow, srcRow, width * 4);
    }

    g_app.context->Unmap(g_app.capturedTexture.Get(), 0);

    // Now perform OCR with copied data
    try {
        // Initialize WinRT apartment (may already be initialized)
        try {
            winrt::init_apartment();
        } catch (...) {
            // Already initialized, continue
        }

        // Create IBuffer from pixel data
        auto buffer = winrt::Windows::Security::Cryptography::CryptographicBuffer::CreateFromByteArray(
            winrt::array_view<uint8_t const>(pixelData.data(), pixelData.data() + pixelData.size()));

        // Create SoftwareBitmap from buffer
        auto bitmap = winrt::Windows::Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(
            buffer,
            winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,
            width, height,
            winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Premultiplied);

        // Create OCR engine
        winrt::Windows::Media::Ocr::OcrEngine ocrEngine{ nullptr };
        ocrEngine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();

        if (!ocrEngine) {
            // Fallback to English
            auto language = winrt::Windows::Globalization::Language(L"en-US");
            ocrEngine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(language);
        }

        if (!ocrEngine) {
            ShowTextNotification(L"OCR Failed", L"OCR engine not available", true);
            return false;
        }

        // Perform OCR
        auto result = ocrEngine.RecognizeAsync(bitmap).get();

        std::wstring extractedText;
        for (auto const& line : result.Lines()) {
            if (!extractedText.empty()) {
                extractedText += L"\r\n";
            }
            extractedText += line.Text().c_str();
        }

        if (extractedText.empty()) {
            ShowTextNotification(L"OCR", L"No text found in selection", true);
            return false;
        }

        // Copy to clipboard
        if (OpenClipboard(g_app.mainWnd)) {
            EmptyClipboard();

            size_t size = (extractedText.length() + 1) * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
            if (hMem) {
                wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
                if (pMem) {
                    wcscpy_s(pMem, extractedText.length() + 1, extractedText.c_str());
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
            }
            CloseClipboard();
            ShowTextNotification(L"Text Copied", L"Text has been copied to clipboard", false);
        }

        return true;
    }
    catch (winrt::hresult_error const&) {
        ShowTextNotification(L"OCR Failed", L"Could not recognize text", true);
        return false;
    }
    catch (...) {
        ShowTextNotification(L"OCR Failed", L"Unknown error occurred", true);
        return false;
    }
}
