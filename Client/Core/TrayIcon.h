#pragma once
#include <windows.h> // Bắt buộc có để dùng kiểu HINSTANCE

namespace TrayIcon {
    // Khai báo hàm để ClientMain.cpp có thể nhìn thấy
    bool Initialize(HINSTANCE hInstance);
    void Cleanup();
}