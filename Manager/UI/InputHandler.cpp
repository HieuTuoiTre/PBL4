#include "InputHandler.h"
#include "../Core/RfbClientSession.h"
#include "../../Shared/RFB/RfbDefines.h"

#include <windowsx.h>

namespace InputHandler {
    static TcpClient* s_client = nullptr;
    static bool s_isControlling = true;
    static uint8_t s_rfbButtonMask = 0;

    void Initialize(TcpClient* client) {
        s_client = client;
        s_isControlling = true;
        s_rfbButtonMask = 0;
    }

    void SendMouse(uint8_t action, uint8_t btn, LPARAM lp, HWND hWnd, int frameW, int frameH, int toolbarH, int16_t wheel) {
        if (!s_isControlling || frameW <= 0 || frameH <= 0) return;

        int mouseX = GET_X_LPARAM(lp);
        int mouseY = GET_Y_LPARAM(lp);

        if (mouseY < toolbarH) return;

        mouseY -= toolbarH;

        RECT r;
        GetClientRect(hWnd, &r);
        int drawHeight = r.bottom - toolbarH;

        if (drawHeight <= 0 || r.right <= 0) return;

        int16_t targetX = (int16_t)(mouseX * frameW / r.right);
        int16_t targetY = (int16_t)(mouseY * frameH / drawHeight);

        SendMouseDirect(action, btn, targetX, targetY, wheel);
    }

    static int16_t s_lastSentX = -1;
    static int16_t s_lastSentY = -1;
    static uint64_t s_lastMoveTime = 0;

    void SendMouseDirect(uint8_t action, uint8_t btn, int16_t x, int16_t y, int16_t wheel) {
        if (!s_isControlling) return;

        if (action == Protocol::MOUSE_ACTION_MOVE) {
            if (x == s_lastSentX && y == s_lastSentY) return;
            uint64_t now = GetTickCount64();
            if (now - s_lastMoveTime < 8) return; // Giới hạn tối đa 125 Hz cho mouse move để chống nghẽn queue
            s_lastMoveTime = now;
            s_lastSentX = x;
            s_lastSentY = y;
        }

        // Ưu tiên truyền tải qua chuẩn giao thức VNC / RFB 3.8
        if (RfbClient::IsConnected()) {
            if (action == Protocol::MOUSE_ACTION_PRESS) {
                if (btn == Protocol::BUTTON_LEFT) s_rfbButtonMask |= Rfb::BUTTON_LEFT;
                else if (btn == Protocol::BUTTON_RIGHT) s_rfbButtonMask |= Rfb::BUTTON_RIGHT;
                else if (btn == Protocol::BUTTON_MIDDLE) s_rfbButtonMask |= Rfb::BUTTON_MIDDLE;
            } else if (action == Protocol::MOUSE_ACTION_RELEASE) {
                if (btn == Protocol::BUTTON_LEFT) s_rfbButtonMask &= ~Rfb::BUTTON_LEFT;
                else if (btn == Protocol::BUTTON_RIGHT) s_rfbButtonMask &= ~Rfb::BUTTON_RIGHT;
                else if (btn == Protocol::BUTTON_MIDDLE) s_rfbButtonMask &= ~Rfb::BUTTON_MIDDLE;
            }

            uint8_t mask = s_rfbButtonMask;
            if (action == Protocol::MOUSE_ACTION_WHEEL && wheel != 0) {
                mask |= (wheel > 0 ? Rfb::WHEEL_UP : Rfb::WHEEL_DOWN);
            }

            uint16_t rfbX = (uint16_t)(x < 0 ? 0 : x);
            uint16_t rfbY = (uint16_t)(y < 0 ? 0 : y);
            RfbClient::SendPointer(mask, rfbX, rfbY);
        }
    }

    void SendKey(WPARAM wp, uint8_t isDown) {
        if (!s_isControlling) return;

        // Khóa phím Windows: Không gửi phím Windows (VK_LWIN, VK_RWIN) xuống Client để tránh kẹt phím
        if (wp == VK_LWIN || wp == VK_RWIN) {
            if (isDown != 0) return;
        }

        // Truyền tải qua chuẩn giao thức VNC / RFB 3.8
        if (RfbClient::IsConnected()) {
            bool isShift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            RfbClient::SendKey(wp, isDown != 0, isShift);
        }
    }

    void ReleaseModifiers() {
        if (!s_isControlling) return;
        SendKey(VK_CONTROL, 0);
        SendKey(VK_MENU, 0);
        SendKey(VK_SHIFT, 0);
        SendKey(VK_LWIN, 0);
        SendKey(VK_RWIN, 0);
        s_rfbButtonMask = 0;
    }

    void ToggleControlling() {
        s_isControlling = !s_isControlling;
    }

    bool IsControlling() {
        return s_isControlling;
    }
}
