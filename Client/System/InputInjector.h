#pragma once
#include "../../Shared/Protocol.h" // Nhúng file chứa cấu trúc MousePayload

namespace InputInjector {
    // Hàm nhận payload từ mạng và biến nó thành thao tác chuột thực tế trên Windows
    void InjectMouse(const Protocol::MousePayload& payload);

    void InjectKeyboard(const Protocol::KeyboardPayload& payload);

    // Xử lý các thao tác và phím tắt đặc biệt (Ctrl+Alt+Del, Windows Key, Lock)
    void InjectSpecialAction(uint8_t actionType);

    // Tự động giải phóng toàn bộ phím bổ trợ (Ctrl, Alt, Shift, Win) để chống kẹt phím
    void ReleaseAllModifiers();
}
