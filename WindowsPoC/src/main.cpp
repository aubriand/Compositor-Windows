#include <windows.h>
#include <shellapi.h>
#include <wincodec.h>
#include <d2d1.h>
#include <vector>
#include <string>
#include <array>
#include <algorithm>
#include <cmath>

#include "DocumentModel.h"

extern "C" {
#include "LevelsPixels.h"
#include "NoisePixels.h"
}

template <class T>
void safeRelease(T*& value) {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

struct AppState {
    ID2D1Factory* d2dFactory = nullptr;
    ID2D1HwndRenderTarget* renderTarget = nullptr;
    ID2D1Bitmap* bitmap = nullptr;
    IWICImagingFactory* wicFactory = nullptr;
    RasterImage composited;
    UINT width = 0;
    UINT height = 0;
    std::wstring status = L"Drop a PNG/JPEG/BMP/TIFF file on this window";
};

static AppState g;

bool ensureRenderTarget(HWND hwnd) {
    if (g.renderTarget) return true;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(std::max<LONG>(1, rc.right - rc.left), std::max<LONG>(1, rc.bottom - rc.top));
    HRESULT hr = g.d2dFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd, size),
        &g.renderTarget
    );
    return SUCCEEDED(hr);
}

void rebuildBitmap() {
    if (!g.renderTarget || g.composited.empty() || !g.width || !g.height) return;
    safeRelease(g.bitmap);
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    g.renderTarget->CreateBitmap(
        D2D1::SizeU(g.width, g.height),
        g.composited.pixels.data(),
        g.width * 4,
        &props,
        &g.bitmap
    );
}

RasterImage loadRasterWithWIC(const wchar_t* path) {
    RasterImage image;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    UINT width = 0;
    UINT height = 0;

    HRESULT hr = g.wicFactory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) goto cleanup;

    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) goto cleanup;

    hr = frame->GetSize(&width, &height);
    if (FAILED(hr) || !width || !height) goto cleanup;

    hr = g.wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr)) goto cleanup;

    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) goto cleanup;

    image = RasterImage(width, height);
    hr = converter->CopyPixels(nullptr, width * 4,
        static_cast<UINT>(image.pixels.size()), image.pixels.data());
    if (FAILED(hr)) image = RasterImage{};

cleanup:
    safeRelease(converter);
    safeRelease(frame);
    safeRelease(decoder);
    return image;
}

bool loadImage(const wchar_t* path) {
    safeRelease(g.bitmap);
    g.composited = RasterImage{};
    g.width = g.height = 0;

    RasterImage source = loadRasterWithWIC(path);
    if (source.empty()) {
        g.status = L"Unable to decode this image with Windows Imaging Component";
        return false;
    }

    // Existing Compositor operation #1: Levels. Identity tables deliberately
    // exercise the original implementation without changing the base pixels.
    std::array<float, 256 * 3> tables{};
    for (int channel = 0; channel < 3; ++channel) {
        for (int i = 0; i < 256; ++i) {
            tables[channel * 256 + i] = static_cast<float>(i) / 255.0f;
        }
    }
    levels_apply(source.pixels.data(), static_cast<size_t>(source.width) * source.height, tables.data());

    const uint32_t canvasWidth = source.width + source.width / 4;
    const uint32_t canvasHeight = source.height + source.height / 4;

    // Build a subset of the real CanvasDocument/ImageLayer model: bottom-to-top
    // layers, a pass-through folder, parent visibility/opacity, and transforms.
    PortableDocument document;
    document.width = canvasWidth;
    document.height = canvasHeight;
    document.resolution = 72;

    DocumentLayer background;
    background.id = "background";
    background.name = "Background";
    background.image = source;
    background.transform = {0, 0, static_cast<double>(source.width), static_cast<double>(source.height)};

    DocumentLayer folder;
    folder.id = "folder";
    folder.name = "Folder 1";
    folder.isGroup = true;
    folder.opacity = 0.75;
    folder.transform = {0, 0, static_cast<double>(canvasWidth), static_cast<double>(canvasHeight)};

    DocumentLayer overlay;
    overlay.id = "overlay";
    overlay.name = "Noise overlay";
    overlay.image = source;
    overlay.parentID = folder.id;
    overlay.opacity = 0.60;
    overlay.transform = {
        static_cast<double>(source.width / 8),
        static_cast<double>(source.height / 8),
        static_cast<double>(source.width),
        static_cast<double>(source.height)
    };

    // Existing Compositor operation #2: Add Noise, applied to one selected layer
    // before the document is flattened. This validates that pixel operations can
    // target portable layer-owned rasters instead of CGImage.
    noise_add(
        overlay.image.pixels.data(), overlay.image.width, overlay.image.height,
        static_cast<size_t>(overlay.image.width) * 4, 18.0f, 0, 1, 0xC0A5E17u
    );

    document.layers = {std::move(background), std::move(folder), std::move(overlay)};
    g.composited = renderDocument(document);
    if (g.composited.empty()) {
        g.status = L"Portable document validation failed";
        return false;
    }

    g.width = g.composited.width;
    g.height = g.composited.height;
    g.status = L"CanvasDocument subset + folder opacity + Levels/Noise C core rendered with Direct2D";
    rebuildBitmap();
    return true;
}

void paint(HWND hwnd) {
    PAINTSTRUCT ps{};
    BeginPaint(hwnd, &ps);
    if (!ensureRenderTarget(hwnd)) {
        EndPaint(hwnd, &ps);
        return;
    }

    g.renderTarget->BeginDraw();
    g.renderTarget->Clear(D2D1::ColorF(0x202124));

    if (g.bitmap) {
        D2D1_SIZE_F rt = g.renderTarget->GetSize();
        const float sx = rt.width / static_cast<float>(g.width);
        const float sy = rt.height / static_cast<float>(g.height);
        const float scale = std::min(sx, sy);
        const float w = g.width * scale;
        const float h = g.height * scale;
        D2D1_RECT_F dest = D2D1::RectF((rt.width - w) * 0.5f, (rt.height - h) * 0.5f,
                                       (rt.width + w) * 0.5f, (rt.height + h) * 0.5f);
        g.renderTarget->DrawBitmap(g.bitmap, dest, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }

    HRESULT hr = g.renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        safeRelease(g.bitmap);
        safeRelease(g.renderTarget);
    }
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        DragAcceptFiles(hwnd, TRUE);
        return 0;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        wchar_t path[MAX_PATH]{};
        if (DragQueryFileW(drop, 0, path, MAX_PATH)) {
            loadImage(path);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        DragFinish(drop);
        return 0;
    }
    case WM_SIZE:
        if (g.renderTarget) {
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            g.renderTarget->Resize(D2D1::SizeU(std::max(1u, width), std::max(1u, height)));
            rebuildBitmap();
        }
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_PAINT:
        paint(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g.d2dFactory))) return 2;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&g.wicFactory)))) return 3;

    const wchar_t* className = L"CompositorWindowsPoC";
    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, className, L"Compositor Windows - Portability PoC",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 760,
        nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 4;

    ShowWindow(hwnd, show);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    safeRelease(g.bitmap);
    safeRelease(g.renderTarget);
    safeRelease(g.wicFactory);
    safeRelease(g.d2dFactory);
    CoUninitialize();
    return 0;
}
