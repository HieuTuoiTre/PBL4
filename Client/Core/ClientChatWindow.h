#pragma once

#include "../../Shared/Network.h"
#include "../../Shared/Protocol.h"
#include <windows.h>
#include <string>

namespace ClientChatWindow {
    // Khởi tạo cửa sổ chat với socket điều khiển máy chủ (Control Server)
    void Initialize(TcpServer* controlServer);

    // Hiển thị cửa sổ chat
    void Show();

    // Ẩn cửa sổ chat
    void Hide();

    // Chuyển đổi trạng thái đóng/mở cửa sổ chat
    void Toggle();

    // Kiểm tra cửa sổ chat đang mở hay đóng
    bool IsOpen();

    // Nhận tin nhắn gửi từ Manager qua kênh Control
    void OnMessageReceived(const Protocol::ChatMessagePayload& msg);

    // Đóng và giải phóng tài nguyên cửa sổ chat khi kết thúc phiên kết nối
    void Close();
}

