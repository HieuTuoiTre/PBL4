#include "SessionManager.h"
#include "../../Shared/Network.h"
#include "../../Shared/Protocol.h"
#include "../Media/ScreenCapture.h"
#include "../System/InputInjector.h"
#include "../FileTransfer/FileWorker.h"
#include "../Media/AudioCapture.h"
#include "../System/SysMonitor.h"

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
        int x, y, w, h; // Biến hứng tọa độ từ hàm Capture

        while (g_isConnected && g_isRunning) {
            // Hàm sẽ trả về False nếu màn hình đứng im (không tốn băng thông)
            if (ScreenCapture::CaptureFrame(frameBuffer, x, y, w, h)) {
                
                // Khởi tạo Header chứa tọa độ
                Protocol::VideoDeltaHeader delta = {x, y, w, h};
                
                // Nối Header tọa độ và Mảng byte JPEG thành 1 cục Payload duy nhất
                std::vector<char> packetData(sizeof(delta) + frameBuffer.size());
                memcpy(packetData.data(), &delta, sizeof(delta));
                memcpy(packetData.data() + sizeof(delta), frameBuffer.data(), frameBuffer.size());
                
                if (!g_server.SendPacket(Protocol::MSG_VIDEO_FRAME, packetData.data(), packetData.size())) {
                    g_isConnected = false;
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(14));
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
                    std::string filePath(header.size, '\0');
                    if (g_server.ReceiveExact(&filePath[0], header.size)) {
                        
                        FileWorker fileWorker;
                        if (fileWorker.OpenFileForRead(filePath)) {
                            
                            // ==========================================
                            // BƯỚC 1: BÁO CÁO THÔNG TIN FILE (MSG_FILE_INFO)
                            // ==========================================
                            Protocol::FileInfoPayload fileInfo = {0};
                            fileInfo.fileSize = fileWorker.GetFileSize();
                            
                            // Trích xuất tên file (cắt phần đuôi sau dấu gạch chéo cuối cùng)
                            std::string fileName = filePath.substr(filePath.find_last_of("\\/") + 1);
                            strncpy_s(fileInfo.fileName, fileName.c_str(), sizeof(fileInfo.fileName) - 1);
                            
                            g_server.SendPacket(Protocol::MSG_FILE_INFO, (char*)&fileInfo, sizeof(Protocol::FileInfoPayload));

                            // ==========================================
                            // BƯỚC 2: BĂM VÀ GỬI DỮ LIỆU (MSG_FILE_CHUNK)
                            // ==========================================
                            const int CHUNK_SIZE = 4096;
                            char buffer[CHUNK_SIZE];
                            
                            while (!fileWorker.IsEOF()) {
                                int bytesRead = fileWorker.ReadNextChunk(buffer, CHUNK_SIZE);
                                if (bytesRead > 0) {
                                    if (!g_server.SendPacket(Protocol::MSG_FILE_CHUNK, buffer, bytesRead)) {
                                        break;
                                    }
                                }
                            }
                            fileWorker.CloseFile();

                            // ==========================================
                            // BƯỚC 3: BÁO KẾT THÚC FILE (MSG_FILE_END)
                            // ==========================================
                            g_server.SendPacket(Protocol::MSG_FILE_END, nullptr, 0);
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

                // === THÔNG SỐ HỆ THỐNG (CPU/RAM/DISK) ===
                case Protocol::MSG_SYS_INFO_REQUEST: {
                    Protocol::SysInfoPayload sysInfo = SysMonitor::GetSystemInfo();
                    g_server.SendPacket(Protocol::MSG_SYS_INFO_RESPONSE, (char*)&sysInfo, sizeof(Protocol::SysInfoPayload));
                    break;
                }

                // === DUYỆT Ổ ĐĨA & THƯ MỤC ===
                case Protocol::MSG_DRIVE_LIST_REQUEST: {
                    std::vector<std::string> drives = FileWorker::GetDrives();
                    std::string payload = "";
                    for (const auto& d : drives) payload += d + "\n";
                    g_server.SendPacket(Protocol::MSG_DRIVE_LIST_RESPONSE, payload.c_str(), payload.size());
                    break;
                }

                case Protocol::MSG_DIR_REQUEST: {
                    std::string path(header.size, '\0');
                    if (g_server.ReceiveExact(&path[0], header.size)) {
                        std::string dirContent = FileWorker::GetDirectoryContent(path);
                        g_server.SendPacket(Protocol::MSG_DIR_RESPONSE, dirContent.c_str(), dirContent.size());
                    }
                    break;
                }

                // === QUẢN LÝ TASK MANAGER ===
                case Protocol::MSG_PROCESS_LIST_REQUEST: {
                    std::vector<Protocol::ProcessInfoPayload> pList = SysMonitor::GetProcessList();
                    uint32_t totalBytes = pList.size() * sizeof(Protocol::ProcessInfoPayload);
                    g_server.SendPacket(Protocol::MSG_PROCESS_LIST_RESPONSE, (char*)pList.data(), totalBytes);
                    break;
                }

                case Protocol::MSG_KILL_PROCESS: {
                    // Manager sẽ gửi kèm 4 byte chứa số PID của app cần diệt
                    uint32_t pidToKill = 0;
                    if (g_server.ReceiveExact((char*)&pidToKill, sizeof(uint32_t))) {
                        SysMonitor::KillProcess(pidToKill);
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