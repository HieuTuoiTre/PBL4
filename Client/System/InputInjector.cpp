#include "InputInjector.h"
#include <windows.h>

namespace InputInjector {

    void InjectMouse(const Protocol::MousePayload& payload) {
        // Khởi tạo cấu trúc INPUT thô
        INPUT input = { 0 };
        input.type = INPUT_MOUSE;

        // Cờ mặc định: Sử dụng hệ tọa độ tuyệt đối (0 - 65535)
        input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE;

        // ==========================================
        // 1. XỬ LÝ TỌA ĐỘ KHI DI CHUYỂN
        // ==========================================
        if (payload.actionType == Protocol::MOUSE_ACTION_MOVE) {
            input.mi.dwFlags |= MOUSEEVENTF_MOVE;

            // Lấy độ phân giải màn hình hiện tại
            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            int screenHeight = GetSystemMetrics(SM_CYSCREEN);

            // Công thức quy đổi: (Tọa độ X / Chiều rộng màn hình) * 65535
            input.mi.dx = (LONG)(((float)payload.x / screenWidth) * 65535.0f);
            input.mi.dy = (LONG)(((float)payload.y / screenHeight) * 65535.0f);
        }

        // ==========================================
        // 2. XỬ LÝ CÁC NÚT BẤM (PRESS / RELEASE)
        // ==========================================
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

        // ==========================================
        // 3. XỬ LÝ CON LĂN CHUỘT (WHEEL)
        // ==========================================
        if (payload.actionType == Protocol::MOUSE_ACTION_WHEEL) {
            input.mi.dwFlags |= MOUSEEVENTF_WHEEL;
            // wheelDelta dương là lăn lên, âm là lăn xuống (thường là bội số của 120)
            input.mi.mouseData = payload.wheelDelta; 
        }

        // ==========================================
        // 4. GỬI LỆNH XUỐNG HỆ ĐIỀU HÀNH
        // ==========================================
        SendInput(1, &input, sizeof(INPUT));
    }

    void InjectKeyboard(const Protocol::KeyboardPayload& payload) {
        // Khởi tạo cấu trúc INPUT thô
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;

        // Gán mã phím ảo (Virtual-Key code). Ví dụ: phím 'A' là 0x41, phím Enter là 0x0D
        input.ki.wVk = (WORD)payload.vkCode;

        // Xử lý trạng thái Nhấn xuống (Down) hoặc Nhả ra (Up)
        // Nếu payload.isDown == 0 (false), tức là lệnh nhả phím
        if (payload.isDown == 0) {
            input.ki.dwFlags = KEYEVENTF_KEYUP;
        } else {
            input.ki.dwFlags = 0; // 0 mặc định là nhấn phím xuống (KeyDown)
        }

        // Gửi lệnh xuống hệ điều hành
        SendInput(1, &input, sizeof(INPUT));
    }

}