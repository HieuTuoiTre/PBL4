#pragma once
#include <vector>
#include <windows.h>
#include "../../Shared/Protocol.h"

namespace ScreenCapture {
    // 1. Khởi tạo các Device Context (HDC) hoặc DirectX 11 DXGI
    bool Initialize();

    // 2. Chụp toàn bộ hoặc vùng thay đổi của màn hình, nén thành JPEG và lưu vào outBuffer
    // Hỗ trợ Progressive Refinement (làm nét tức thì khi dừng) và Dynamic Quality
    bool CaptureFrame(std::vector<char>& outBuffer, int& outX, int& outY, int& outWidth, int& outHeight);

    // 3. Giải phóng tài nguyên đồ họa trước khi tắt phần mềm
    void Cleanup();

    // 4. Báo cáo thời gian gửi qua socket để tự động đo tốc độ mạng thời gian thực
    void ReportSendTime(double sendTimeMs);

    // 5. Yêu cầu gửi toàn bộ màn hình (Full-Frame) ở khung kế tiếp (khi Manager mới kết nối)
    void ForceFullFrame();
}
