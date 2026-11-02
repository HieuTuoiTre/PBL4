#pragma once

#include "../../Shared/Network.h"
#include "../../Shared/Protocol.h"
#include <windows.h>
#include <string>

namespace ChatWindow {
    void Show(HWND hParent, TcpClient* client);
    void Hide();
    void Toggle(HWND hParent, TcpClient* client);
    bool IsOpen();
    int GetUnreadCount();
    void ResetUnreadCount();

    // Gọi khi Manager nhận được tin nhắn từ Client qua socket Control
    void OnMessageReceived(const Protocol::ChatMessagePayload& msg);

    // Giải phóng tài nguyên khi đóng phiên kết nối
    void Cleanup();
}

