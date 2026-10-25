// ============================================================================
// File: TaskManagerWindow.h
// Mô tả: Khai báo giao diện cửa sổ TaskManagerWindow (Quản lý tiến trình và tài nguyên Client từ xa).
// Chịu trách nhiệm:
//   - Khởi tạo và hiển thị Dashboard giám sát phần cứng (CPU, RAM, Đĩa) và bảng tiến trình từ xa.
//   - Tiếp nhận dữ liệu thông số hệ thống định kỳ (SysInfoPayload) và cập nhật thẻ chỉ số.
//   - Tiếp nhận danh sách tiến trình (ProcessInfoPayload) và làm mới bảng dữ liệu SysListView32.
// ============================================================================

#pragma once // Chỉ thị tiền xử lý ngăn chặn nạp trùng lặp header

// Nạp các lớp mạng kết nối TCP và các cấu trúc dữ liệu gói tin giao thức
#include "../../Shared/Network.h"
#include "../../Shared/Protocol.h"

// Nạp thư viện chuẩn Windows API
#include <windows.h>

namespace TaskManagerWindow {
    // Hiển thị hoặc đưa cửa sổ Task Manager lên phía trước (Foreground)
    // - hParent: Handle của cửa sổ cha (RemoteWindow)
    // - client: Con trỏ kênh TCP Điều khiển để gửi yêu cầu làm mới hoặc tắt tiến trình
    void Show(HWND hParent, TcpClient* client);

    // Cập nhật các chỉ số phần cứng (CPU %, RAM %, Disk %) lên Dashboard phía trên
    void UpdateSysInfo(const Protocol::SysInfoPayload& sys);

    // Cập nhật toàn bộ danh sách tiến trình đang thực thi từ xa vào bảng hiển thị
    void UpdateProcessList(const Protocol::ProcessInfoPayload* pList, int count);
}
