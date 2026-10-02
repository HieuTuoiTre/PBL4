#pragma once
#include <windows.h>
#include <string>

namespace MainWindow {

constexpr wchar_t kWindowClassName[] = L"RemoteManagerWindow";

bool Register(HINSTANCE hInstance);
HWND Create(HINSTANCE hInstance, int nCmdShow);
void Show(bool bShow = true);
HWND GetHWND();

// Các hàm quản lý danh sách thiết bị động
void AddDevice(const std::wstring& id, const std::wstring& name, const std::wstring& ip, const std::wstring& status);
void ClearDevices();

} // namespace MainWindow

