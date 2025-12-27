#include "capture.h"
#include "app_state.h"

bool InitializeD3D() {
    HRESULT hr;

    ComPtr<IDXGIFactory1> factory;
    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIAdapter1> adapter;
    hr = factory->EnumAdapters1(0, &adapter);
    if (FAILED(hr)) return false;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(
        adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
        featureLevels, 1, D3D11_SDK_VERSION,
        &g_app.device, &featureLevel, &g_app.context
    );
    if (FAILED(hr)) return false;

    ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(0, &output);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr)) return false;

    DXGI_OUTPUT_DESC outputDesc;
    output->GetDesc(&outputDesc);
    g_app.screenWidth = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    g_app.screenHeight = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

    hr = output1->DuplicateOutput(g_app.device.Get(), &g_app.duplication);
    if (FAILED(hr)) return false;

    return true;
}

bool CaptureScreen() {
    if (!g_app.duplication) return false;

    HRESULT hr;
    ComPtr<IDXGIResource> desktopResource;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;

    g_app.duplication->ReleaseFrame();

    for (int i = 0; i < 10; i++) {
        hr = g_app.duplication->AcquireNextFrame(100, &frameInfo, &desktopResource);
        if (SUCCEEDED(hr)) break;
        if (hr != DXGI_ERROR_WAIT_TIMEOUT) return false;
    }
    if (FAILED(hr)) return false;

    ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);
    if (FAILED(hr)) {
        g_app.duplication->ReleaseFrame();
        return false;
    }

    D3D11_TEXTURE2D_DESC desc;
    desktopTexture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;

    g_app.capturedTexture.Reset();
    hr = g_app.device->CreateTexture2D(&desc, nullptr, &g_app.capturedTexture);
    if (FAILED(hr)) {
        g_app.duplication->ReleaseFrame();
        return false;
    }

    g_app.context->CopyResource(g_app.capturedTexture.Get(), desktopTexture.Get());
    g_app.duplication->ReleaseFrame();

    return true;
}

bool SaveScreenshot(const RECT& region, const wchar_t* filename) {
    if (!g_app.capturedTexture) return false;

    HRESULT hr;
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = g_app.context->Map(g_app.capturedTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    int x = region.left;
    int y = region.top;
    int width = region.right - region.left;
    int height = region.bottom - region.top;

    x = std::max(0, std::min(x, (int)g_app.screenWidth - 1));
    y = std::max(0, std::min(y, (int)g_app.screenHeight - 1));
    width = std::min(width, (int)g_app.screenWidth - x);
    height = std::min(height, (int)g_app.screenHeight - y);

    if (width <= 0 || height <= 0) {
        g_app.context->Unmap(g_app.capturedTexture.Get(), 0);
        return false;
    }

    ComPtr<IWICImagingFactory> wicFactory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr)) {
        g_app.context->Unmap(g_app.capturedTexture.Get(), 0);
        return false;
    }

    ComPtr<IWICBitmap> bitmap;
    hr = wicFactory->CreateBitmapFromMemory(
        g_app.screenWidth, g_app.screenHeight,
        GUID_WICPixelFormat32bppBGRA,
        mapped.RowPitch, mapped.RowPitch * g_app.screenHeight,
        (BYTE*)mapped.pData, &bitmap
    );
    g_app.context->Unmap(g_app.capturedTexture.Get(), 0);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapClipper> clipper;
    wicFactory->CreateBitmapClipper(&clipper);
    WICRect wicRect = { x, y, width, height };
    clipper->Initialize(bitmap.Get(), &wicRect);

    ComPtr<IWICStream> stream;
    wicFactory->CreateStream(&stream);
    stream->InitializeFromFilename(filename, GENERIC_WRITE);

    ComPtr<IWICBitmapEncoder> encoder;
    wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);

    ComPtr<IWICBitmapFrameEncode> frame;
    encoder->CreateNewFrame(&frame, nullptr);
    frame->Initialize(nullptr);
    frame->SetSize(width, height);

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&format);
    frame->WriteSource(clipper.Get(), nullptr);
    frame->Commit();
    encoder->Commit();

    return true;
}

HBITMAP CaptureScreenToBitmap() {
    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);

    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);

    HBITMAP bitmap = CreateCompatibleBitmap(screenDC, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, bitmap);

    BitBlt(memDC, 0, 0, width, height, screenDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);

    return bitmap;
}

std::wstring GenerateAutoFilename() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &time);

    wchar_t filename[256];
    swprintf_s(filename, L"Screenshot_%04d%02d%02d_%02d%02d%02d.png",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);

    if (!g_app.settings.savePath.empty()) {
        return g_app.settings.savePath + L"\\" + filename;
    }
    return filename;
}

bool ShowSaveDialog(wchar_t* filepath, int maxLen) {
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_app.mainWnd;
    ofn.lpstrFilter = L"PNG Image\0*.png\0All Files\0*.*\0";
    ofn.lpstrFile = filepath;
    ofn.nMaxFile = maxLen;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"png";

    if (!g_app.settings.savePath.empty()) {
        ofn.lpstrInitialDir = g_app.settings.savePath.c_str();
    }

    return GetSaveFileNameW(&ofn) != 0;
}
