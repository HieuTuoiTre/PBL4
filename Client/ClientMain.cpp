#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Ứng dụng Client chạy ngầm, không tạo cửa sổ hiển thị.
    // Tạm thời dùng vòng lặp vô hạn để giữ app chạy.
    
    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}