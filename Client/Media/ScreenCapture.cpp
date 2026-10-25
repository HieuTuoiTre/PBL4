#include "ScreenCapture.h"
#include <gdiplus.h>
#include <cstring>

#pragma comment(lib, "gdiplus.lib") 
using namespace Gdiplus;

namespace {
    ULONG_PTR g_gdiplusToken = 0;
    bool g_isInitialized = false;
    int g_screenWidth = 0;
    int g_screenHeight = 0;

    HDC g_hDesktopDC = NULL;
    HDC g_hCaptureDC = NULL;
    HBITMAP g_hDibBitmap = NULL;
    HBITMAP g_hOldBitmap = NULL;
    uint32_t* g_dibPixels = nullptr;
    IStream* g_pStream = nullptr; // Tái sử dụng Stream cố định để tránh cấp phát mỗi frame

    CLSID g_jpegClsid;
    std::vector<uint32_t> g_prevPixels;

    int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
        UINT num = 0, size = 0;
        GetImageEncodersSize(&num, &size);
        if (size == 0) return -1;
        ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
        GetImageEncoders(num, size, pImageCodecInfo);
        for (UINT j = 0; j < num; ++j) {
            if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
                *pClsid = pImageCodecInfo[j].Clsid;
                free(pImageCodecInfo);
                return j;
            }
        }
        free(pImageCodecInfo);
        return -1;
    }
}

namespace ScreenCapture {

    bool Initialize() {
        if (g_isInitialized) return true;

        GdiplusStartupInput gdiplusStartupInput;
        if (GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL) != Ok) return false;
        
        if (GetEncoderClsid(L"image/jpeg", &g_jpegClsid) == -1) return false;

        g_screenWidth = GetSystemMetrics(SM_CXSCREEN);
        g_screenHeight = GetSystemMetrics(SM_CYSCREEN);

        g_hDesktopDC = GetDC(NULL);
        g_hCaptureDC = CreateCompatibleDC(g_hDesktopDC);

        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = g_screenWidth;
        bmi.bmiHeader.biHeight = -g_screenHeight; // Top-down bitmap
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        g_hDibBitmap = CreateDIBSection(g_hDesktopDC, &bmi, DIB_RGB_COLORS, (void**)&g_dibPixels, NULL, 0);
        if (!g_hDibBitmap || !g_dibPixels) return false;

        // Cấp phát sẵn bộ đệm 2MB HGLOBAL để nén ảnh không bị phân mảnh bộ nhớ
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, 2 * 1024 * 1024);
        if (!hMem || CreateStreamOnHGlobal(hMem, TRUE, &g_pStream) != S_OK) return false;

        g_hOldBitmap = (HBITMAP)SelectObject(g_hCaptureDC, g_hDibBitmap);
        g_prevPixels.clear();
        g_isInitialized = true;
        return true;
    }

    bool CaptureFrame(std::vector<char>& outBuffer, int& outX, int& outY, int& outWidth, int& outHeight) {
        if (!g_isInitialized) return false;

        if (!BitBlt(g_hCaptureDC, 0, 0, g_screenWidth, g_screenHeight, g_hDesktopDC, 0, 0, SRCCOPY)) {
            return false;
        }

        int pixelsCount = g_screenWidth * g_screenHeight;
        int minX = g_screenWidth, minY = g_screenHeight, maxX = -1, maxY = -1;

        if (g_prevPixels.empty()) {
            g_prevPixels.assign(g_dibPixels, g_dibPixels + pixelsCount);
            minX = 0; minY = 0;
            maxX = g_screenWidth - 1; maxY = g_screenHeight - 1;
        } else {
            const size_t rowBytes = g_screenWidth * sizeof(uint32_t);
            for (int y = 0; y < g_screenHeight; ++y) {
                int rowOffset = y * g_screenWidth;
                if (std::memcmp(&g_dibPixels[rowOffset], &g_prevPixels[rowOffset], rowBytes) == 0) {
                    continue;
                }

                if (y < minY) minY = y;
                if (y > maxY) maxY = y;

                // Tối ưu quét biên: chỉ quét trong phạm vi có thể mở rộng biên minX/maxX
                for (int x = 0; x < minX; ++x) {
                    if (g_dibPixels[rowOffset + x] != g_prevPixels[rowOffset + x]) {
                        minX = x;
                        break;
                    }
                }
                for (int x = g_screenWidth - 1; x > maxX; --x) {
                    if (g_dibPixels[rowOffset + x] != g_prevPixels[rowOffset + x]) {
                        maxX = x;
                        break;
                    }
                }
                std::memcpy(&g_prevPixels[rowOffset], &g_dibPixels[rowOffset], rowBytes);
            }
        }

        if (minX > maxX) return false;

        // Căn chỉnh tọa độ theo block 16px (MCU JPEG) để tăng tốc nén tối đa
        int alignX = (minX / 16) * 16;
        int alignY = (minY / 16) * 16;
        int alignR = ((maxX + 16) / 16) * 16;
        int alignB = ((maxY + 16) / 16) * 16;
        if (alignR > g_screenWidth) alignR = g_screenWidth;
        if (alignB > g_screenHeight) alignB = g_screenHeight;

        outX = alignX;
        outY = alignY;
        outWidth = alignR - alignX;
        outHeight = alignB - alignY;

        BYTE* cropStartPtr = (BYTE*)(g_dibPixels + (outY * g_screenWidth) + outX);
        int stride = g_screenWidth * 4;
        Bitmap croppedBmp(outWidth, outHeight, stride, PixelFormat32bppRGB, cropStartPtr);

        int changedArea = outWidth * outHeight;
        ULONG quality = 50;
        if (changedArea > (pixelsCount / 2)) {
            quality = 35; // Khi toàn màn hình thay đổi -> nén nhanh hơn để giữ 60 FPS
        } else if (changedArea > (pixelsCount / 4)) {
            quality = 40;
        }

        EncoderParameters encoderParameters;
        encoderParameters.Count = 1;
        encoderParameters.Parameter[0].Guid = EncoderQuality;
        encoderParameters.Parameter[0].Type = EncoderParameterValueTypeLong;
        encoderParameters.Parameter[0].NumberOfValues = 1;
        encoderParameters.Parameter[0].Value = &quality;

        // Reset vị trí ghi về đầu stream (tái sử dụng buffer 2MB HGLOBAL)
        LARGE_INTEGER liZero = {};
        g_pStream->Seek(liZero, STREAM_SEEK_SET, NULL);

        if (croppedBmp.Save(g_pStream, &g_jpegClsid, &encoderParameters) != Ok) {
            return false;
        }

        ULARGE_INTEGER streamPos = {};
        g_pStream->Seek(liZero, STREAM_SEEK_CUR, &streamPos);
        ULONG streamSize = (ULONG)streamPos.QuadPart;

        if (streamSize > 0) {
            Protocol::VideoDeltaHeader delta = { outX, outY, outWidth, outHeight };
            outBuffer.resize(sizeof(delta) + streamSize);
            std::memcpy(outBuffer.data(), &delta, sizeof(delta));

            HGLOBAL hMem = NULL;
            if (GetHGlobalFromStream(g_pStream, &hMem) == S_OK && hMem) {
                void* pData = GlobalLock(hMem);
                if (pData) {
                    std::memcpy(outBuffer.data() + sizeof(delta), pData, streamSize);
                    GlobalUnlock(hMem);
                }
            }
        } else {
            outBuffer.clear();
        }

        return !outBuffer.empty();
    }

    void Cleanup() {
        if (!g_isInitialized) return;
        g_prevPixels.clear();
        if (g_pStream) {
            g_pStream->Release();
            g_pStream = nullptr;
        }
        if (g_hCaptureDC && g_hOldBitmap) SelectObject(g_hCaptureDC, g_hOldBitmap);
        if (g_hDibBitmap) DeleteObject(g_hDibBitmap);
        if (g_hCaptureDC) DeleteDC(g_hCaptureDC);
        if (g_hDesktopDC) ReleaseDC(NULL, g_hDesktopDC);
        GdiplusShutdown(g_gdiplusToken);
        g_isInitialized = false;
    }
}