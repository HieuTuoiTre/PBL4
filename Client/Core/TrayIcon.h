#pragma once
#include <windows.h> // Bắt buộc có để dùng kiểu HINSTANCE
#include <string>

namespace TrayIcon {
    // Khai báo hàm để ClientMain.cpp có thể nhìn thấy
    bool Initialize(HINSTANCE hInstance);
    void Cleanup();
    HWND GetHwnd();
    void SetPin(const std::string& pin);
    std::string GetPin();
    void ShowPinNotification();
}