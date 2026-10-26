#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <string>
#include <functional>

namespace ConnectWindow {
    // Callback thực hiện kết nối: nhận vào IP và trả về kết quả kèm chuỗi lỗi (nếu có)
    using ConnectCallback = std::function<bool(const std::string& ip, std::string& outError)>;

    // Hiển thị giao diện nhập IP và kết nối Client
    // - hInstance: Handle instance của ứng dụng
    // - outSelectedIP: Trả về địa chỉ IP khi kết nối thành công
    // - onConnect: Callback kiểm tra kết nối với IP
    // Trả về true nếu kết nối thành công, false nếu người dùng thoát hoặc đóng cửa sổ
    bool Show(HINSTANCE hInstance, std::string& outSelectedIP, ConnectCallback onConnect);
}

