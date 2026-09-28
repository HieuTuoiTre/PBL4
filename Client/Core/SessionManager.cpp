#include "SessionManager.h"
#include "../../Shared/Network.h"
#include "../../Shared/Protocol.h"
#include "../Media/ScreenCapture.h"
#include "../System/InputInjector.h"
#include "../FileTransfer/FileWorker.h"
#include "../Media/AudioCapture.h"

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

                case Protocol::MSG_FILE_DOWNLOAD_REQ: {
                    // 1. Lấy đường dẫn file cần tải (Manager gửi chuỗi string, header.size chính là độ dài chuỗi)
                    std::string filePath(header.size, '\0');
                    if (g_server.ReceiveExact(&filePath[0], header.size)) {
                        
                        FileWorker fileWorker;
                        
                        // 2. Mở file để đọc
                        if (fileWorker.OpenFileForRead(filePath)) {
                            
                            // (Tùy chọn) Gửi trước 1 gói tin chứa fileWorker.GetFileSize() để Manager hiện thanh tiến trình %

                            const int CHUNK_SIZE = 4096; // Chia mỗi gói 4KB để mạng không bị nghẽn
                            char buffer[CHUNK_SIZE];
                            
                            // 3. Đọc và gửi cho đến khi hết file (EOF)[cite: 9, 10]
                            while (!fileWorker.IsEOF()) {
                                int bytesRead = fileWorker.ReadNextChunk(buffer, CHUNK_SIZE); //[cite: 9, 10]
                                
                                if (bytesRead > 0) {
                                    // Gửi mảnh file này qua mạng
                                    if (!g_server.SendPacket(Protocol::MSG_FILE_CHUNK, buffer, bytesRead)) {
                                        // Rớt mạng giữa chừng thì ngắt vòng lặp
                                        break;
                                    }
                                }
                            }
                            // 4. Đóng file để giải phóng tài nguyên hệ điều hành[cite: 9, 10]
                            fileWorker.CloseFile();
                        } else {
                            // Tùy chọn: Gửi 1 gói tin báo lỗi MSG_FILE_ERROR về cho Manager nếu file không tồn tại
                        }
                    }
                    break;
                }

                // === KIỂM TRA ĐƯỜNG TRUYỀN ===
                case Protocol::MSG_PING: {
                    // Manager gửi PING, ta trả lời PONG[cite: 11] ngay lập tức
                    g_server.SendPacket(Protocol::MSG_PONG, nullptr, 0);
                    break;
                }

                case Protocol::MSG_DISCONNECT: {
                    // Manager chủ động ngắt kết nối[cite: 11]
                    g_isConnected = false; // Thoát vòng lặp RX/TX một cách êm đẹp
                    break;
                }

                // === QUẢN LÝ TASK MANAGER ===
                case Protocol::MSG_PROCESS_LIST_REQUEST: {
                    // Cần gọi hàm lấy danh sách tiến trình từ SysMonitor (Sẽ code tiếp theo)
                    // ...
                    break;
                }

                case Protocol::MSG_KILL_PROCESS: {
                    // Manager sẽ gửi kèm 4 byte chứa số PID của app cần diệt
                    uint32_t pidToKill = 0;
                    if (g_server.ReceiveExact((char*)&pidToKill, sizeof(uint32_t))) {
                        // Cần gọi hàm diệt tiến trình từ SysMonitor (Sẽ code tiếp theo)
                        // ...
                    }
                    break;
                }
                
                
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

                AudioCapture::Start([](const char* buffer, int size) {
                g_server.SendPacket(Protocol::MSG_AUDIO_CHUNK, buffer, size);
            });

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
        AudioCapture::Stop();
        g_isRunning = false;
        g_isConnected = false;
        g_server.Close(); // Đá bay kết nối hiện tại để luồng Accept thoát ra
    }
}