#include "ScreenCapture.h"
#include <turbojpeg.h>
#include <cstring>
#include <vector>
#include <algorithm>

namespace {
    bool g_isInitialized = false;
    int g_screenWidth = 0;
    int g_screenHeight = 0;

    HDC g_hDesktopDC = NULL;
    HDC g_hCaptureDC = NULL;
    HBITMAP g_hDibBitmap = NULL;
    HBITMAP g_hOldBitmap = NULL;
    uint32_t* g_dibPixels = nullptr;

    tjhandle g_tjCompressor = nullptr;
    unsigned char* g_jpegBuf = nullptr;
    unsigned long g_jpegBufSize = 0;

    std::vector<uint32_t> g_prevPixels;
}

namespace ScreenCapture {

    bool Initialize() {
        if (g_isInitialized) return true;

        g_tjCompressor = tjInitCompress();
        if (!g_tjCompressor) return false;

        g_screenWidth = GetSystemMetrics(SM_CXSCREEN);
        g_screenHeight = GetSystemMetrics(SM_CYSCREEN);

        g_hDesktopDC = GetDC(NULL);
        if (!g_hDesktopDC) {
            tjDestroy(g_tjCompressor);
            g_tjCompressor = nullptr;
            return false;
        }

        g_hCaptureDC = CreateCompatibleDC(g_hDesktopDC);
        if (!g_hCaptureDC) {
            ReleaseDC(NULL, g_hDesktopDC);
            g_hDesktopDC = NULL;
            tjDestroy(g_tjCompressor);
            g_tjCompressor = nullptr;
            return false;
        }

        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = g_screenWidth;
        bmi.bmiHeader.biHeight = -g_screenHeight; // Top-down DIB bitmap
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        g_hDibBitmap = CreateDIBSection(g_hDesktopDC, &bmi, DIB_RGB_COLORS, (void**)&g_dibPixels, NULL, 0);
<<<<<<< HEAD
        if (!g_hDibBitmap || !g_dibPixels) return false;

        // Cấp phát sẵn bộ đệm 2MB HGLOBAL để nén ảnh không bị phân mảnh bộ nhớ
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, 2 * 1024 * 1024);
        if (!hMem || CreateStreamOnHGlobal(hMem, TRUE, &g_pStream) != S_OK) return false;
=======
        if (!g_hDibBitmap || !g_dibPixels) {
            DeleteDC(g_hCaptureDC); g_hCaptureDC = NULL;
            ReleaseDC(NULL, g_hDesktopDC); g_hDesktopDC = NULL;
            tjDestroy(g_tjCompressor); g_tjCompressor = nullptr;
            return false;
        }
>>>>>>> origin/main

        g_hOldBitmap = (HBITMAP)SelectObject(g_hCaptureDC, g_hDibBitmap);
        g_prevPixels.clear();
        g_isInitialized = true;
        return true;
    }

    bool CaptureFrame(std::vector<char>& outBuffer, int& outX, int& outY, int& outWidth, int& outHeight) {
        if (!g_isInitialized || !g_tjCompressor) return false;

        // Chụp nội dung màn hình hiện tại vào bộ đệm DIB Section
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

        // Không có thay đổi nào trên màn hình -> Không cần gửi gói tin
        if (minX > maxX || minY > maxY) return false;

<<<<<<< HEAD
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
=======
        // Căn chỉnh tọa độ về số chẵn để tối ưu Chroma Subsampling 4:2:0
        int alignedX = minX & ~1;
        int alignedY = minY & ~1;
        int alignedW = maxX - alignedX + 1;
        int alignedH = maxY - alignedY + 1;
        if ((alignedW & 1) && (alignedX + alignedW < g_screenWidth)) alignedW++;
        if ((alignedH & 1) && (alignedY + alignedH < g_screenHeight)) alignedH++;
>>>>>>> origin/main

        outX = alignedX;
        outY = alignedY;
        outWidth = alignedW;
        outHeight = alignedH;

        // Con trỏ tới góc trên bên trái của vùng thay đổi
        const unsigned char* cropStartPtr = (const unsigned char*)(g_dibPixels + (outY * g_screenWidth) + outX);
        int pitch = g_screenWidth * 4;

        // Chất lượng JPEG sắc nét: 75% cho khung hình lớn, 85% cho vùng chữ/chuột nhỏ
        int changedArea = outWidth * outHeight;
<<<<<<< HEAD
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
=======
        int quality = (changedArea > (pixelsCount / 2)) ? 75 : 85;

        // Nén trực tiếp với TurboJPEG SIMD (Zero-copy crop, nén trong ~2-5ms)
        if (tjCompress2(g_tjCompressor, cropStartPtr, outWidth, pitch, outHeight,
                        TJPF_BGRX, &g_jpegBuf, &g_jpegBufSize,
                        TJSAMP_420, quality, TJFLAG_FASTDCT) != 0) {
            return false;
        }

        if (g_jpegBuf && g_jpegBufSize > 0) {
>>>>>>> origin/main
            Protocol::VideoDeltaHeader delta = { outX, outY, outWidth, outHeight };
            outBuffer.resize(sizeof(delta) + g_jpegBufSize);
            std::memcpy(outBuffer.data(), &delta, sizeof(delta));
<<<<<<< HEAD

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
=======
            std::memcpy(outBuffer.data() + sizeof(delta), g_jpegBuf, g_jpegBufSize);
            return true;
>>>>>>> origin/main
        }

        return false;
    }

    void Cleanup() {
        if (!g_isInitialized) return;
        g_prevPixels.clear();

        if (g_jpegBuf) {
            tjFree(g_jpegBuf);
            g_jpegBuf = nullptr;
            g_jpegBufSize = 0;
        }

        if (g_tjCompressor) {
            tjDestroy(g_tjCompressor);
            g_tjCompressor = nullptr;
        }

        if (g_hCaptureDC && g_hOldBitmap) SelectObject(g_hCaptureDC, g_hOldBitmap);
        if (g_hDibBitmap) DeleteObject(g_hDibBitmap);
        if (g_hCaptureDC) DeleteDC(g_hCaptureDC);
        if (g_hDesktopDC) ReleaseDC(NULL, g_hDesktopDC);

        g_hCaptureDC = NULL;
        g_hDesktopDC = NULL;
        g_hDibBitmap = NULL;
        g_hOldBitmap = NULL;
        g_dibPixels = nullptr;

        g_isInitialized = false;
    }
}
