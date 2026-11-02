#include "../../Shared/Network.h"
#include <windows.h>
#include <mmsystem.h>
#include <timeapi.h>
#include <thread>
#include <gdiplus.h>

#include "SessionManager.h"
#include "TrayIcon.h"
#include "DiscoveryResponder.h"
#include "../System/InputInjector.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow){

    // Đặt độ phân giải bộ định thời Windows về 1ms (chuẩn 60-120 FPS mượt mà)
    timeBeginPeriod(1);

    SetProcessDPIAware();

    HMODULE hComCtl = LoadLibraryA("comctl32.dll");
    if (hComCtl) {
        typedef void (WINAPI *INITPROC)(void);
        INITPROC pInit = (INITPROC)GetProcAddress(hComCtl, "InitCommonControls");
        if (pInit) pInit();
    }

    // Khởi tạo GDI+ cho toàn bộ tiến trình Client
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartupInput gdiInput;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, NULL);

    // --- BẬT CƠ CHẾ CHỈ CHO PHÉP 1 TIẾN TRÌNH DUY NHẤT ---
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "PBL4_RemoteDesktop_Client_Mutex");
    
    // Kiểm tra nếu thẻ bài (Mutex) này đã tồn tại trên hệ thống
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) {
            CloseHandle(hMutex);
        }
        MessageBoxW(NULL, L"Ứng dụng Client đang chạy ngầm trong khay hệ thống (System Tray)!\nHãy kiểm tra biểu tượng ở góc phải thanh Taskbar.", L"Thông báo", MB_OK | MB_ICONINFORMATION);
        Gdiplus::GdiplusShutdown(gdiToken);
        if (hComCtl) FreeLibrary(hComCtl);
        timeEndPeriod(1);
        return 0; // Lập tức tắt tiến trình mới này, giữ nguyên tiến trình cũ đang chạy
    }
    // --------------------------------------------------

    // 1. Khởi tạo Winsock
    if (!Network::Initialize()) {
        MessageBoxW(NULL, L"Không thể khởi tạo Winsock2!", L"Lỗi nghiêm trọng", MB_OK | MB_ICONERROR);
        Gdiplus::GdiplusShutdown(gdiToken);
        if (hComCtl) FreeLibrary(hComCtl);
        timeEndPeriod(1);
        if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); } // Dọn dẹp Mutex
        return 1;
    }

    // 2. Khởi tạo UDP Discovery Responder để Manager có thể tự động quét thấy máy này
    DiscoveryResponder::Start();

    // 3. Khởi tạo Cửa sổ tàng hình & Icon khay hệ thống
    if (!TrayIcon::Initialize(hInstance)) {
        MessageBoxW(NULL, L"Không thể khởi tạo System Tray Icon!", L"Lỗi nghiêm trọng", MB_OK | MB_ICONERROR);
        DiscoveryResponder::Stop();
        Network::Cleanup();
        Gdiplus::GdiplusShutdown(gdiToken);
        if (hComCtl) FreeLibrary(hComCtl);
        timeEndPeriod(1);
        if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); } // Dọn dẹp Mutex
        return 1;
    }

    // 4. Đẩy vòng lặp mạng sang một luồng (Thread) chạy ngầm riêng biệt
    std::thread networkThread([]() {
        // Cổng cơ sở 8080 (mở 8080 cho Control, 8081 cho Video, 8082 cho Audio)
        SessionManager::StartServer(Protocol::PORT_CONTROL); 
    });
    
    // Tách rời luồng này khỏi luồng chính (Main Thread)
    networkThread.detach();

    // 5. Vòng lặp sự kiện (Message Loop) của luồng chính
    MSG msg = {0};

    // GetMessage sẽ chặn luồng chính lại, chỉ chạy tiếp khi có sự kiện (ví dụ click chuột)
    // Nó chỉ trả về 0 (thoát vòng lặp) khi nhận được lệnh WM_QUIT (do ta bấm nút Thoát)
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg); // Dịch các phím gõ thành ký tự chuẩn
        DispatchMessage(&msg);  // Đẩy sự kiện về cho hàm WindowProc của TrayIcon xử lý
    }

    // 6. Dọn dẹp tài nguyên trước khi tắt hẳn ứng dụng
    TrayIcon::Cleanup();              // Xóa icon khỏi màn hình
    SessionManager::Stop();           // Đóng socket, tắt luồng mạng
    DiscoveryResponder::Stop();       // Dừng lắng nghe UDP và gửi thông báo DISCOVERY_BYE
    Network::Cleanup();               // Tắt Winsock

    // Giải phóng Mutex trước khi app tắt hoàn toàn
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    Gdiplus::GdiplusShutdown(gdiToken);
    if (hComCtl) FreeLibrary(hComCtl);

    // Khôi phục bộ định thời Windows
    timeEndPeriod(1);

    return 0;
}
