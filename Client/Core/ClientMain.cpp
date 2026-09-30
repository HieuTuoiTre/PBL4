#include "../../Shared/Network.h"
#include <windows.h>
#include <thread>

#include "SessionManager.h"
#include "TrayIcon.h"

#include "../System/InputInjector.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow){

    SetProcessDPIAware();
    // --- BẬT CƠ CHẾ CHỈ CHO PHÉP 1 TIẾN TRÌNH DUY NHẤT ---
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "PBL4_RemoteDesktop_Client_Mutex");
    
    // Kiểm tra nếu thẻ bài (Mutex) này đã tồn tại trên hệ thống
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
        }
        return 0; // Lập tức tắt tiến trình mới này, giữ nguyên tiến trình cũ đang chạy
    }
    // --------------------------------------------------

    // 1. Khởi tạo Winsock (Gọi hàm do Dev 1 viết)
    if (!Network::Initialize()) {
        MessageBoxW(NULL, L"Không thể khởi tạo Winsock2!", L"Lỗi nghiêm trọng", MB_OK | MB_ICONERROR);
        if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); } // Dọn dẹp Mutex
        return 1;
    }

    // 2. Khởi tạo Cửa sổ tàng hình & Icon khay hệ thống
    if (!TrayIcon::Initialize(hInstance)) {
        MessageBoxW(NULL, L"Không thể khởi tạo System Tray Icon!", L"Lỗi nghiêm trọng", MB_OK | MB_ICONERROR);
        Network::Cleanup();
        if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); } // Dọn dẹp Mutex
        return 1;
    }

    // 3. Đẩy vòng lặp mạng sang một luồng (Thread) chạy ngầm riêng biệt
    std::thread networkThread([]() {
        // Cổng 8080. Hàm này chứa vòng lặp vô hạn while(true) chờ kết nối.
        SessionManager::StartServer(8080); 
    });
    
    // Tách rời luồng này khỏi luồng chính (Main Thread)
    networkThread.detach();

    // 4. Vòng lặp sự kiện (Message Loop) của luồng chính
    MSG msg = {0};

    // GetMessage sẽ chặn luồng chính lại, chỉ chạy tiếp khi có sự kiện (ví dụ click chuột)
    // Nó chỉ trả về 0 (thoát vòng lặp) khi nhận được lệnh WM_QUIT (do ta bấm nút Thoát)
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg); // Dịch các phím gõ thành ký tự chuẩn
        DispatchMessage(&msg);  // Đẩy sự kiện về cho hàm WindowProc của TrayIcon xử lý
    }

    // 5. Dọn dẹp tài nguyên trước khi tắt hẳn ứng dụng
    TrayIcon::Cleanup();      // Xóa icon khỏi màn hình
    SessionManager::Stop();   // Đóng socket, tắt luồng mạng
    Network::Cleanup();       // Tắt Winsock

    // Giải phóng Mutex trước khi app tắt hoàn toàn
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return 0;
}