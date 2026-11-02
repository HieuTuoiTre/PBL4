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

    void OnMessageReceived(const Protocol::ChatMessagePayload& msg);

    void Cleanup();
}

