#pragma once
#include <vector>
#include <windows.h>
#include "../../Shared/Protocol.h"

namespace ScreenCapture {
    // 1. Khởi tạo các Device Context (HDC) và GDI+
    // Gọi một lần duy nhất lúc bật phần mềm.
    bool Initialize();

    // 2. Chụp toàn bộ màn hình, nén thành JPEG và lưu vào outBuffer
    // Trả về true nếu chụp và nén thành công
    // Thêm các tham số tọa độ đầu ra
    bool CaptureFrame(std::vector<char>& outBuffer, int& outX, int& outY, int& outWidth, int& outHeight);

    // 3. Giải phóng bộ nhớ HDC và GDI+ trước khi tắt phần mềm
    void Cleanup();
}