// ============================================================================
// File: InputHandler.h
// Mô tả: Khai báo giao diện của module InputHandler (Bộ xử lý và gửi tương tác chuột, bàn phím).
// Chịu trách nhiệm:
//   - Thu nhận các sự kiện chuột (Click, Move, Scroll) và phím bấm trên cửa sổ RemoteWindow.
//   - Chuẩn hóa tọa độ chuột theo tỷ lệ kích thước màn hình máy Client từ xa.
//   - Đóng gói dữ liệu sự kiện thành các gói tin giao thức mạng và truyền qua kênh TCP Điều khiển.
//   - Hỗ trợ chuyển đổi chế độ "Đang điều khiển" (cho phép thao tác) và "Chỉ xem" (khóa thao tác).
// ============================================================================

#pragma once // Chỉ thị tiền xử lý đảm bảo file header chỉ được nạp một lần duy nhất trong quá trình biên dịch

// Nạp định nghĩa đối tượng mạng TcpClient để truyền gói tin điều khiển
#include "../../Shared/Network.h"

// Nạp các hằng số, mã định danh sự kiện và cấu trúc dữ liệu giao thức (MousePayload, KeyboardPayload)
#include "../../Shared/Protocol.h"

// Nạp thư viện chuẩn Windows API (chứa các kiểu dữ liệu LPARAM, WPARAM, HWND, RECT)
#include <windows.h>

namespace InputHandler {
    // Khởi tạo bộ xử lý sự kiện tương tác, liên kết với kênh TCP Điều khiển (8080)
    void Initialize(TcpClient* client);

    // Chuẩn hóa tọa độ chuột và gửi gói tin sự kiện chuột (di chuyển, nhấn nút, cuộn bánh xe) sang Client
    void SendMouse(uint8_t action, uint8_t btn, LPARAM lp, HWND hWnd, int frameW, int frameH, int toolbarH, int16_t wheel = 0);

    // Đóng gói và gửi sự kiện bàn phím (nhấn phím hoặc nhả phím) sang Client
    void SendKey(WPARAM wp, uint8_t isDown);

    // Đảo trạng thái giữa "Đang điều khiển" (cho phép gửi input) và "Chỉ xem" (chặn toàn bộ input)
    void ToggleControlling();

    // Kiểm tra trạng thái hiện tại xem Manager có đang được quyền tương tác điều khiển máy Client hay không
    bool IsControlling();
}
