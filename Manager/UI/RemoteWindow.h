// ============================================================================
// File: RemoteWindow.h
// Mô tả: Khai báo giao diện cửa sổ RemoteWindow (Màn hình điều khiển và xem máy từ xa).
// Chịu trách nhiệm:
//   - Khởi tạo cửa sổ toàn màn hình hiển thị luồng hình ảnh Desktop nhận từ Client.
//   - Cung cấp thanh công cụ Toolbar hiện đại phía trên (Chế độ, Âm thanh, Task Manager, File Explorer, FPS).
//   - Giải nén luồng JPEG và kết hợp các khối khung hình chênh lệch (Video Delta Frame).
//   - Bắt các sự kiện chuột và bàn phím của người dùng rồi chuyển tiếp sang InputHandler.
// ============================================================================

#pragma once // Chỉ thị tiền xử lý ngăn chặn nạp lặp lại file header

// Nạp các lớp mạng kết nối TCP và các định nghĩa gói tin giao thức
#include "../../Shared/Network.h"
#include "../../Shared/Protocol.h"

// Nạp thư viện chuẩn Windows API
#include <windows.h>

namespace RemoteWindow {
    // Khởi tạo và hiển thị cửa sổ điều khiển từ xa RemoteWindow
    // - hInst: Handle phiên bản ứng dụng
    // - client: Con trỏ kênh TCP Điều khiển để điều phối lệnh
    // Trả về HWND của cửa sổ đã được tạo
    HWND Create(HINSTANCE hInst, TcpClient* client);

    // Tiếp nhận khối dữ liệu ảnh JPEG của vùng thay đổi (Delta Frame) và giải nén vẽ đè lên khung hình hiện tại
    void UpdateFrame(const Protocol::VideoDeltaHeader& delta, const char* jpegData, int jpegSize);

    // Giải phóng toàn bộ tài nguyên đồ họa (GDI+ Bitmap, IStream COM, Critical Section) khi đóng phiên
    void Cleanup();
}
