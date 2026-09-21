#include <windows.h>
#include <thread>

#include "../../Shared/Network.h"
#include "SessionManager.h"
#include "TrayIcon.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow){
   // 1. Khởi tạo Winsock (Gọi hàm do Dev 1 viết)
    if (!Network::Initialize()) {
        MessageBoxW(NULL, L"Không thể khởi tạo Winsock2!", L"Lỗi nghiêm trọng", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 2. Khởi tạo Cửa sổ tàng hình & Icon khay hệ thống
    if (!TrayIcon::Initialize(hInstance)) {
        MessageBoxW(NULL, L"Không thể khởi tạo System Tray Icon!", L"Lỗi nghiêm trọng", MB_OK | MB_ICONERROR);
        Network::Cleanup();
        return 1;
    }
}