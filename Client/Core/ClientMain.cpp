#include <windows.h>
#include "../Shared/Network.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Khởi tạo thư viện mạng
    Network::Initialize();

    TcpServer server;
    // Khởi động server ở port 8080
    if (server.Start(8080)) {
        // Hàm AcceptClient() sẽ chặn luồng, ứng dụng sẽ chạy ngầm và đợi ở đây cho đến khi có người kết nối
        if (server.AcceptClient()) {
            char buffer[256] = {0};
            server.ReceiveData(buffer, sizeof(buffer));
            
            // Hiện hộp thoại hiển thị tin nhắn nhận được từ Manager
            MessageBoxA(NULL, buffer, "Client - Nhan tin nhan", MB_OK | MB_ICONINFORMATION);
            
            // Gửi phản hồi lại cho Manager
            server.SendData("Pong! Client da nhan.", 21);
        }
    }

    server.Close();
    Network::Cleanup();
    return 0;
}