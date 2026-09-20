#include <windows.h>
#include "../../Shared/Network.h"

// Biến toàn cục để duy trì kết nối mạng trong suốt vòng đời cửa sổ
TcpClient client; 

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            // Thử kết nối tới Client đang chạy ở máy hiện tại, port 8080
            if (client.Connect("127.0.0.1", 8080)) {
                // Gửi tin nhắn đi
                client.SendData("Ping! Manager ket noi.", 22);
                
                // Chờ nhận phản hồi
                char buffer[256] = {0};
                client.ReceiveData(buffer, sizeof(buffer));
                
                // Hiển thị phản hồi từ Client
                MessageBoxA(hwnd, buffer, "Manager - Nhan phan hoi", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxA(hwnd, "Khong the ket noi toi Client. Hay bat Client truoc!", "Loi", MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        case WM_DESTROY:
            client.Close();
            Network::Cleanup();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    Network::Initialize();

    const wchar_t CLASS_NAME[]  = L"ManagerWindowClass";
    WNDCLASS wc = { };
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, L"LanRemote - Manager",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) { return 0; }

    ShowWindow(hwnd, nCmdShow);

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}