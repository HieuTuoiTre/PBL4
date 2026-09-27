#include "ScreenCapture.h"
#include <gdiplus.h> // Thư viện đồ họa nâng cao của Windows (dùng để nén ảnh JPEG)

// Yêu cầu Linker tự động liên kết với thư viện gdiplus.lib
#pragma comment(lib, "gdiplus.lib") 

using namespace Gdiplus;

namespace {
    // Biến lưu token quản lý vòng đời của GDI+
    ULONG_PTR g_gdiplusToken = 0;
    bool g_isInitialized = false;

    // Kích thước màn hình
    int g_screenWidth = 0;
    int g_screenHeight = 0;

    // Hàm tra cứu mã CLSID của thuật toán nén ảnh (JPEG, PNG, v.v.)
    int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
        UINT num = 0;          // Số lượng bộ mã hóa
        UINT size = 0;         // Kích thước mảng byte của bộ mã hóa

        GetImageEncodersSize(&num, &size);
        if (size == 0) return -1;

        ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
        if (pImageCodecInfo == NULL) return -1;

        GetImageEncoders(num, size, pImageCodecInfo);
        for (UINT j = 0; j < num; ++j) {
            // So sánh xem có phải định dạng đang cần tìm không (ví dụ "image/jpeg")
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

        // 1. Khởi động GDI+
        GdiplusStartupInput gdiplusStartupInput;
        Status status = GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);
        if (status != Ok) {
            return false;
        }

        // 2. Lấy kích thước màn hình chính hiện tại
        g_screenWidth = GetSystemMetrics(SM_CXSCREEN);
        g_screenHeight = GetSystemMetrics(SM_CYSCREEN);

        g_isInitialized = true;
        return true;
    }

    bool CaptureFrame(std::vector<char>& outBuffer) {
        if (!g_isInitialized) return false;

        // 1. Lấy "khung vẽ" (Device Context - DC) của toàn bộ màn hình
        HWND hDesktopWnd = GetDesktopWindow();
        HDC hDesktopDC = GetDC(hDesktopWnd);

        // 2. Tạo một khung vẽ ảo (Memory DC) trên RAM máy tính
        HDC hCaptureDC = CreateCompatibleDC(hDesktopDC);

        // 3. Tạo một "tờ giấy trắng" (Bitmap) có kích thước bằng với độ phân giải màn hình
        HBITMAP hCaptureBitmap = CreateCompatibleBitmap(hDesktopDC, g_screenWidth, g_screenHeight);

        // 4. Đặt tờ giấy trắng đó lên khung vẽ ảo
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hCaptureDC, hCaptureBitmap);

        // 5. CHỤP ẢNH: Copy điểm ảnh từ màn hình thật sang khung vẽ ảo
        bool isCaptured = BitBlt(
            hCaptureDC, 0, 0, g_screenWidth, g_screenHeight, // Đích: Khung vẽ ảo
            hDesktopDC, 0, 0,                                // Nguồn: Bắt đầu từ tọa độ (0,0) của màn hình
            SRCCOPY | CAPTUREBLT                             // Cờ: Copy nguyên bản, bao gồm cả các cửa sổ trong suốt
        );

        if (!isCaptured) {
            // Nếu copy lỗi, phải dọn dẹp RAM rồi mới thoát để chống rò rỉ bộ nhớ
            SelectObject(hCaptureDC, hOldBitmap);
            DeleteObject(hCaptureBitmap);
            DeleteDC(hCaptureDC);
            ReleaseDC(hDesktopWnd, hDesktopDC);
            return false;
        }

        // ==========================================================
        // CÔNG ĐOẠN 3: NÉN ẢNH VÀ XUẤT RA outBuffer
        // ==========================================================

        // 1. Gói tờ giấy thô hCaptureBitmap vào đối tượng quản lý của GDI+
        Bitmap bitmap(hCaptureBitmap, NULL);

        // 2. Lấy mã nhận diện của bộ nén JPEG
        CLSID jpegClsid;
        GetEncoderClsid(L"image/jpeg", &jpegClsid);

        // 3. Ép chất lượng JPEG giảm xuống để tiết kiệm băng thông mạng (Ví dụ: 60%)
        // Nếu để mặc định 100%, ảnh sẽ rất nặng và gây giật lag
        EncoderParameters encoderParameters;
        encoderParameters.Count = 1;
        encoderParameters.Parameter[0].Guid = EncoderQuality;
        encoderParameters.Parameter[0].Type = EncoderParameterValueTypeLong;
        encoderParameters.Parameter[0].NumberOfValues = 1;
        ULONG quality = 60; // Bạn có thể tăng giảm từ 1 đến 100 tùy chất lượng mạng
        encoderParameters.Parameter[0].Value = &quality;

        // 4. Tạo một "file ảo" trên RAM (IStream) để chứa dữ liệu sau khi nén
        IStream* pStream = NULL;
        CreateStreamOnHGlobal(NULL, TRUE, &pStream);

        // 5. Nén ảnh và lưu thẳng vào IStream
        Status stat = bitmap.Save(pStream, &jpegClsid, &encoderParameters);
        
        if (stat == Ok) {
            // Lấy kích thước của dòng IStream
            STATSTG statstg;
            pStream->Stat(&statstg, STATFLAG_NONAME);
            ULONG streamSize = statstg.cbSize.LowPart;

            // Tua con trỏ đọc về lại đầu file ảo
            LARGE_INTEGER liZero = {};
            pStream->Seek(liZero, STREAM_SEEK_SET, NULL);

            // Mở rộng vector để có đủ chỗ chứa và copy dữ liệu từ IStream ra ngoài
            outBuffer.resize(streamSize);
            ULONG bytesRead = 0;
            pStream->Read(outBuffer.data(), streamSize, &bytesRead);
        }

        // 6. Giải phóng bộ nhớ của IStream
        if (pStream) {
            pStream->Release();
        }

        // 6. Dọn dẹp tài nguyên đồ họa của Win32 API 
        // (Lưu ý: hàm DeleteObject rất quan trọng. Hàm này được gọi 30 lần/giây, 
        // nếu quên xóa, RAM máy Client sẽ bị đầy tràn chỉ sau 1 phút).
        SelectObject(hCaptureDC, hOldBitmap);
        DeleteObject(hCaptureBitmap);
        DeleteDC(hCaptureDC);
        ReleaseDC(hDesktopWnd, hDesktopDC);

        return true;
    }

    void Cleanup() {
        if (!g_isInitialized) return;

        // Tắt GDI+ khi ứng dụng kết thúc
        GdiplusShutdown(g_gdiplusToken);
        g_isInitialized = false;
    }

} // namespace ScreenCapture