// ============================================================================
// File: ConnectWindow.h
// Mô tả: Khai báo giao diện cửa sổ ConnectWindow (Bảng điều khiển quét và chọn máy Client LAN).
// Chịu trách nhiệm:
//   - Khởi tạo giao diện đồ họa hiện đại quản lý kết nối trước khi vào phiên làm việc.
//   - Tích hợp module quét UDP Broadcast để tự động liệt kê các máy Client đang trực tuyến.
//   - Cho phép chọn máy trong danh sách hoặc nhập địa chỉ IP thủ công.
//   - Định nghĩa hàm callback `ConnectCallback` kiểm tra và xác lập 3 kết nối TCP.
// ============================================================================

#pragma once // Chỉ thị tiền xử lý ngăn chặn nạp trùng lặp header

// Giảm thiểu các thư viện không cần thiết khi nạp windows.h nhằm tăng tốc độ biên dịch
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Nạp Winsock2 trước Windows.h để tránh xung đột định nghĩa socket
#include <winsock2.h>
#include <windows.h>

// Nạp các thư viện chuẩn C++ cho chuỗi ký tự và con trỏ hàm đa năng
#include <string>
#include <functional>

namespace ConnectWindow {
    // Kiểu dữ liệu callback thực hiện kiểm tra và thiết lập kết nối:
    // Nhận vào IP mục tiêu và biến outError để lưu thông điệp lỗi nếu kết nối thất bại
    using ConnectCallback = std::function<bool(const std::string& ip, std::string& outError)>;

    // Hiển thị giao diện Dashboard kết nối Client:
    // - hInstance: Handle định danh phiên bản của ứng dụng trong bộ nhớ
    // - outSelectedIP: Biến tham chiếu nhận địa chỉ IP Client được chọn khi kết nối thành công
    // - onConnect: Hàm callback thực hiện việc bắt tay 3 kênh TCP (Control, Video, Audio)
    // Trả về true nếu người dùng chọn và kết nối thành công, false nếu đóng cửa sổ hoặc thoát ứng dụng
    bool Show(HINSTANCE hInstance, std::string& outSelectedIP, ConnectCallback onConnect);
}
