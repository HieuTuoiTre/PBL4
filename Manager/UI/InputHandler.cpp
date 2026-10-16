#include "InputHandler.h"
#include <windowsx.h>

namespace {
    TcpClient* g_client = nullptr;
    bool g_isControlling = true;
}

namespace InputHandler {
    void Initialize(TcpClient* client) {
        g_client = client;
        g_isControlling = true;
    }

    void SendMouse(uint8_t action, uint8_t btn, LPARAM lp, HWND hWnd, int frameW, int frameH, int toolbarH, int16_t wheel) {
        if (!g_client || !g_isControlling || frameW <= 0 || frameH <= 0) return;

        int mouseX = GET_X_LPARAM(lp);
        int mouseY = GET_Y_LPARAM(lp);
        if (mouseY < toolbarH) return;
        mouseY -= toolbarH;

        RECT r;
        GetClientRect(hWnd, &r);
        int drawHeight = r.bottom - toolbarH;
        if (drawHeight <= 0 || r.right <= 0) return;

        Protocol::MousePayload mp = {
            action,
            btn,
            (int16_t)(mouseX * frameW / r.right),
            (int16_t)(mouseY * frameH / drawHeight),
            wheel
        };
        g_client->SendPacket(Protocol::MSG_MOUSE_EVENT, (char*)&mp, sizeof(mp));
    }

    void SendKey(WPARAM wp, uint8_t isDown) {
        if (!g_client || !g_isControlling) return;
        Protocol::KeyboardPayload kp = { (uint32_t)wp, isDown };
        g_client->SendPacket(Protocol::MSG_KEY_EVENT, (char*)&kp, sizeof(kp));
    }

    void ToggleControlling() {
        g_isControlling = !g_isControlling;
    }

    bool IsControlling() {
        return g_isControlling;
    }
}