#include "D2D1Renderer.h"
#include <iostream>

namespace {
    HWND g_hVideoWnd = NULL;
    ID2D1Factory* g_pFactory = nullptr;
    ID2D1HwndRenderTarget* g_pRenderTarget = nullptr;
    ID2D1Bitmap* g_pBitmap = nullptr;

    int g_renderW = 0;
    int g_renderH = 0;
    int g_bmpWidth = 0;
    int g_bmpHeight = 0;

    CRITICAL_SECTION g_d2dCs;
    bool g_csInitialized = false;
    bool g_isReady = false;

    void RecreateResourcesLocked() {
        if (g_pBitmap) {
            g_pBitmap->Release();
            g_pBitmap = nullptr;
        }
        if (g_pRenderTarget) {
            g_pRenderTarget->Release();
            g_pRenderTarget = nullptr;
        }
        g_bmpWidth = 0;
        g_bmpHeight = 0;

        if (!g_pFactory || !g_hVideoWnd) return;

        RECT rc = {};
        GetClientRect(g_hVideoWnd, &rc);
        g_renderW = rc.right - rc.left;
        g_renderH = rc.bottom - rc.top;
        if (g_renderW <= 0) g_renderW = 1;
        if (g_renderH <= 0) g_renderH = 1;

        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0f, // Cố định DPI 96 (1 DIP = 1 Pixel thực) để triệt tiêu lỗi co giãn phóng to khi Windows bật 125%/150% DPI
            96.0f
        );

        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
            g_hVideoWnd,
            D2D1::SizeU(g_renderW, g_renderH),
            D2D1_PRESENT_OPTIONS_IMMEDIATELY // Hiển thị 0ms không chờ VSync để triệt tiêu độ trễ
        );

        HRESULT hr = g_pFactory->CreateHwndRenderTarget(rtProps, hwndProps, &g_pRenderTarget);
        if (SUCCEEDED(hr) && g_pRenderTarget) {
            g_pRenderTarget->SetDpi(96.0f, 96.0f);
        } else {
            g_pRenderTarget = nullptr;
        }
    }
}

namespace D2D1Renderer {

    bool Initialize(HWND hVideoWnd) {
        if (!g_csInitialized) {
            InitializeCriticalSection(&g_d2dCs);
            g_csInitialized = true;
        }

        EnterCriticalSection(&g_d2dCs);
        g_hVideoWnd = hVideoWnd;

        if (!g_pFactory) {
            HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, &g_pFactory);
            if (FAILED(hr)) {
                g_pFactory = nullptr;
                LeaveCriticalSection(&g_d2dCs);
                return false;
            }
        }

        RecreateResourcesLocked();
        g_isReady = (g_pRenderTarget != nullptr);

        LeaveCriticalSection(&g_d2dCs);
        return g_isReady;
    }

    void Cleanup() {
        if (!g_csInitialized) return;

        EnterCriticalSection(&g_d2dCs);
        g_isReady = false;

        if (g_pBitmap) {
            g_pBitmap->Release();
            g_pBitmap = nullptr;
        }
        if (g_pRenderTarget) {
            g_pRenderTarget->Release();
            g_pRenderTarget = nullptr;
        }
        if (g_pFactory) {
            g_pFactory->Release();
            g_pFactory = nullptr;
        }

        g_bmpWidth = 0;
        g_bmpHeight = 0;
        g_hVideoWnd = NULL;

        LeaveCriticalSection(&g_d2dCs);
        DeleteCriticalSection(&g_d2dCs);
        g_csInitialized = false;
    }

    void Resize(int width, int height) {
        if (!g_csInitialized) return;

        EnterCriticalSection(&g_d2dCs);
        g_renderW = width;
        g_renderH = height;

        if (g_pRenderTarget) {
            D2D1_SIZE_U size = D2D1::SizeU(width > 0 ? width : 1, height > 0 ? height : 1);
            g_pRenderTarget->Resize(size);
            g_pRenderTarget->SetDpi(96.0f, 96.0f);
        }
        LeaveCriticalSection(&g_d2dCs);
    }

    bool UpdateDirtyRect(int deltaX, int deltaY, int deltaW, int deltaH, const uint32_t* pixelData, int frameW, int frameH) {
        if (!g_csInitialized || !pixelData || frameW <= 0 || frameH <= 0) return false;

        EnterCriticalSection(&g_d2dCs);

        if (!g_pRenderTarget) {
            RecreateResourcesLocked();
            if (!g_pRenderTarget) {
                LeaveCriticalSection(&g_d2dCs);
                return false;
            }
        }

        // Tái tạo Bitmap nếu độ phân giải khung hình thay đổi hoặc khởi tạo lần đầu
        if (!g_pBitmap || g_bmpWidth != frameW || g_bmpHeight != frameH) {
            if (g_pBitmap) {
                g_pBitmap->Release();
                g_pBitmap = nullptr;
            }

            D2D1_BITMAP_PROPERTIES props;
            props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
            props.dpiX = 96.0f;
            props.dpiY = 96.0f;

            HRESULT hr = g_pRenderTarget->CreateBitmap(D2D1::SizeU(frameW, frameH), props, &g_pBitmap);
            if (FAILED(hr) || !g_pBitmap) {
                LeaveCriticalSection(&g_d2dCs);
                return false;
            }

            g_bmpWidth = frameW;
            g_bmpHeight = frameH;

            // Nạp toàn bộ khung hình ban đầu vào VRAM GPU
            D2D1_RECT_U fullRect = D2D1::RectU(0, 0, frameW, frameH);
            g_pBitmap->CopyFromMemory(&fullRect, pixelData, frameW * sizeof(uint32_t));
            LeaveCriticalSection(&g_d2dCs);
            return true;
        }

        // Đẩy vùng thay đổi (Dirty Rect) trực tiếp vào texture trong VRAM GPU (<0.1ms)
        if (deltaW > 0 && deltaH > 0 && deltaX >= 0 && deltaY >= 0 &&
            deltaX + deltaW <= frameW && deltaY + deltaH <= frameH) {
            D2D1_RECT_U d2dRect = D2D1::RectU(deltaX, deltaY, deltaX + deltaW, deltaY + deltaH);
            const void* srcPtr = (const void*)(pixelData + (deltaY * frameW) + deltaX);
            g_pBitmap->CopyFromMemory(&d2dRect, srcPtr, frameW * sizeof(uint32_t));
        }

        LeaveCriticalSection(&g_d2dCs);
        return true;
    }

    void GetViewport(float& outX, float& outY, float& outW, float& outH) {
        if (!g_csInitialized) {
            outX = 0; outY = 0; outW = 0; outH = 0;
            return;
        }

        EnterCriticalSection(&g_d2dCs);
        if (g_hVideoWnd) {
            RECT rc = {};
            GetClientRect(g_hVideoWnd, &rc);
            int curW = rc.right - rc.left;
            int curH = rc.bottom - rc.top;
            if (curW > 0 && curH > 0) {
                g_renderW = curW;
                g_renderH = curH;
            }
        }

        if (g_bmpWidth <= 0 || g_bmpHeight <= 0 || g_renderW <= 0 || g_renderH <= 0) {
            outX = 0.0f;
            outY = 0.0f;
            outW = (float)g_renderW;
            outH = (float)g_renderH;
            LeaveCriticalSection(&g_d2dCs);
            return;
        }

        float scaleX = (float)g_renderW / (float)g_bmpWidth;
        float scaleY = (float)g_renderH / (float)g_bmpHeight;
        float scale = (std::min)(scaleX, scaleY);

        outW = (float)g_bmpWidth * scale;
        outH = (float)g_bmpHeight * scale;
        outX = ((float)g_renderW - outW) / 2.0f;
        outY = ((float)g_renderH - outH) / 2.0f;
        LeaveCriticalSection(&g_d2dCs);
    }

    void Render() {
        if (!g_csInitialized) return;

        EnterCriticalSection(&g_d2dCs);

        // Luôn đồng bộ kích thước khung vẽ với ClientRect thực tế của cửa sổ
        if (g_hVideoWnd) {
            RECT rc = {};
            GetClientRect(g_hVideoWnd, &rc);
            int curW = rc.right - rc.left;
            int curH = rc.bottom - rc.top;
            if (curW > 0 && curH > 0 && (curW != g_renderW || curH != g_renderH)) {
                g_renderW = curW;
                g_renderH = curH;
                if (g_pRenderTarget) {
                    g_pRenderTarget->Resize(D2D1::SizeU(curW, curH));
                    g_pRenderTarget->SetDpi(96.0f, 96.0f);
                }
            }
        }

        if (!g_pRenderTarget || !g_pBitmap || g_renderW <= 0 || g_renderH <= 0 || g_bmpWidth <= 0 || g_bmpHeight <= 0) {
            LeaveCriticalSection(&g_d2dCs);
            return;
        }

        // Tính toán giữ đúng tỷ lệ khung hình gốc (Aspect-Ratio Preserved Letterboxing/Pillarboxing)
        float scaleX = (float)g_renderW / (float)g_bmpWidth;
        float scaleY = (float)g_renderH / (float)g_bmpHeight;
        float scale = (std::min)(scaleX, scaleY);

        float drawW = (float)g_bmpWidth * scale;
        float drawH = (float)g_bmpHeight * scale;
        float offsetX = ((float)g_renderW - drawW) / 2.0f;
        float offsetY = ((float)g_renderH - drawH) / 2.0f;

        D2D1_RECT_F dstRect = D2D1::RectF(offsetX, offsetY, offsetX + drawW, offsetY + drawH);
        D2D1_RECT_F srcRect = D2D1::RectF(0.0f, 0.0f, (float)g_bmpWidth, (float)g_bmpHeight);

        g_pRenderTarget->BeginDraw();

        // Xóa viền ngoài bằng màu nền tối đồng bộ
        g_pRenderTarget->Clear(D2D1::ColorF(0x121317));

        g_pRenderTarget->DrawBitmap(
            g_pBitmap,
            &dstRect,
            1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, // GPU Bilinear Filtering sắc nét, mượt mà
            &srcRect
        );

        HRESULT hr = g_pRenderTarget->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            RecreateResourcesLocked();
        }

        LeaveCriticalSection(&g_d2dCs);
    }

    bool IsInitialized() {
        return g_isReady;
    }
}
