// ============================================================================
// File: DiscoveryScanner.h
// Mô tả: Khai báo cấu trúc dữ liệu và giao diện quét tìm máy Client tự động trong LAN.
// Cơ chế:
//   - Manager phát gói tin UDP Broadcast trên cổng 8089 để "hỏi" (DISCOVERY_QUERY).
//   - Các máy Client đang bật sẽ phản hồi lại thông tin (Tên máy, User, Trạng thái bận).
//   - Manager tiếp nhận trên cổng 8088 và cập nhật tự động lên danh sách kết nối.
// ============================================================================

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN // Loại bỏ bớt các thư viện Windows không cần thiết để tăng tốc biên dịch
#endif

#include <winsock2.h>    // Thư viện mạng Windows Socket 2
#include <windows.h>     // Thư viện hệ điều hành Windows API chuẩn
#include <string>        // Thư viện chuỗi ký tự std::string và std::wstring
#include <functional>    // Thư viện con trỏ hàm callback std::function

// Cấu trúc lưu trữ thông tin của một máy Client được tìm thấy trong mạng LAN
struct DiscoveredClient {
    std::string ip;           // Địa chỉ IPv4 của Client (ví dụ: "192.168.1.15")
    std::wstring computerName; // Tên máy tính Client (dạng Unicode UTF-16, ví dụ: L"DESKTOP-ABC")
    std::wstring userName;     // Tên tài khoản Windows đang đăng nhập (ví dụ: L"Nguyen Van A")
    bool isBusy;              // Trạng thái: false = Sẵn sàng kết nối, true = Đang có Manager khác điều khiển
    DWORD lastSeenTick = 0;   // Mốc thời gian hệ thống (GetTickCount) lần cuối cùng nhận được phản hồi
};

namespace DiscoveryScanner {
    // Định nghĩa kiểu hàm gọi lại (Callback) khi danh sách Client có thay đổi:
    // - client: Đối tượng chứa thông tin máy Client vừa phản hồi
    // - isOnline: true = Client đang hoạt động / phản hồi; false = Client vừa thông báo tắt app (DISCOVERY_BYE)
    using ClientUpdateCallback = std::function<void(const DiscoveredClient& client, bool isOnline)>;

    // Khởi tạo socket UDP và bắt đầu luồng lắng nghe phản hồi từ các Client trong mạng
    bool Start(ClientUpdateCallback onUpdate);

    // Phát gói tin UDP Broadcast tới toàn bộ mạng LAN để yêu cầu tất cả Client đang online phản hồi
    void TriggerScan();

    // Dừng luồng quét ngầm, đóng socket UDP và giải phóng tài nguyên
    void Stop();
}
