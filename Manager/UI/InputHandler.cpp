#include "InputHandler.h"

#include <windowsx.h>

namespace InputHandler {
    static TcpClient* s_client = nullptr;
    static bool s_isControlling = true;

    void Initialize(TcpClient* client) {
        s_client = client;
        s_isControlling = true;
    }

    void SendMouse(uint8_t action, uint8_t btn, LPARAM lp, HWND hWnd, int frameW, int frameH, int toolbarH, int16_t wheel) {
        if (!s_client || !s_isControlling || frameW <= 0 || frameH <= 0) return;

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
        s_client->SendPacket(Protocol::MSG_MOUSE_EVENT, (char*)&mp, sizeof(mp));
    }

    static int16_t s_lastSentX = -1;
    static int16_t s_lastSentY = -1;
    static uint64_t s_lastMoveTime = 0;

    void SendMouseDirect(uint8_t action, uint8_t btn, int16_t x, int16_t y, int16_t wheel) {
        if (!s_client || !s_isControlling) return;

        if (action == Protocol::MOUSE_ACTION_MOVE) {
            if (x == s_lastSentX && y == s_lastSentY) return;
            uint64_t now = GetTickCount64();
            if (now - s_lastMoveTime < 8) return; // Giới hạn tối đa 125 Hz cho mouse move để chống nghẽn queue
            s_lastMoveTime = now;
            s_lastSentX = x;
            s_lastSentY = y;
        }

        Protocol::MousePayload mp = { action, btn, x, y, wheel };
        s_client->SendPacket(Protocol::MSG_MOUSE_EVENT, (char*)&mp, sizeof(mp));
    }

    void SendKey(WPARAM wp, uint8_t isDown) {
        if (!s_client || !s_isControlling) return;
        // Khóa phím Windows: Không gửi phím Windows (VK_LWIN, VK_RWIN) xuống Client để tránh kẹt phím
        if (wp == VK_LWIN || wp == VK_RWIN) {
            if (isDown != 0) return;
        }
        Protocol::KeyboardPayload kp = { (uint32_t)wp, isDown };
        s_client->SendPacket(Protocol::MSG_KEY_EVENT, (char*)&kp, sizeof(kp));
    }

    void ReleaseModifiers() {
        if (!s_client || !s_isControlling) return;
        SendKey(VK_CONTROL, 0);
        SendKey(VK_MENU, 0);
        SendKey(VK_SHIFT, 0);
        SendKey(VK_LWIN, 0);
        SendKey(VK_RWIN, 0);
    }

    void ToggleControlling() {
        s_isControlling = !s_isControlling;
    }

    bool IsControlling() {
        return s_isControlling;
    }
}
