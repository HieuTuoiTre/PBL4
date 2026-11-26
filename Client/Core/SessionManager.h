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

    // Sinh mã PIN ngẫu nhiên 6 chữ số
    std::string GenerateNewPin();

    // Quản lý cấp quyền kết nối cổng RFB 5900 dựa trên cổng 8080
    bool IsPeerAuthorized(const std::string& ip);
    void AuthorizePeer(const std::string& ip, uint64_t durationMs = 30000);
    void RevokeAuthorizedPeer();
    std::string GetAuthorizedPeerIP();

    // Correlation / Session ID cho việc truy vết nhật ký (N-2)
    std::string GetCurrentSessionId();

    // Kiểm tra trạng thái kết nối phiên
    bool IsConnected();

    // Gửi dữ liệu clipboard văn bản sang Manager
    bool SendClipboardText(const std::string& text);
}
