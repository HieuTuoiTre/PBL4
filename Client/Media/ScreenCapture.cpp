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

    // Giữ cố định GDI resources và con trỏ bộ nhớ DIB (Zero-Copy)
    HDC g_hDesktopDC = NULL;
    HDC g_hCaptureDC = NULL;
    HBITMAP g_hDibBitmap = NULL;
    HBITMAP g_hOldBitmap = NULL;
    uint32_t* g_dibPixels = nullptr; // Con trỏ trỏ thẳng vào pixel màn hình

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

        // Tạo DIBSection 32-bit để CPU đọc trực tiếp mảng pixel không cần LockBits
        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = g_screenWidth;
        bmi.bmiHeader.biHeight = -g_screenHeight; // Top-down bitmap (chiều từ trên xuống)
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        g_hDibBitmap = CreateDIBSection(g_hDesktopDC, &bmi, DIB_RGB_COLORS, (void**)&g_dibPixels, NULL, 0);
        if (!g_hDibBitmap || !g_dibPixels) return false;

        g_hOldBitmap = (HBITMAP)SelectObject(g_hCaptureDC, g_hDibBitmap);
        g_prevPixels.clear();
        g_isInitialized = true;
        return true;
    }

    bool CaptureFrame(std::vector<char>& outBuffer, int& outX, int& outY, int& outWidth, int& outHeight) {
        if (!g_isInitialized) return false;

        // 1. Chụp thẳng vào vùng nhớ g_dibPixels (bỏ CAPTUREBLT để tránh nghẽn DWM)
        if (!BitBlt(g_hCaptureDC, 0, 0, g_screenWidth, g_screenHeight, g_hDesktopDC, 0, 0, SRCCOPY)) {
            return false;
        }

        int pixelsCount = g_screenWidth * g_screenHeight;
        int minX = g_screenWidth, minY = g_screenHeight, maxX = -1, maxY = -1;

        // 2. Tìm vùng màn hình thay đổi (Dùng memcmp nhảy cóc từng hàng cực nhanh)
        if (g_prevPixels.empty()) {
            g_prevPixels.assign(g_dibPixels, g_dibPixels + pixelsCount);
            minX = 0; minY = 0;
            maxX = g_screenWidth - 1; maxY = g_screenHeight - 1;
        } else {
            const size_t rowBytes = g_screenWidth * sizeof(uint32_t);
            for (int y = 0; y < g_screenHeight; ++y) {
                int rowOffset = y * g_screenWidth;
                // Nếu cả hàng ngang không đổi -> bỏ qua ngay lập tức
                if (std::memcmp(&g_dibPixels[rowOffset], &g_prevPixels[rowOffset], rowBytes) == 0) {
                    continue;
                }

                if (y < minY) minY = y;
                if (y > maxY) maxY = y;

                // Quét từ trái sang tìm biên trái
                for (int x = 0; x < g_screenWidth; ++x) {
                    if (g_dibPixels[rowOffset + x] != g_prevPixels[rowOffset + x]) {
                        if (x < minX) minX = x;
                        break;
                    }
                }
                // Quét từ phải sang tìm biên phải
                for (int x = g_screenWidth - 1; x >= minX; --x) {
                    if (g_dibPixels[rowOffset + x] != g_prevPixels[rowOffset + x]) {
                        if (x > maxX) maxX = x;
                        break;
                    }
                }
                // Cập nhật lại hàng pixel đã thay đổi
                std::memcpy(&g_prevPixels[rowOffset], &g_dibPixels[rowOffset], rowBytes);
            }
        }

        // Không có điểm ảnh nào thay đổi
        if (minX > maxX) return false;

        outX = minX;
        outY = minY;
        outWidth = maxX - minX + 1;
        outHeight = maxY - minY + 1;

        // 3. ZERO-COPY CROP: Trỏ trực tiếp Gdiplus::Bitmap vào vùng (outX, outY) trên g_dibPixels
        BYTE* cropStartPtr = (BYTE*)(g_dibPixels + (outY * g_screenWidth) + outX);
        int stride = g_screenWidth * 4;
        Bitmap croppedBmp(outWidth, outHeight, stride, PixelFormat32bppRGB, cropStartPtr);

        // 4. Linh hoạt giảm chất lượng JPEG khi khung hình thay đổi lớn để giảm độ trễ mạng
        int changedArea = outWidth * outHeight;
        ULONG quality = (changedArea > (pixelsCount / 2)) ? 35 : 55;

        EncoderParameters encoderParameters;
        encoderParameters.Count = 1;
        encoderParameters.Parameter[0].Guid = EncoderQuality;
        encoderParameters.Parameter[0].Type = EncoderParameterValueTypeLong;
        encoderParameters.Parameter[0].NumberOfValues = 1;
        encoderParameters.Parameter[0].Value = &quality;

        IStream* pStream = NULL;
        if (CreateStreamOnHGlobal(NULL, TRUE, &pStream) != S_OK) return false;

        croppedBmp.Save(pStream, &g_jpegClsid, &encoderParameters);

        // 5. Đặt sẵn 16 byte đầu của outBuffer cho VideoDeltaHeader để không phải copy lần 2
        STATSTG statstg;
        pStream->Stat(&statstg, STATFLAG_NONAME);
        ULONG streamSize = statstg.cbSize.LowPart;

        if (streamSize > 0) {
            LARGE_INTEGER liZero = {};
            pStream->Seek(liZero, STREAM_SEEK_SET, NULL);

            Protocol::VideoDeltaHeader delta = { outX, outY, outWidth, outHeight };
            outBuffer.resize(sizeof(delta) + streamSize);
            std::memcpy(outBuffer.data(), &delta, sizeof(delta));

            ULONG bytesRead = 0;
            pStream->Read(outBuffer.data() + sizeof(delta), streamSize, &bytesRead);
        } else {
            outBuffer.clear();
        }

        pStream->Release();
        return !outBuffer.empty();
    }

    void Cleanup() {
        if (!g_isInitialized) return;
        g_prevPixels.clear();
        if (g_hCaptureDC && g_hOldBitmap) SelectObject(g_hCaptureDC, g_hOldBitmap);
        if (g_hDibBitmap) DeleteObject(g_hDibBitmap);
        if (g_hCaptureDC) DeleteDC(g_hCaptureDC);
        if (g_hDesktopDC) ReleaseDC(NULL, g_hDesktopDC);
        GdiplusShutdown(g_gdiplusToken);
        g_isInitialized = false;
    }
}