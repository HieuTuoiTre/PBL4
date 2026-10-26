#include "InputInjector.h"
#include <windows.h>

namespace InputInjector {

    void ReleaseAllModifiers() {
        WORD keys[] = { VK_LWIN, VK_RWIN, VK_CONTROL, VK_LCONTROL, VK_RCONTROL,
                        VK_MENU, VK_LMENU, VK_RMENU, VK_SHIFT, VK_LSHIFT, VK_RSHIFT };
        INPUT inputs[11] = {};
        for (int i = 0; i < 11; i++) {
            inputs[i].type = INPUT_KEYBOARD;
            inputs[i].ki.wVk = keys[i];
            inputs[i].ki.dwFlags = KEYEVENTF_KEYUP;
            if (keys[i] == VK_LWIN || keys[i] == VK_RWIN || keys[i] == VK_RCONTROL || keys[i] == VK_RMENU) {
                inputs[i].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            }
        }
        SendInput(11, inputs, sizeof(INPUT));
    }

    void InjectMouse(const Protocol::MousePayload& payload) {
        INPUT input = { 0 };
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE;

        // 1. XỬ LÝ KHI DI CHUYỂN CHUỘT
        if (payload.actionType == Protocol::MOUSE_ACTION_MOVE) {
            input.mi.dwFlags |= MOUSEEVENTF_MOVE;

            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            int screenHeight = GetSystemMetrics(SM_CYSCREEN);

            if (screenWidth > 1 && screenHeight > 1) {
                input.mi.dx = (LONG)(((float)payload.x / (screenWidth - 1)) * 65535.0f);
                input.mi.dy = (LONG)(((float)payload.y / (screenHeight - 1)) * 65535.0f);
            }
        }

        // 2. XỬ LÝ KHI CLICK CHUỘT (PRESS / RELEASE)
        if (payload.actionType == Protocol::MOUSE_ACTION_PRESS) {
            if (payload.buttonType == Protocol::BUTTON_LEFT)   input.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
            if (payload.buttonType == Protocol::BUTTON_RIGHT)  input.mi.dwFlags |= MOUSEEVENTF_RIGHTDOWN;
            if (payload.buttonType == Protocol::BUTTON_MIDDLE) input.mi.dwFlags |= MOUSEEVENTF_MIDDLEDOWN;
        }
        else if (payload.actionType == Protocol::MOUSE_ACTION_RELEASE) {
            if (payload.buttonType == Protocol::BUTTON_LEFT)   input.mi.dwFlags |= MOUSEEVENTF_LEFTUP;
            if (payload.buttonType == Protocol::BUTTON_RIGHT)  input.mi.dwFlags |= MOUSEEVENTF_RIGHTUP;
            if (payload.buttonType == Protocol::BUTTON_MIDDLE) input.mi.dwFlags |= MOUSEEVENTF_MIDDLEUP;
        }

        // 3. XỬ LÝ CON LĂN CHUỘT (WHEEL)
        if (payload.actionType == Protocol::MOUSE_ACTION_WHEEL) {
            input.mi.dwFlags |= MOUSEEVENTF_WHEEL;
            input.mi.mouseData = payload.wheelDelta; 
        }

        SendInput(1, &input, sizeof(INPUT));
    }

    void InjectKeyboard(const Protocol::KeyboardPayload& payload) {
        // Khóa hoàn toàn phím Windows (VK_LWIN, VK_RWIN) để chống kẹt phím
        if (payload.vkCode == VK_LWIN || payload.vkCode == VK_RWIN) {
            // Chặn hoàn toàn lệnh nhấn xuống (keydown), chỉ cho phép lệnh nhả (keyup) nếu có
            if (payload.isDown != 0) {
                return;
            }
        }

        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = (WORD)payload.vkCode;

        DWORD flags = 0;
        if (payload.isDown == 0) {
            flags |= KEYEVENTF_KEYUP;
        }

        // Bổ sung cờ KEYEVENTF_EXTENDEDKEY cho các phím điều hướng và phím đặc biệt trên Windows
        switch (payload.vkCode) {
            case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
            case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
            case VK_INSERT: case VK_DELETE:
            case VK_RCONTROL: case VK_RMENU:
            case VK_LWIN: case VK_RWIN:
                flags |= KEYEVENTF_EXTENDEDKEY;
                break;
        }

        input.ki.dwFlags = flags;
        SendInput(1, &input, sizeof(INPUT));
    }
}
