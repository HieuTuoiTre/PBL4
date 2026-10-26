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
