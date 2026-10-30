#include "../../Shared/Network.h"
#include "SessionManager.h"
#include "../../Shared/Protocol.h"
#include "../Media/ScreenCapture.h"
#include "../System/InputInjector.h"
#include "../FileTransfer/FileWorker.h"
#include "../Media/AudioCapture.h"
#include "../System/SysMonitor.h"
#include "StatusOverlay.h"
#include "ConnectionAlert.h"
#include "DiscoveryResponder.h"
#include "ClientChatWindow.h"
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <cstring>

namespace {
    TcpServer g_controlServer;
    TcpServer g_videoServer;
    TcpServer g_audioServer;

    StatusOverlay g_overlay;
    std::atomic<uint64_t> g_lastInputTimeMs(0);

    std::atomic<bool> g_isRunning(false);
    std::atomic<bool> g_isConnected(false);

    // ==========================================
    // LUỒNG GỬI VIDEO (TX): LIÊN TỤC CHỤP & GỬI MÀN HÌNH QUA CỔNG VIDEO (8081)
    // ==========================================
    void TransmitVideoLoop() {
        if (!ScreenCapture::Initialize()) return;
        
        std::vector<char> frameBuffer;
        frameBuffer.reserve(512 * 1024); // Cấp phát sẵn 512KB tránh cấp phát lại
        int x, y, w, h;

        // Chu kỳ chuẩn xác cho 60.0 FPS = 16,666 microsecond (16.666 ms)
        constexpr std::chrono::microseconds FRAME_TARGET_TIME(16666);
        auto nextFrameTime = std::chrono::steady_clock::now();

        while (g_isConnected && g_isRunning) {
            nextFrameTime += FRAME_TARGET_TIME;

            // Chống dồn hàng đợi (TCP Flow-Control): Nếu socket đang bận thì bỏ qua khung
            if (!g_videoServer.CanSendWithoutBlocking()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                auto now = std::chrono::steady_clock::now();
                if (now > nextFrameTime + FRAME_TARGET_TIME) {
                    nextFrameTime = now;
                }
                continue;
            }

            if (ScreenCapture::CaptureFrame(frameBuffer, x, y, w, h)) {
                // frameBuffer đã chứa sẵn [VideoDeltaHeader + JPEG Data], gửi thẳng qua socket video!
                auto tSendStart = std::chrono::steady_clock::now();
                if (!g_videoServer.SendPacket(Protocol::MSG_VIDEO_FRAME, frameBuffer.data(), (int)frameBuffer.size())) {
                    g_isConnected = false;
                    break;
                }
                auto tSendEnd = std::chrono::steady_clock::now();
                double sendMs = std::chrono::duration<double, std::milli>(tSendEnd - tSendStart).count();
                ScreenCapture::ReportSendTime(sendMs);
            }

            // Bộ giữ nhịp độ chính xác cao (High-Precision Frame Pacer chuẩn 60.0 FPS)
            auto now = std::chrono::steady_clock::now();
            if (nextFrameTime > now) {
                auto remainingUs = std::chrono::duration_cast<std::chrono::microseconds>(nextFrameTime - now).count();
                // Nếu còn nhiều hơn 2.5ms, ngủ nhẹ để nhường CPU cho các luồng khác
                if (remainingUs > 2500) {
                    std::this_thread::sleep_for(std::chrono::microseconds(remainingUs - 2000));
                }
                // Trong 2ms cuối cùng, spin-wait kết hợp yield để căn trúng đích 60.0 FPS chuẩn xác
                while (std::chrono::steady_clock::now() < nextFrameTime) {
                    std::this_thread::yield();
                }
            } else {
                if (now - nextFrameTime > FRAME_TARGET_TIME * 2) {
                    nextFrameTime = now;
                }
            }
        }
        ScreenCapture::Cleanup();
    }

    // ==========================================
    // LUỒNG NHẬN CONTROL (RX): CHỜ LỆNH CHUỘT / BÀN PHÍM / HỆ THỐNG / FILE QUA CỔNG CONTROL (8080)
    // ==========================================
    void ReceiveControlLoop() {
        while (g_isConnected && g_isRunning) {
            Protocol::PacketHeader header;
            
            if (!g_controlServer.ReceiveExact((char*)&header, sizeof(Protocol::PacketHeader))) {
                g_isConnected = false;
                break;
            }

            switch (header.type) {
                
                case Protocol::MSG_MOUSE_EVENT: {
                    Protocol::MousePayload mouse;
                    if (g_controlServer.ReceiveExact((char*)&mouse, sizeof(Protocol::MousePayload))) {
                        InputInjector::InjectMouse(mouse);
                        g_overlay.SetState(OverlayState::ConnectedControlling);
                        g_lastInputTimeMs = GetTickCount64();
                    }
                    break;
                }
                
                case Protocol::MSG_KEY_EVENT: {
                    Protocol::KeyboardPayload key;
                    if (g_controlServer.ReceiveExact((char*)&key, sizeof(Protocol::KeyboardPayload))) {
                        InputInjector::InjectKeyboard(key);
                        g_overlay.SetState(OverlayState::ConnectedControlling);
                        g_lastInputTimeMs = GetTickCount64();
                    }
                    break;
                }

                case Protocol::MSG_FILE_DOWNLOAD_REQ: {
                    std::string filePath(header.size, '\0');
                    if (g_controlServer.ReceiveExact(&filePath[0], header.size)) {
                        // Tách sang luồng riêng để không chặn luồng điều khiển chuột/phím
                        std::thread([filePath]() {
                            FileWorker fileWorker;
                            if (fileWorker.OpenFileForRead(filePath)) {
                                Protocol::FileInfoPayload fileInfo = {0};
                                fileInfo.fileSize = fileWorker.GetFileSize();
                                std::string fileName = filePath.substr(filePath.find_last_of("\\/") + 1);
                                strncpy_s(fileInfo.fileName, fileName.c_str(), sizeof(fileInfo.fileName) - 1);
                                
                                g_controlServer.SendPacket(Protocol::MSG_FILE_INFO, (char*)&fileInfo, sizeof(fileInfo));

                                const int CHUNK_SIZE = 65536; 
                                std::vector<char> buffer(CHUNK_SIZE);
                                
                                while (!fileWorker.IsEOF() && g_isConnected) {
                                    int bytesRead = fileWorker.ReadNextChunk(buffer.data(), CHUNK_SIZE);
                                    if (bytesRead > 0) {
                                        if (!g_controlServer.SendPacket(Protocol::MSG_FILE_CHUNK, buffer.data(), bytesRead)) break;
                                    }
                                }
                                fileWorker.CloseFile();
                                g_controlServer.SendPacket(Protocol::MSG_FILE_END, nullptr, 0);
                            }
                        }).detach();
                    }
                    break;
                }

                case Protocol::MSG_PING: {
                    g_controlServer.SendPacket(Protocol::MSG_PONG, nullptr, 0);
                    break;
                }

                case Protocol::MSG_DISCONNECT: {
                    g_isConnected = false;
                    break;
                }

                case Protocol::MSG_SYS_INFO_REQUEST: {
                    Protocol::SysInfoPayload sysInfo = SysMonitor::GetSystemInfo();
                    g_controlServer.SendPacket(Protocol::MSG_SYS_INFO_RESPONSE, (char*)&sysInfo, sizeof(Protocol::SysInfoPayload));
                    break;
                }

                case Protocol::MSG_DRIVE_LIST_REQUEST: {
                    std::vector<std::string> drives = FileWorker::GetDrives();
                    std::string payload = "";
                    for (const auto& d : drives) payload += d + "\n";
                    g_controlServer.SendPacket(Protocol::MSG_DRIVE_LIST_RESPONSE, payload.c_str(), (int)payload.size());
                    break;
                }

                case Protocol::MSG_DIR_REQUEST: {
                    std::string path(header.size, '\0');
                    if (g_controlServer.ReceiveExact(&path[0], header.size)) {
                        std::string dirContent = FileWorker::GetDirectoryContent(path);
                        g_controlServer.SendPacket(Protocol::MSG_DIR_RESPONSE, dirContent.c_str(), (int)dirContent.size());
                    }
                    break;
                }

                case Protocol::MSG_PROCESS_LIST_REQUEST: {
                    std::vector<Protocol::ProcessInfoPayload> pList = SysMonitor::GetProcessList();
                    uint32_t totalBytes = (uint32_t)(pList.size() * sizeof(Protocol::ProcessInfoPayload));
                    g_controlServer.SendPacket(Protocol::MSG_PROCESS_LIST_RESPONSE, (char*)pList.data(), totalBytes);
                    break;
                }

                case Protocol::MSG_KILL_PROCESS: {
                    uint32_t pidToKill = 0;
                    if (g_controlServer.ReceiveExact((char*)&pidToKill, sizeof(uint32_t))) {
                        SysMonitor::KillProcess(pidToKill);
                    }
                    break;
                }

                case Protocol::MSG_CHAT_MESSAGE: {
                    if (header.size == sizeof(Protocol::ChatMessagePayload)) {
                        Protocol::ChatMessagePayload chat;
                        if (g_controlServer.ReceiveExact((char*)&chat, sizeof(chat))) {
                            chat.sender[sizeof(chat.sender) - 1] = '\0';
                            chat.message[sizeof(chat.message) - 1] = '\0';
                            ClientChatWindow::OnMessageReceived(chat);
                        } else {
                            g_isConnected = false;
                        }
                    } else if (header.size > 0) {
                        std::vector<char> dump(header.size);
                        g_controlServer.ReceiveExact(dump.data(), header.size);
                    }
                    break;
                }
                
                default: {
                    if (header.size > 0) {
                        std::vector<char> dump(header.size);
                        g_controlServer.ReceiveExact(dump.data(), header.size);
                    }
                    break;
                }
            }
        }
        g_isConnected = false;
    }
}

namespace SessionManager {

    void StartServer(int basePort) {
        g_isRunning = true;
        
        // Khởi tạo trạng thái Overlay (chạy ngầm trên luồng UI riêng)
        g_overlay.Initialize();
        g_overlay.SetVisible(false);
        g_overlay.SetState(OverlayState::Disconnected);

        int portControl = basePort;
        int portVideo   = basePort + 1;
        int portAudio   = basePort + 2;

        if (!g_controlServer.Start(portControl) ||
            !g_videoServer.Start(portVideo) ||
            !g_audioServer.Start(portAudio)) {
            g_controlServer.Close();
            g_videoServer.Close();
            g_audioServer.Close();
            return;
        }

        while (g_isRunning) {
            // Bước 1: Chờ Manager kết nối tới cổng Control (kiểm tra g_isRunning mỗi 1 giây)
            if (!g_controlServer.AcceptConnection(1000)) {
                continue;
            }

            // Bước 2: Manager đã kết nối Control, chờ kết nối tiếp Video và Audio (trong vòng 5 giây)
            if (!g_videoServer.AcceptConnection(5000) || !g_audioServer.AcceptConnection(5000)) {
                g_controlServer.CloseClient();
                g_videoServer.CloseClient();
                g_audioServer.CloseClient();
                continue;
            }

            // Cả 3 kết nối đã sẵn sàng!
            g_isConnected = true;
            DiscoveryResponder::SetBusy(true);
            InputInjector::ReleaseAllModifiers();
            g_lastInputTimeMs = 0;

            // 1. Hiển thị Pop-up thông báo (tự tắt sau 5s hoặc khi click)
            ConnectionAlert::Show(nullptr, L"Manager is connecting to your device.");

            // 2. Kích hoạt và hiện Overlay trạng thái ở góc phải trên màn hình
            g_overlay.SetVisible(true);
            g_overlay.SetState(OverlayState::ConnectedViewing);

            // 3. Khởi tạo cửa sổ chat cho máy khách
            ClientChatWindow::Initialize(&g_controlServer);

            // Bắt đầu thu âm và gửi trực tiếp qua cổng Audio (8082)
            AudioCapture::Start([](const char* buffer, int size) {
                if (g_isConnected) {
                    g_audioServer.SendPacket(Protocol::MSG_AUDIO_CHUNK, buffer, size);
                }
            });

            // Khởi chạy luồng gửi video và luồng nhận điều khiển
            std::thread txThread(TransmitVideoLoop);
            std::thread rxThread(ReceiveControlLoop);

            // Giám sát trạng thái kết nối
            while (g_isConnected && g_isRunning) {
                // Tự động chuyển từ "Controlling" về "Monitoring" nếu quản lý ngưng thao tác > 1.5 giây
                if (g_lastInputTimeMs > 0 && (GetTickCount64() - g_lastInputTimeMs > 1500)) {
                    g_overlay.SetState(OverlayState::ConnectedViewing);
                    g_lastInputTimeMs = 0;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            // Mất kết nối hoặc app dừng: Dọn dẹp phiên hiện tại
            g_isConnected = false;
            DiscoveryResponder::SetBusy(false);
            InputInjector::ReleaseAllModifiers();
            AudioCapture::Stop();
            ClientChatWindow::Close();
            g_controlServer.CloseClient();
            g_videoServer.CloseClient();
            g_audioServer.CloseClient();

            // Cập nhật trạng thái và ẩn overlay khi Manager ngắt kết nối
            g_overlay.SetState(OverlayState::Disconnected);
            g_overlay.SetVisible(false);

            if (txThread.joinable()) txThread.join();
            if (rxThread.joinable()) rxThread.join();
        }

        g_controlServer.Close();
        g_videoServer.Close();
        g_audioServer.Close();
        g_overlay.Destroy();
        ClientChatWindow::Close();
    }

    void Stop() {
        g_isRunning = false;
        g_isConnected = false;
        DiscoveryResponder::SetBusy(false);
        InputInjector::ReleaseAllModifiers();
        AudioCapture::Stop();
        ClientChatWindow::Close();
        g_controlServer.Close();
        g_videoServer.Close();
        g_audioServer.Close();
        g_overlay.Destroy();
    }

    StatusOverlay& GetOverlay() {
        return g_overlay;
    }
}
