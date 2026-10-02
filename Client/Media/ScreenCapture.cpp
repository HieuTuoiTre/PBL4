#include "ScreenCapture.h"
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib") 

using namespace Gdiplus;

namespace {
    ULONG_PTR g_gdiplusToken = 0;
    bool g_isInitialized = false;
    int g_screenWidth = 0;
    int g_screenHeight = 0;
    
    // Mảng lưu trữ điểm ảnh của khung hình trước đó
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
        
        g_screenWidth = GetSystemMetrics(SM_CXSCREEN);
        g_screenHeight = GetSystemMetrics(SM_CYSCREEN);
        g_isInitialized = true;
        return true;
    }

    bool CaptureFrame(std::vector<char>& outBuffer, int& outX, int& outY, int& outWidth, int& outHeight) {
        if (!g_isInitialized) return false;

        HWND hDesktopWnd = GetDesktopWindow();
        HDC hDesktopDC = GetDC(hDesktopWnd);
        HDC hCaptureDC = CreateCompatibleDC(hDesktopDC);
        HBITMAP hCaptureBitmap = CreateCompatibleBitmap(hDesktopDC, g_screenWidth, g_screenHeight);
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hCaptureDC, hCaptureBitmap);

        // Chụp màn hình
        BitBlt(hCaptureDC, 0, 0, g_screenWidth, g_screenHeight, hDesktopDC, 0, 0, SRCCOPY | CAPTUREBLT);

        Bitmap bitmap(hCaptureBitmap, NULL);
        
        BitmapData bmpData;
        Rect fullRect(0, 0, g_screenWidth, g_screenHeight);
        bitmap.LockBits(&fullRect, ImageLockModeRead, PixelFormat32bppARGB, &bmpData);
        
        uint32_t* currentPixels = (uint32_t*)bmpData.Scan0;
        int pixelsCount = g_screenWidth * g_screenHeight;
        
        int minX = g_screenWidth, minY = g_screenHeight, maxX = -1, maxY = -1;

        if (g_prevPixels.empty()) {
            g_prevPixels.assign(currentPixels, currentPixels + pixelsCount);
            minX = 0; minY = 0; 
            maxX = g_screenWidth - 1; maxY = g_screenHeight - 1;
        } else {
            for (int y = 0; y < g_screenHeight; ++y) {
                int rowOffset = y * g_screenWidth;
                for (int x = 0; x < g_screenWidth; ++x) {
                    if (currentPixels[rowOffset + x] != g_prevPixels[rowOffset + x]) {
                        if (x < minX) minX = x;
                        if (x > maxX) maxX = x;
                        if (y < minY) minY = y;
                        if (y > maxY) maxY = y;
                    }
                }
            }
        }
        
        // VÁ LỖI 1: Phải copy mảng pixel TRƯỚC KHI UnlockBits để tránh crash Client
        if (minX <= maxX) {
            memcpy(g_prevPixels.data(), currentPixels, pixelsCount * sizeof(uint32_t));
        }
        
        bitmap.UnlockBits(&bmpData);

        if (minX > maxX) {
            SelectObject(hCaptureDC, hOldBitmap);
            DeleteObject(hCaptureBitmap);
            DeleteDC(hCaptureDC);
            ReleaseDC(hDesktopWnd, hDesktopDC);
            return false;
        }

        outX = minX;
        outY = minY;
        outWidth = maxX - minX + 1;
        outHeight = maxY - minY + 1;

        Rect cropRect(outX, outY, outWidth, outHeight);
        // VÁ LỖI 2: Dùng PixelFormatDontCare để GDI+ giữ nguyên định dạng, tránh lỗi ảnh trắng
        Bitmap* croppedBmp = bitmap.Clone(cropRect, PixelFormatDontCare);

        CLSID jpegClsid;
        GetEncoderClsid(L"image/jpeg", &jpegClsid);
        EncoderParameters encoderParameters;
        encoderParameters.Count = 1;
        encoderParameters.Parameter[0].Guid = EncoderQuality;
        encoderParameters.Parameter[0].Type = EncoderParameterValueTypeLong;
        encoderParameters.Parameter[0].NumberOfValues = 1;
        ULONG quality = 50; 
        encoderParameters.Parameter[0].Value = &quality;

        IStream* pStream = NULL;
        CreateStreamOnHGlobal(NULL, TRUE, &pStream);
        
        if (croppedBmp) { 
            croppedBmp->Save(pStream, &jpegClsid, &encoderParameters);
        }
        
        STATSTG statstg;
        pStream->Stat(&statstg, STATFLAG_NONAME);
        ULONG streamSize = statstg.cbSize.LowPart;
        
        // Đảm bảo có dữ liệu mới copy
        if (streamSize > 0) {
            LARGE_INTEGER liZero = {};
            pStream->Seek(liZero, STREAM_SEEK_SET, NULL);
            outBuffer.resize(streamSize);
            ULONG bytesRead = 0;
            pStream->Read(outBuffer.data(), streamSize, &bytesRead);
        } else {
            outBuffer.clear();
        }
        
        pStream->Release();
        if (croppedBmp) delete croppedBmp;
        SelectObject(hCaptureDC, hOldBitmap);
        DeleteObject(hCaptureBitmap);
        DeleteDC(hCaptureDC);
        ReleaseDC(hDesktopWnd, hDesktopDC);

        return !outBuffer.empty();
    }

    void Cleanup() {
        if (!g_isInitialized) return;
        g_prevPixels.clear(); // Dọn dẹp RAM
        GdiplusShutdown(g_gdiplusToken);
        g_isInitialized = false;
    }
}