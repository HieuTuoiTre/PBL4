#pragma once

#include "../../Shared/Protocol.h"
#include "StatusOverlay.h"
#include "ConnectionAlert.h"

namespace SessionManager {
    // Hàm mở server chờ Manager kết nối tới (cổng cơ sở basePort, mặc định 8080)
    void StartServer(int basePort = Protocol::PORT_CONTROL);
    
    // Hàm đóng kết nối và dọn dẹp
    void Stop();

    // Lấy instance overlay nếu cần tương tác từ nơi khác
    StatusOverlay& GetOverlay();

    // Cấu hình token xác thực phiên
    void SetExpectedToken(const std::string& token);
    std::string GetExpectedToken();

    // Kiểm tra trạng thái kết nối phiên
    bool IsConnected();

    // Gửi dữ liệu clipboard văn bản sang Manager
    bool SendClipboardText(const std::string& text);
}
