#pragma once
#include "../../Shared/Protocol.h" // Nhúng file chứa cấu trúc MousePayload

namespace InputInjector {
    // Hàm nhận payload từ mạng và biến nó thành thao tác chuột thực tế trên Windows
    void InjectMouse(const Protocol::MousePayload& payload);

    void InjectKeyboard(const Protocol::KeyboardPayload& payload);
}