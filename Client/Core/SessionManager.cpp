#include "SessionManager.h"
#include "../../Shared/Network.h"
#include "../../Shared/Protocol.h"
#include "../Media/ScreenCapture.h"
#include "../System/InputInjector.h"

#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

namespace {
    TcpServer g_server;
    std::atomic<bool> g_isRunning(false);
    std::atomic<bool> g_isConnected(false);

    // ==========================================
    // LUỒNG GỬI (TX): LIÊN TỤC CHỤP & GỬI MÀN HÌNH
    // ==========================================
    void TransmitLoop() {
        if (!ScreenCapture::Initialize()) return;
        
        std::vector<char> frameBuffer;

        while (g_isConnected && g_isRunning) {
            // Chụp và nén khung hình
            if (ScreenCapture::CaptureFrame(frameBuffer)) {
                
                // Dev 1 đã viết sẵn hàm SendPacket tự bọc Header, ta chỉ việc ném dữ liệu vào
                if (!g_server.SendPacket(Protocol::MSG_VIDEO_FRAME, frameBuffer.data(), frameBuffer.size())) {
                    g_isConnected = false; // Lỗi mạng -> Thoát vòng lặp
                    break;
                }
            }
            // Ngủ ~33ms để giới hạn tốc độ truyền ở mức ~30 FPS, tránh làm quá tải mạng
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }

        ScreenCapture::Cleanup();
    }

    // ==========================================
    // LUỒNG NHẬN (RX): CHỜ LỆNH CHUỘT / BÀN PHÍM
    // ==========================================
    void ReceiveLoop() {
        while (g_isConnected && g_isRunning) {
            Protocol::PacketHeader header;
            
            // 1. Nhận 5 byte Header trước để biết đang nhận loại tin nhắn gì
            if (!g_server.ReceiveExact((char*)&header, sizeof(Protocol::PacketHeader))) {
                g_isConnected = false;
                break;
            }

            // 2. Dựa vào loại tin nhắn để bóc tách gói Payload đi kèm
            switch (header.type) {
                
                case Protocol::MSG_MOUSE_EVENT: {
                    Protocol::MousePayload mouse;
                    if (g_server.ReceiveExact((char*)&mouse, sizeof(Protocol::MousePayload))) {
                        InputInjector::InjectMouse(mouse);
                    }
                    break;
                }
                
                case Protocol::MSG_KEY_EVENT: {
                    Protocol::KeyboardPayload key;
                    if (g_server.ReceiveExact((char*)&key, sizeof(Protocol::KeyboardPayload))) {
                        InputInjector::InjectKeyboard(key);
                    }
                    break;
                }
                
                // Các lệnh khác như MSG_SYS_INFO_REQUEST sẽ được bổ sung sau...
                
                default: {
                    // Nếu nhận được gói tin không quan tâm, ta phải "đọc bỏ" phần payload 
                    // để không làm kẹt luồng byte của TCP
                    if (header.size > 0) {
                        std::vector<char> dump(header.size);
                        g_server.ReceiveExact(dump.data(), header.size);
                    }
                    break;
                }
            }
        }
    }
}

namespace SessionManager {

    void StartServer(int port) {
        g_isRunning = true;
        
        // Mở cổng 8080 để chờ Manager
        if (!g_server.Start(port)) {
            return;
        }

        // Vòng lặp chờ kết nối (Luôn mở để nếu Manager rớt mạng, họ có thể kết nối lại)
        while (g_isRunning) {
            if (g_server.AcceptConnection()) {
                g_isConnected = true;

                // Bật 2 luồng Gửi và Nhận chạy song song
                std::thread txThread(TransmitLoop);
                std::thread rxThread(ReceiveLoop);

                // Chờ cho đến khi 1 trong 2 luồng báo lỗi rớt mạng (g_isConnected = false)
                txThread.join();
                rxThread.join();
                
                // Đóng socket của Manager hiện tại để chờ Manager mới
                g_server.CloseClient(); 
            } else {
                // Nếu accept lỗi, ngủ 1 chút rồi thử lại
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }

    void Stop() {
        g_isRunning = false;
        g_isConnected = false;
        g_server.Close(); // Đá bay kết nối hiện tại để luồng Accept thoát ra
    }
}