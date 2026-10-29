#include "ScreenCapture.h"
#include <turbojpeg.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstring>
#include <vector>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <atomic>

namespace {
    bool g_isInitialized = false;
    bool g_useDxgi = false;

    // TurboJPEG shared handle
    tjhandle g_tjCompressor = nullptr;
    unsigned char* g_jpegBuf = nullptr;
    unsigned long g_jpegBufSize = 0;

    // ==========================================
    // ĐO TỐC ĐỘ MẠNG THỜI GIAN THỰC & PROGRESSIVE REFINEMENT
    // ==========================================
    std::atomic<double> g_avgSendTimeMs(2.0); // Giả định ban đầu mạng rất tốt (<4ms)
    bool g_needsRefinement = false;
    std::chrono::steady_clock::time_point g_lastMotionTime = std::chrono::steady_clock::now();

    struct CompressionConfig {
        int quality;
        int subsamp;
    };

    // Tự động nâng sàn chất lượng nếu mạng mạnh và chọn subsampling 4:4:4 cho chi tiết nhỏ
    CompressionConfig GetCompressionConfig(int changedArea, int totalPixels) {
        double sendMs = g_avgSendTimeMs.load(std::memory_order_relaxed);
        bool fastNetwork = (sendMs < 4.0);
        bool slowNetwork = (sendMs > 12.0);

        double ratio = (double)changedArea / (double)(totalPixels > 0 ? totalPixels : 1);

        if (ratio >= 0.50) {
            // Thay đổi diện rộng (cuộn trang nhanh, chuyển ứng dụng, video)
            int q = slowNetwork ? 50 : (fastNetwork ? 75 : 65);
            return { q, TJSAMP_420 };
        } else if (ratio >= 0.20) {
            // Thay đổi trung bình (kéo cửa sổ, cuộn văn bản)
            int q = slowNetwork ? 65 : (fastNetwork ? 82 : 74);
            return { q, TJSAMP_420 };
        } else if (ratio >= 0.04) {
            // Thay đổi nhỏ (gõ văn bản, mở menu xổ xuống)
            int q = slowNetwork ? 78 : (fastNetwork ? 88 : 84);
            return { q, TJSAMP_420 };
        } else {
            // Thay đổi rất nhỏ (con trỏ chuột, 1 dòng chữ, icon)
            // Dùng TJSAMP_444 để chữ ký tự nhỏ không bị lem màu viền
            int q = slowNetwork ? 85 : 94;
            int subsamp = slowNetwork ? TJSAMP_420 : TJSAMP_444;
            return { q, subsamp };
        }
    }

    // Nén khung hoàn thiện toàn màn hình (Progressive Refinement) sắc nét tuyệt đối 94% với TJSAMP_444
    bool EncodeRefinementFrame(const uint32_t* pixels, int width, int height,
                               std::vector<char>& outBuffer,
                               int& outX, int& outY, int& outWidth, int& outHeight) {
        if (!pixels || width <= 0 || height <= 0 || !g_tjCompressor) return false;

        outX = 0;
        outY = 0;
        outWidth = width;
        outHeight = height;

        int compressRes = tjCompress2(
            g_tjCompressor,
            (const unsigned char*)pixels,
            width,
            width * sizeof(uint32_t),
            height,
            TJPF_BGRX,
            &g_jpegBuf,
            &g_jpegBufSize,
            TJSAMP_444, // Full chroma không giảm mẫu màu để chữ và đường nét cực sắc
            94,         // Chất lượng đỉnh cao 94%
            TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE
        );

        if (compressRes != 0 || !g_jpegBuf || g_jpegBufSize == 0) {
            return false;
        }

        Protocol::VideoDeltaHeader delta = { 0, 0, width, height };
        outBuffer.resize(sizeof(delta) + g_jpegBufSize);
        std::memcpy(outBuffer.data(), &delta, sizeof(delta));
        std::memcpy(outBuffer.data() + sizeof(delta), g_jpegBuf, g_jpegBufSize);
        return true;
    }

    // ==========================================
    // 1. DIRECTX 11 DESKTOP DUPLICATION (GPU)
    // ==========================================
    ID3D11Device* g_d3dDevice = nullptr;
    ID3D11DeviceContext* g_d3dContext = nullptr;
    IDXGIOutputDuplication* g_dxgiDupl = nullptr;
    ID3D11Texture2D* g_stagingTexture = nullptr;
    int g_dxgiWidth = 0;
    int g_dxgiHeight = 0;
    std::vector<uint32_t> g_dxgiPrevPixels;

    void CleanupDxgi() {
        g_dxgiPrevPixels.clear();
        if (g_stagingTexture) { g_stagingTexture->Release(); g_stagingTexture = nullptr; }
        if (g_dxgiDupl) { g_dxgiDupl->Release(); g_dxgiDupl = nullptr; }
        if (g_d3dContext) { g_d3dContext->Release(); g_d3dContext = nullptr; }
        if (g_d3dDevice) { g_d3dDevice->Release(); g_d3dDevice = nullptr; }
    }

    bool InitDxgi() {
        CleanupDxgi();

        // Gán thread vào Input Desktop hiện tại để đảm bảo quyền truy cập màn hình
        HDESK hInputDesk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
        if (!hInputDesk) hInputDesk = OpenDesktopA("Default", 0, FALSE, GENERIC_ALL);
        if (hInputDesk) {
            SetThreadDesktop(hInputDesk);
            CloseDesktop(hInputDesk);
        }

        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL featLevel;

        // 1. Tạo Direct3D 11 Hardware Device
        HRESULT hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &g_d3dDevice,
            &featLevel,
            &g_d3dContext
        );

        if (FAILED(hr)) {
            // Thử WARP driver nếu Hardware Driver không khởi tạo được
            hr = D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                0,
                featureLevels,
                ARRAYSIZE(featureLevels),
                D3D11_SDK_VERSION,
                &g_d3dDevice,
                &featLevel,
                &g_d3dContext
            );
            if (FAILED(hr)) return false;
        }

        // 2. Lấy DXGI Output 1 (Màn hình chính)
        IDXGIDevice* dxgiDevice = nullptr;
        hr = g_d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
        if (FAILED(hr) || !dxgiDevice) return false;

        IDXGIAdapter* dxgiAdapter = nullptr;
        hr = dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&dxgiAdapter);
        dxgiDevice->Release();
        if (FAILED(hr) || !dxgiAdapter) return false;

        IDXGIOutput* dxgiOutput = nullptr;
        hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput);
        dxgiAdapter->Release();
        if (FAILED(hr) || !dxgiOutput) return false;

        DXGI_OUTPUT_DESC outputDesc;
        dxgiOutput->GetDesc(&outputDesc);
        g_dxgiWidth = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
        g_dxgiHeight = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

        IDXGIOutput1* dxgiOutput1 = nullptr;
        hr = dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&dxgiOutput1);
        dxgiOutput->Release();
        if (FAILED(hr) || !dxgiOutput1) return false;

        // 3. Khởi tạo Desktop Duplication
        hr = dxgiOutput1->DuplicateOutput(g_d3dDevice, &g_dxgiDupl);
        dxgiOutput1->Release();
        if (FAILED(hr) || !g_dxgiDupl) return false;

        // 4. Tạo Staging Texture (CPU readable)
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = g_dxgiWidth;
        desc.Height = g_dxgiHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        hr = g_d3dDevice->CreateTexture2D(&desc, nullptr, &g_stagingTexture);
        if (FAILED(hr) || !g_stagingTexture) {
            CleanupDxgi();
            return false;
        }

        g_dxgiPrevPixels.clear();
        return true;
    }

    bool CaptureDxgiFrame(std::vector<char>& outBuffer, int& outX, int& outY, int& outWidth, int& outHeight) {
        if (!g_dxgiDupl || !g_stagingTexture || !g_tjCompressor) return false;

        DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
        IDXGIResource* desktopResource = nullptr;

        // Timeout 4ms: Nếu màn hình tĩnh thì kiểm tra Progressive Refinement
        HRESULT hr = g_dxgiDupl->AcquireNextFrame(4, &frameInfo, &desktopResource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            if (g_needsRefinement && !g_dxgiPrevPixels.empty()) {
                auto now = std::chrono::steady_clock::now();
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastMotionTime).count();
                if (elapsedMs >= 150) {
                    g_needsRefinement = false;
                    return EncodeRefinementFrame(g_dxgiPrevPixels.data(), g_dxgiWidth, g_dxgiHeight, outBuffer, outX, outY, outWidth, outHeight);
                }
            }
            return false;
        }

        if (hr == DXGI_ERROR_ACCESS_LOST) {
            // Khi UAC xuất hiện hoặc thay đổi độ phân giải màn hình
            InitDxgi();
            return false;
        }

        if (FAILED(hr) || !desktopResource) {
            return false;
        }

        ID3D11Texture2D* desktopTexture = nullptr;
        hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&desktopTexture);
        desktopResource->Release();

        if (FAILED(hr) || !desktopTexture) {
            g_dxgiDupl->ReleaseFrame();
            return false;
        }

        // Copy từ VRAM sang Staging Texture (GPU copy cực nhanh < 0.5ms)
        g_d3dContext->CopyResource(g_stagingTexture, desktopTexture);
        desktopTexture->Release();
        g_dxgiDupl->ReleaseFrame();

        // Map bộ đệm sang CPU để nén
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        hr = g_d3dContext->Map(g_stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) return false;

        int pixelsCount = g_dxgiWidth * g_dxgiHeight;
        int minX = g_dxgiWidth, minY = g_dxgiHeight, maxX = -1, maxY = -1;
        const uint32_t* curPixels = (const uint32_t*)mapped.pData;
        int rowPitchElements = mapped.RowPitch / sizeof(uint32_t);

        if (g_dxgiPrevPixels.empty()) {
            g_dxgiPrevPixels.resize(pixelsCount);
            for (int y = 0; y < g_dxgiHeight; ++y) {
                std::memcpy(&g_dxgiPrevPixels[y * g_dxgiWidth],
                            &curPixels[y * rowPitchElements],
                            g_dxgiWidth * sizeof(uint32_t));
            }
            minX = 0; minY = 0;
            maxX = g_dxgiWidth - 1; maxY = g_dxgiHeight - 1;
        } else {
            const size_t rowBytes = g_dxgiWidth * sizeof(uint32_t);
            for (int y = 0; y < g_dxgiHeight; ++y) {
                int prevRowOffset = y * g_dxgiWidth;
                int curRowOffset = y * rowPitchElements;

                if (std::memcmp(&curPixels[curRowOffset], &g_dxgiPrevPixels[prevRowOffset], rowBytes) == 0) {
                    continue;
                }

                if (y < minY) minY = y;
                if (y > maxY) maxY = y;

                for (int x = 0; x < g_dxgiWidth; ++x) {
                    if (curPixels[curRowOffset + x] != g_dxgiPrevPixels[prevRowOffset + x]) {
                        if (x < minX) minX = x;
                        break;
                    }
                }
                for (int x = g_dxgiWidth - 1; x >= minX; --x) {
                    if (curPixels[curRowOffset + x] != g_dxgiPrevPixels[prevRowOffset + x]) {
                        if (x > maxX) maxX = x;
                        break;
                    }
                }
                std::memcpy(&g_dxgiPrevPixels[prevRowOffset], &curPixels[curRowOffset], rowBytes);
            }
        }

        if (minX > maxX || minY > maxY) {
            g_d3dContext->Unmap(g_stagingTexture, 0);
            if (g_needsRefinement && !g_dxgiPrevPixels.empty()) {
                auto now = std::chrono::steady_clock::now();
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastMotionTime).count();
                if (elapsedMs >= 150) {
                    g_needsRefinement = false;
                    return EncodeRefinementFrame(g_dxgiPrevPixels.data(), g_dxgiWidth, g_dxgiHeight, outBuffer, outX, outY, outWidth, outHeight);
                }
            }
            return false;
        }

        // Màn hình có chuyển động: Đánh dấu cần làm nét (Progressive Refinement) khi dừng
        g_needsRefinement = true;
        g_lastMotionTime = std::chrono::steady_clock::now();

        int alignedX = minX & ~1;
        int alignedY = minY & ~1;
        int alignedW = maxX - alignedX + 1;
        int alignedH = maxY - alignedY + 1;
        if ((alignedW & 1) && (alignedX + alignedW < g_dxgiWidth)) alignedW++;
        if ((alignedH & 1) && (alignedY + alignedH < g_dxgiHeight)) alignedH++;

        outX = alignedX;
        outY = alignedY;
        outWidth = alignedW;
        outHeight = alignedH;

        const unsigned char* cropStartPtr = ((const unsigned char*)mapped.pData) + (outY * mapped.RowPitch) + (outX * 4);

        int changedArea = outWidth * outHeight;
        CompressionConfig cfg = GetCompressionConfig(changedArea, pixelsCount);

        int compressRes = tjCompress2(
            g_tjCompressor,
            cropStartPtr,
            outWidth,
            mapped.RowPitch,
            outHeight,
            TJPF_BGRX,
            &g_jpegBuf,
            &g_jpegBufSize,
            cfg.subsamp,
            cfg.quality,
            TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE
        );

        g_d3dContext->Unmap(g_stagingTexture, 0);

        if (compressRes != 0 || !g_jpegBuf || g_jpegBufSize == 0) {
            return false;
        }

        Protocol::VideoDeltaHeader delta = { outX, outY, outWidth, outHeight };
        outBuffer.resize(sizeof(delta) + g_jpegBufSize);
        std::memcpy(outBuffer.data(), &delta, sizeof(delta));
        std::memcpy(outBuffer.data() + sizeof(delta), g_jpegBuf, g_jpegBufSize);

        return true;
    }

    // ==========================================
    // 2. GDI BITBLT FALLBACK ENGINE (CPU)
    // ==========================================
    int g_gdiWidth = 0;
    int g_gdiHeight = 0;
    HDC g_hDesktopDC = NULL;
    HDC g_hCaptureDC = NULL;
    HBITMAP g_hDibBitmap = NULL;
    HBITMAP g_hOldBitmap = NULL;
    uint32_t* g_dibPixels = nullptr;
    std::vector<uint32_t> g_gdiPrevPixels;

    void CleanupGdi() {
        g_gdiPrevPixels.clear();
        if (g_hCaptureDC && g_hOldBitmap) SelectObject(g_hCaptureDC, g_hOldBitmap);
        if (g_hDibBitmap) DeleteObject(g_hDibBitmap);
        if (g_hCaptureDC) DeleteDC(g_hCaptureDC);
        if (g_hDesktopDC) ReleaseDC(NULL, g_hDesktopDC);

        g_hCaptureDC = NULL;
        g_hDesktopDC = NULL;
        g_hDibBitmap = NULL;
        g_hOldBitmap = NULL;
        g_dibPixels = nullptr;
    }

    bool InitGdi() {
        CleanupGdi();

        HDESK hInputDesk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
        if (!hInputDesk) hInputDesk = OpenDesktopA("Default", 0, FALSE, GENERIC_ALL);
        if (hInputDesk) {
            SetThreadDesktop(hInputDesk);
            CloseDesktop(hInputDesk);
        }

        g_gdiWidth = GetSystemMetrics(SM_CXSCREEN);
        g_gdiHeight = GetSystemMetrics(SM_CYSCREEN);

        g_hDesktopDC = GetDC(NULL);
        if (!g_hDesktopDC) return false;

        g_hCaptureDC = CreateCompatibleDC(g_hDesktopDC);
        if (!g_hCaptureDC) {
            ReleaseDC(NULL, g_hDesktopDC);
            g_hDesktopDC = NULL;
            return false;
        }

        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = g_gdiWidth;
        bmi.bmiHeader.biHeight = -g_gdiHeight;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        g_hDibBitmap = CreateDIBSection(g_hCaptureDC, &bmi, DIB_RGB_COLORS, (void**)&g_dibPixels, NULL, 0);
        if (!g_hDibBitmap || !g_dibPixels) {
            CleanupGdi();
            return false;
        }

        g_hOldBitmap = (HBITMAP)SelectObject(g_hCaptureDC, g_hDibBitmap);
        g_gdiPrevPixels.clear();
        return true;
    }

    bool CaptureGdiFrame(std::vector<char>& outBuffer, int& outX, int& outY, int& outWidth, int& outHeight) {
        HDC hScreenDC = GetDC(NULL);
        if (!hScreenDC) return false;

        BOOL bltOk = BitBlt(g_hCaptureDC, 0, 0, g_gdiWidth, g_gdiHeight, hScreenDC, 0, 0, SRCCOPY);
        ReleaseDC(NULL, hScreenDC);

        if (!bltOk) {
            return false;
        }

        int pixelsCount = g_gdiWidth * g_gdiHeight;
        int minX = g_gdiWidth, minY = g_gdiHeight, maxX = -1, maxY = -1;

        if (g_gdiPrevPixels.empty()) {
            g_gdiPrevPixels.assign(g_dibPixels, g_dibPixels + pixelsCount);
            minX = 0; minY = 0;
            maxX = g_gdiWidth - 1; maxY = g_gdiHeight - 1;
        } else {
            const size_t rowBytes = g_gdiWidth * sizeof(uint32_t);
            for (int y = 0; y < g_gdiHeight; ++y) {
                int rowOffset = y * g_gdiWidth;
                if (std::memcmp(&g_dibPixels[rowOffset], &g_gdiPrevPixels[rowOffset], rowBytes) == 0) {
                    continue;
                }

                if (y < minY) minY = y;
                if (y > maxY) maxY = y;

                for (int x = 0; x < g_gdiWidth; ++x) {
                    if (g_dibPixels[rowOffset + x] != g_gdiPrevPixels[rowOffset + x]) {
                        if (x < minX) minX = x;
                        break;
                    }
                }
                for (int x = g_gdiWidth - 1; x >= minX; --x) {
                    if (g_dibPixels[rowOffset + x] != g_gdiPrevPixels[rowOffset + x]) {
                        if (x > maxX) maxX = x;
                        break;
                    }
                }
                std::memcpy(&g_gdiPrevPixels[rowOffset], &g_dibPixels[rowOffset], rowBytes);
            }
        }

        if (minX > maxX || minY > maxY) {
            if (g_needsRefinement && !g_gdiPrevPixels.empty()) {
                auto now = std::chrono::steady_clock::now();
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastMotionTime).count();
                if (elapsedMs >= 150) {
                    g_needsRefinement = false;
                    return EncodeRefinementFrame(g_gdiPrevPixels.data(), g_gdiWidth, g_gdiHeight, outBuffer, outX, outY, outWidth, outHeight);
                }
            }
            return false;
        }

        // Màn hình có chuyển động: Đánh dấu cần làm nét (Progressive Refinement) khi dừng
        g_needsRefinement = true;
        g_lastMotionTime = std::chrono::steady_clock::now();

        int alignedX = minX & ~1;
        int alignedY = minY & ~1;
        int alignedW = maxX - alignedX + 1;
        int alignedH = maxY - alignedY + 1;
        if ((alignedW & 1) && (alignedX + alignedW < g_gdiWidth)) alignedW++;
        if ((alignedH & 1) && (alignedY + alignedH < g_gdiHeight)) alignedH++;

        outX = alignedX;
        outY = alignedY;
        outWidth = alignedW;
        outHeight = alignedH;

        const unsigned char* cropStartPtr = (const unsigned char*)(g_dibPixels + (outY * g_gdiWidth) + outX);
        int pitch = g_gdiWidth * 4;

        int changedArea = outWidth * outHeight;
        CompressionConfig cfg = GetCompressionConfig(changedArea, pixelsCount);

        if (tjCompress2(g_tjCompressor, cropStartPtr, outWidth, pitch, outHeight,
                        TJPF_BGRX, &g_jpegBuf, &g_jpegBufSize,
                        cfg.subsamp, cfg.quality, TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE) != 0) {
            return false;
        }

        if (g_jpegBuf && g_jpegBufSize > 0) {
            Protocol::VideoDeltaHeader delta = { outX, outY, outWidth, outHeight };
            outBuffer.resize(sizeof(delta) + g_jpegBufSize);
            std::memcpy(outBuffer.data(), &delta, sizeof(delta));
            std::memcpy(outBuffer.data() + sizeof(delta), g_jpegBuf, g_jpegBufSize);
            return true;
        }

        return false;
    }
}

namespace ScreenCapture {

    bool Initialize() {
        if (g_isInitialized) return true;

        g_avgSendTimeMs.store(2.0, std::memory_order_relaxed);
        g_needsRefinement = false;
        g_lastMotionTime = std::chrono::steady_clock::now();

        g_tjCompressor = tjInitCompress();
        if (!g_tjCompressor) return false;

        // Ưu tiên khởi tạo DirectX 11 Desktop Duplication API (GPU)
        if (InitDxgi()) {
            g_useDxgi = true;
            std::cout << "[ScreenCapture] Active Engine: DirectX 11 Desktop Duplication (GPU Accelerated, 0% CPU Idle)\n";
        } else {
            // Tự động chuyển về GDI BitBlt nếu card màn hình hoặc môi trường không hỗ trợ DXGI
            g_useDxgi = false;
            if (!InitGdi()) {
                tjDestroy(g_tjCompressor);
                g_tjCompressor = nullptr;
                return false;
            }
            std::cout << "[ScreenCapture] Active Engine: GDI BitBlt Fallback Engine\n";
        }

        g_isInitialized = true;
        return true;
    }

    bool CaptureFrame(std::vector<char>& outBuffer, int& outX, int& outY, int& outWidth, int& outHeight) {
        if (!g_isInitialized) return false;

        if (g_useDxgi) {
            return CaptureDxgiFrame(outBuffer, outX, outY, outWidth, outHeight);
        } else {
            return CaptureGdiFrame(outBuffer, outX, outY, outWidth, outHeight);
        }
    }

    void ReportSendTime(double sendTimeMs) {
        if (sendTimeMs <= 0.0) return;
        double current = g_avgSendTimeMs.load(std::memory_order_relaxed);
        // Exponential Moving Average: 85% lịch sử + 15% mẫu đo mới
        double updated = 0.85 * current + 0.15 * sendTimeMs;
        g_avgSendTimeMs.store(updated, std::memory_order_relaxed);
    }

    void Cleanup() {
        if (!g_isInitialized) return;

        g_needsRefinement = false;

        if (g_useDxgi) {
            CleanupDxgi();
        } else {
            CleanupGdi();
        }

        if (g_jpegBuf) {
            tjFree(g_jpegBuf);
            g_jpegBuf = nullptr;
            g_jpegBufSize = 0;
        }

        if (g_tjCompressor) {
            tjDestroy(g_tjCompressor);
            g_tjCompressor = nullptr;
        }

        g_isInitialized = false;
    }
}
