#pragma once

#include "../../Shared/Network.h"

#include "../../Shared/Protocol.h"

#include <windows.h>

namespace InputHandler {

    void Initialize(TcpClient* client);

    void SendMouse(uint8_t action, uint8_t btn, LPARAM lp, HWND hWnd, int frameW, int frameH, int toolbarH, int16_t wheel = 0);

    void SendKey(WPARAM wp, uint8_t isDown);
    void ReleaseModifiers();
    void ToggleControlling();

    bool IsControlling();
}

