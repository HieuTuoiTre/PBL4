#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

class TcpClient;

namespace Dialogs {

enum class DialogResult {
    Ok,
    Cancel,
    Retry
};

// Đăng ký lớp cửa sổ Dialog
bool Register(HINSTANCE hInstance);

// Hộp thoại xác nhận kết nối
bool ShowConfirm(HWND parent, const std::wstring& ip, const std::wstring& machineName);

// Hộp thoại đang kết nối (hiển thị loading marquee, thông tin IP và nút Hủy)
bool ShowConnecting(HWND parent, const std::wstring& ip, const std::wstring& machineName, TcpClient*& outClient);

// Hộp thoại báo lỗi kết nối
DialogResult ShowError(HWND parent, const std::wstring& errorMessage, bool allowRetry = true);

// Hộp thoại thông báo cảnh báo (VD: chưa nhập IP)
void ShowWarning(HWND parent, const std::wstring& warningMessage);

// Hộp thoại thông báo thông tin (VD: mất kết nối đột ngột)
void ShowInfo(HWND parent, const std::wstring& title, const std::wstring& infoMessage);

} // namespace Dialogs

