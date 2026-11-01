#include "../../Shared/Network.h"
#include "SessionManager.h"
#include "../../Shared/Protocol.h"
#include "../../Shared/Crypto.h"
#include "../../Shared/ControlEnvelope.h"
#include "../../Shared/ClipboardManager.h"
#include "TrayIcon.h"
#include "../Media/ScreenCapture.h"
#include "../System/InputInjector.h"
#include "../FileTransfer/FileWorker.h"
#include "../Media/AudioCapture.h"
#include "../System/SysMonitor.h"
#include "StatusOverlay.h"
#include "ConnectionAlert.h"
#include "DiscoveryResponder.h"
#include "ClientChatWindow.h"
#include "../../Shared/CRC32.h"
#include <filesystem>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <cstring>
#include <mutex>

namespace {
    TcpServer g_controlServer;
    TcpServer g_videoServer;
    TcpServer g_audioServer;

    StatusOverlay g_overlay;
    std::atomic<uint64_t> g_lastInputTimeMs(0);

    std::atomic<bool> g_isRunning(false);
    std::atomic<bool> g_isConnected(false);

    std::atomic<bool> g_fileTransferCancel(false);
    FileWorker g_clientUploadWorker;
    std::string g_uploadTargetDir = "";

    std::string g_expectedSessionToken = Protocol::DEFAULT_AUTH_TOKEN;
    std::mutex g_tokenMutex;

    // Helper: Xác thực handshake với select() timeout 3 giây
    bool PerformAuthHandshake(TcpServer& server) {
        SOCKET ctrlSock = server.GetClientSocket();
        if (ctrlSock == INVALID_SOCKET) return false;

        // 1. Thiết lập SO_RCVTIMEO = 3000ms phòng vệ chống treo socket nếu gửi ngắt quãng
        DWORD rcvTimeout = 3000;
        setsockopt(ctrlSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rcvTimeout, sizeof(rcvTimeout));

        // 2. Chờ dữ liệu bằng select() trong 3 giây
        fd_set readFds;
        FD_ZERO(&readFds);
        FD_SET(ctrlSock, &readFds);
        timeval tv = { 3, 0 }; // 3s

        int selRet = select(0, &readFds, NULL, NULL, &tv);
        if (selRet <= 0) {
            Protocol::AuthResponsePayload resp = {0};
            resp.status = Protocol::AUTH_TIMEOUT;
            strncpy_s(resp.message, "Authentication timed out (3s)", sizeof(resp.message) - 1);
            server.SendPacket(Protocol::MSG_AUTH_RESPONSE, (const char*)&resp, sizeof(resp));
            return false;
        }

        // 3. Đọc Header
        Protocol::PacketHeader header = { (Protocol::MessageType)0, 0 };
        if (!server.ReceiveExact((char*)&header, sizeof(header))) {
            return false;
        }

        if (header.type != Protocol::MSG_AUTH_REQUEST || header.size != sizeof(Protocol::AuthRequestPayload)) {
            if (header.size > 0 && header.size <= 4096) {
                std::vector<char> dump(header.size);
                server.ReceiveExact(dump.data(), header.size);
            }
            Protocol::AuthResponsePayload resp = {0};
            resp.status = Protocol::AUTH_REJECTED;
            strncpy_s(resp.message, "Expected MSG_AUTH_REQUEST", sizeof(resp.message) - 1);
            server.SendPacket(Protocol::MSG_AUTH_RESPONSE, (const char*)&resp, sizeof(resp));
            return false;
        }

        // 4. Đọc AuthRequestPayload
        Protocol::AuthRequestPayload req = {0};
        if (!server.ReceiveExact((char*)&req, sizeof(req))) {
            return false;
        }
        req.sessionToken[sizeof(req.sessionToken) - 1] = '\0';

        // 5. Kiểm tra tính hợp lệ của Token trong constant time
        std::string expectedToken = SessionManager::GetExpectedToken();
        char expectedBuf[64] = {0};
        strncpy_s(expectedBuf, expectedToken.c_str(), sizeof(expectedBuf) - 1);

        if (!Crypto::ConstantTimeCompare((const uint8_t*)req.sessionToken, (const uint8_t*)expectedBuf, sizeof(req.sessionToken))) {
            Protocol::AuthResponsePayload resp = {0};
            resp.status = Protocol::AUTH_INVALID_TOKEN;
            strncpy_s(resp.message, "Invalid session token", sizeof(resp.message) - 1);
            server.SendPacket(Protocol::MSG_AUTH_RESPONSE, (const char*)&resp, sizeof(resp));
            return false;
        }

        // 6. Xác thực thành công: Gửi phản hồi SUCCESS
        Protocol::AuthResponsePayload resp = {0};
        resp.status = Protocol::AUTH_SUCCESS;
        strncpy_s(resp.message, "Authenticated successfully", sizeof(resp.message) - 1);
        if (!server.SendPacket(Protocol::MSG_AUTH_RESPONSE, (const char*)&resp, sizeof(resp))) {
            return false;
        }

        // 7. Sinh khóa phiên đối xứng ChaCha20 từ token và kích hoạt trên socket điều khiển
        uint8_t sessionKey[Crypto::KEY_SIZE];
        Crypto::DeriveKey(expectedToken, nullptr, 0, sessionKey);
        server.SetSessionKey(sessionKey);

        // 8. Khôi phục SO_RCVTIMEO = 0 (vô hạn) cho luồng nhận điều khiển sau handshake
        DWORD zeroTimeout = 0;
        setsockopt(ctrlSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&zeroTimeout, sizeof(zeroTimeout));
        return true;
    }

    // ==========================================
    // LUỒNG GỬI VIDEO (TX): LIÊN TỤC CHỤP & GỬI MÀN HÌNH QUA CỔNG VIDEO (8081)
    // ==========================================
    void TransmitVideoLoop() {
        if (!ScreenCapture::Initialize()) return;
        ScreenCapture::ForceFullFrame(); // Đảm bảo frame đầu tiên gửi cho Manager luôn là 100% full screen
        
        std::vector<char> frameBuffer;
        frameBuffer.reserve(512 * 1024); // Cấp phát sẵn 512KB tránh cấp phát lại
        int x, y, w, h;

        int currentFps = ScreenCapture::GetTargetFps();
        if (currentFps != 30 && currentFps != 60) currentFps = 60;
        std::chrono::microseconds frameTargetTime(1000000 / currentFps);
        auto nextFrameTime = std::chrono::steady_clock::now();

        while (g_isConnected && g_isRunning) {
            int fps = ScreenCapture::GetTargetFps();
            if (fps != currentFps && (fps == 30 || fps == 60)) {
                currentFps = fps;
                frameTargetTime = std::chrono::microseconds(1000000 / currentFps);
            }
            nextFrameTime += frameTargetTime;

            // Chống dồn hàng đợi (TCP Flow-Control): Nếu socket đang bận thì bỏ qua khung
            if (!g_videoServer.CanSendWithoutBlocking()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                auto now = std::chrono::steady_clock::now();
                if (now > nextFrameTime + frameTargetTime) {
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
                // Đã quá thời hạn chu kỳ khung hình hiện tại: Căn chỉnh ngay nextFrameTime theo now để chống drift
                nextFrameTime = now;
            }
        }
        ScreenCapture::Cleanup();
    }

    // ==========================================
    // ĐIỀU PHỐI TIN NHẮN ĐIỀU KHIỂN (ĐÃ GIẢI MÃ HOẶC RAW)
    // ==========================================
    void DispatchClientControlMessage(Protocol::MessageType type, const char* payload, uint32_t size) {
        switch (type) {
            case Protocol::MSG_MOUSE_EVENT: {
                if (payload && size >= sizeof(Protocol::MousePayload)) {
                    Protocol::MousePayload mouse;
                    memcpy(&mouse, payload, sizeof(mouse));
                    InputInjector::InjectMouse(mouse);
                    g_overlay.SetState(OverlayState::ConnectedControlling);
                    g_lastInputTimeMs = GetTickCount64();
                }
                break;
            }

            case Protocol::MSG_KEY_EVENT: {
                if (payload && size >= sizeof(Protocol::KeyboardPayload)) {
                    Protocol::KeyboardPayload key;
                    memcpy(&key, payload, sizeof(key));
                    InputInjector::InjectKeyboard(key);
                    g_overlay.SetState(OverlayState::ConnectedControlling);
                    g_lastInputTimeMs = GetTickCount64();
                }
                break;
            }

            case Protocol::MSG_FILE_DOWNLOAD_REQ: {
                if (payload && size > 0) {
                    std::string reqPath(payload, size);
                    std::thread([reqPath]() {
                        g_fileTransferCancel = false;
                        auto items = FileWorker::ScanDirectoryTree(reqPath);
                        if (items.empty()) {
                            std::string err = "ERROR: Duong dan khong hop le hoac khong ton tai!";
                            g_controlServer.SendPacket(Protocol::MSG_FILE_ERROR, err.c_str(), (int)err.size());
                            return;
                        }

                        bool isDir = false;
                        try {
                            isDir = std::filesystem::is_directory(std::filesystem::u8path(reqPath));
                        } catch (...) {}

                        if (isDir || items.size() > 1) {
                            uint64_t totalBytes = 0;
                            uint32_t totalFiles = 0;
                            for (const auto& it : items) {
                                if (!it.isDirectory) {
                                    totalBytes += it.fileSize;
                                    totalFiles++;
                                }
                            }
                            Protocol::FileBatchStartPayload batch = {0};
                            batch.totalFiles = totalFiles;
                            batch.totalBytes = totalBytes;
                            std::string rootName = "";
                            try {
                                rootName = std::filesystem::u8path(reqPath).filename().u8string();
                            } catch (...) {}
                            strncpy_s(batch.rootName, rootName.c_str(), sizeof(batch.rootName) - 1);
                            g_controlServer.SendPacket(Protocol::MSG_FILE_BATCH_START, (char*)&batch, sizeof(batch));
                        }

                        uint32_t fileIdx = 0;
                        for (const auto& it : items) {
                            if (g_fileTransferCancel || !g_isConnected) break;

                            Protocol::FileHeaderPayload header = {0};
                            header.fileSize = it.fileSize;
                            header.expectedCrc32 = it.crc32;
                            header.totalChunks = (uint32_t)((it.fileSize + Protocol::FILE_CHUNK_SIZE - 1) / Protocol::FILE_CHUNK_SIZE);
                            header.isDirectory = it.isDirectory ? 1 : 0;
                            strncpy_s(header.relativePath, it.relativePath.c_str(), sizeof(header.relativePath) - 1);

                            if (!g_controlServer.SendPacket(Protocol::MSG_FILE_INFO, (char*)&header, sizeof(header))) break;

                            if (it.isDirectory) {
                                continue;
                            }

                            FileWorker worker;
                            if (worker.OpenFileForRead(it.fullPath)) {
                                std::vector<char> buffer(Protocol::FILE_CHUNK_SIZE + sizeof(Protocol::FileChunkHeader));
                                Protocol::FileChunkHeader* chkHdr = (Protocol::FileChunkHeader*)buffer.data();
                                char* chunkData = buffer.data() + sizeof(Protocol::FileChunkHeader);

                                uint32_t chunkIdx = 0;
                                uint32_t runningCrc = 0;

                                while (!worker.IsEOF() && g_isConnected && !g_fileTransferCancel) {
                                    int bytesRead = worker.ReadNextChunk(chunkData, Protocol::FILE_CHUNK_SIZE);
                                    if (bytesRead > 0) {
                                        chkHdr->chunkIndex = chunkIdx++;
                                        chkHdr->dataLength = static_cast<uint32_t>(bytesRead);
                                        runningCrc = CRC32::Update(runningCrc, chunkData, bytesRead);

                                        int pktLen = (int)(sizeof(Protocol::FileChunkHeader) + bytesRead);
                                        if (!g_controlServer.SendPacket(Protocol::MSG_FILE_CHUNK, buffer.data(), pktLen)) break;
                                    }
                                }
                                worker.CloseFile();

                                if (g_fileTransferCancel) {
                                    Protocol::FileEndPayload endP = { fileIdx, runningCrc, Protocol::FILE_STATUS_CANCEL };
                                    g_controlServer.SendPacket(Protocol::MSG_FILE_END, (char*)&endP, sizeof(endP));
                                    break;
                                }

                                Protocol::FileEndPayload endP = { fileIdx, runningCrc, Protocol::FILE_STATUS_SUCCESS };
                                g_controlServer.SendPacket(Protocol::MSG_FILE_END, (char*)&endP, sizeof(endP));
                                fileIdx++;
                            }
                        }

                        if (isDir || items.size() > 1) {
                            g_controlServer.SendPacket(Protocol::MSG_FILE_BATCH_END, nullptr, 0);
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
                std::string payloadOut = "";
                for (const auto& d : drives) payloadOut += d + "\n";
                g_controlServer.SendPacket(Protocol::MSG_DRIVE_LIST_RESPONSE, payloadOut.c_str(), (int)payloadOut.size());
                break;
            }

            case Protocol::MSG_DIR_REQUEST: {
                if (payload && size > 0) {
                    std::string path(payload, size);
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
                if (payload && size >= sizeof(uint32_t)) {
                    uint32_t pidToKill = 0;
                    memcpy(&pidToKill, payload, sizeof(uint32_t));
                    SysMonitor::KillProcess(pidToKill);
                }
                break;
            }

            case Protocol::MSG_CHAT_MESSAGE: {
                if (payload && size >= sizeof(Protocol::ChatMessagePayload)) {
                    Protocol::ChatMessagePayload chat;
                    memcpy(&chat, payload, sizeof(chat));
                    chat.sender[sizeof(chat.sender) - 1] = '\0';
                    chat.message[sizeof(chat.message) - 1] = '\0';
                    ClientChatWindow::OnMessageReceived(chat);
                }
                break;
            }

            case Protocol::MSG_CLIPBOARD_TEXT: {
                if (payload && size > 0 && size <= ClipboardManager::MAX_CLIPBOARD_TEXT_BYTES) {
                    std::string text(payload, size);
                    ClipboardManager::WriteText(TrayIcon::GetHwnd(), text);
                }
                break;
            }

            case Protocol::MSG_SPECIAL_ACTION: {
                if (payload && size >= sizeof(Protocol::SpecialActionPayload)) {
                    Protocol::SpecialActionPayload act;
                    memcpy(&act, payload, sizeof(act));
                    InputInjector::InjectSpecialAction(act.actionType);
                }
                break;
            }

            case Protocol::MSG_VIDEO_CONFIG: {
                if (payload && size >= sizeof(Protocol::VideoConfigPayload)) {
                    Protocol::VideoConfigPayload cfg;
                    memcpy(&cfg, payload, sizeof(cfg));
                    ScreenCapture::SetVideoConfig(cfg.qualityPreset, cfg.targetFps);
                }
                break;
            }

            case Protocol::MSG_FILE_CANCEL: {
                g_fileTransferCancel = true;
                g_clientUploadWorker.AbortWrite();
                break;
            }

            case Protocol::MSG_FILE_BATCH_START: {
                if (payload && size >= sizeof(Protocol::FileBatchStartPayload)) {
                    Protocol::FileBatchStartPayload batch;
                    memcpy(&batch, payload, sizeof(batch));
                    batch.rootName[sizeof(batch.rootName) - 1] = '\0';
                    g_uploadTargetDir = batch.rootName;
                    g_fileTransferCancel = false;
                }
                break;
            }

            case Protocol::MSG_FILE_BATCH_END: {
                g_uploadTargetDir = "";
                break;
            }

            case Protocol::MSG_FILE_INFO: {
                if (payload && size >= sizeof(Protocol::FileHeaderPayload)) {
                    Protocol::FileHeaderPayload hdr;
                    memcpy(&hdr, payload, sizeof(hdr));
                    hdr.relativePath[sizeof(hdr.relativePath) - 1] = '\0';

                    std::string targetPath = hdr.relativePath;
                    if (!g_uploadTargetDir.empty() && targetPath.find(g_uploadTargetDir) != 0) {
                        targetPath = g_uploadTargetDir + "/" + targetPath;
                    }

                    if (hdr.isDirectory) {
                        try {
                            std::filesystem::create_directories(std::filesystem::u8path(targetPath));
                        } catch (...) {}
                    } else {
                        g_clientUploadWorker.OpenFileForWrite(targetPath, hdr.fileSize, hdr.expectedCrc32);
                    }
                } else if (payload && size >= sizeof(Protocol::FileInfoPayload)) {
                    Protocol::FileInfoPayload info;
                    memcpy(&info, payload, sizeof(info));
                    info.fileName[sizeof(info.fileName) - 1] = '\0';
                    g_clientUploadWorker.OpenFileForWrite(info.fileName, info.fileSize, 0);
                }
                break;
            }

            case Protocol::MSG_FILE_CHUNK: {
                if (payload && size > 0) {
                    if (size > sizeof(Protocol::FileChunkHeader)) {
                        const Protocol::FileChunkHeader* chk = (const Protocol::FileChunkHeader*)payload;
                        if (chk->dataLength + sizeof(Protocol::FileChunkHeader) == size) {
                            g_clientUploadWorker.WriteChunk(payload + sizeof(Protocol::FileChunkHeader), chk->dataLength);
                            break;
                        }
                    }
                    g_clientUploadWorker.WriteChunk(payload, (int)size);
                }
                break;
            }

            case Protocol::MSG_FILE_END: {
                bool crcMatch = false;
                bool ok = g_clientUploadWorker.FinalizeWrite(&crcMatch);
                if (payload && size >= sizeof(Protocol::FileEndPayload)) {
                    const Protocol::FileEndPayload* endP = (const Protocol::FileEndPayload*)payload;
                    if (endP->status == Protocol::FILE_STATUS_CANCEL) {
                        g_clientUploadWorker.AbortWrite();
                    }
                }
                if (!ok && !crcMatch) {
                    std::string err = "ERROR: CRC32 mismatch on received file!";
                    g_controlServer.SendPacket(Protocol::MSG_FILE_ERROR, err.c_str(), (int)err.size());
                }
                break;
            }

            default:
                break;
        }
    }

    // ==========================================
    // LUỒNG NHẬN CONTROL (RX): CHỜ LỆNH QUA CỔNG CONTROL (8080) VÀ GIẢI MÃ NẾU CẦN
    // ==========================================
    void ReceiveControlLoop() {
        std::vector<char> rawBuf;
        rawBuf.reserve(64 * 1024);
        std::vector<uint8_t> plainPayload;
        plainPayload.reserve(64 * 1024);

        while (g_isConnected && g_isRunning) {
            Protocol::PacketHeader header;
            if (!g_controlServer.ReceiveExact((char*)&header, sizeof(Protocol::PacketHeader))) {
                g_isConnected = false;
                break;
            }

            if (header.size > ControlEnvelope::MAX_CONTROL_PAYLOAD) {
                g_isConnected = false;
                break;
            }

            if (header.size > 0) {
                rawBuf.resize(header.size);
                if (!g_controlServer.ReceiveExact(rawBuf.data(), (int)header.size)) {
                    g_isConnected = false;
                    break;
                }
            } else {
                rawBuf.clear();
            }

            if (header.type == Protocol::MSG_ENCRYPTED_CONTROL) {
                const uint8_t* key = g_controlServer.GetSessionKey();
                if (!key) {
                    g_isConnected = false;
                    break;
                }

                Protocol::PacketHeader innerHdr;
                if (!ControlEnvelope::UnwrapPacket(key, (const uint8_t*)rawBuf.data(), header.size, innerHdr, plainPayload)) {
                    g_isConnected = false;
                    break;
                }

                const char* innerData = (innerHdr.size > 0) ? (const char*)plainPayload.data() : nullptr;
                DispatchClientControlMessage(innerHdr.type, innerData, innerHdr.size);
            } else {
                DispatchClientControlMessage(header.type, rawBuf.data(), header.size);
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

            // Bước 1.5: Xác thực phiên trên Port 8080 với 3s timeout.
            // Client KHÔNG chấp nhận kết nối Video (8081) hoặc Audio (8082) nếu xác thực thất bại!
            if (!PerformAuthHandshake(g_controlServer)) {
                g_controlServer.CloseClient();
                continue;
            }

            // Bước 2: Manager đã xác thực thành công Port 8080!
            // Tiếp tục chấp nhận kết nối Video (8081) và Audio (8082) trong vòng 5 giây
            if (!g_videoServer.AcceptConnection(5000) || !g_audioServer.AcceptConnection(5000)) {
                g_controlServer.CloseClient();
                g_videoServer.CloseClient();
                g_audioServer.CloseClient();
                continue;
            }

            // Bước 2.5: Kiểm tra xác minh IP nguồn (Chống tấn công cướp kênh stream 8081/8082)
            sockaddr_in ctrlAddr = {}, vidAddr = {}, audAddr = {};
            int addrLen = sizeof(sockaddr_in);
            getpeername(g_controlServer.GetClientSocket(), (sockaddr*)&ctrlAddr, &addrLen);
            addrLen = sizeof(sockaddr_in);
            getpeername(g_videoServer.GetClientSocket(), (sockaddr*)&vidAddr, &addrLen);
            addrLen = sizeof(sockaddr_in);
            getpeername(g_audioServer.GetClientSocket(), (sockaddr*)&audAddr, &addrLen);

            if (ctrlAddr.sin_addr.s_addr != vidAddr.sin_addr.s_addr ||
                ctrlAddr.sin_addr.s_addr != audAddr.sin_addr.s_addr) {
                // Remote IP của các kênh không trùng khớp!
                g_controlServer.CloseClient();
                g_videoServer.CloseClient();
                g_audioServer.CloseClient();
                continue;
            }

            // Cả 3 kết nối đã sẵn sàng và được xác thực hoàn toàn!
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
            ClipboardManager::Reset();
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
        ClipboardManager::Reset();
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

    void SetExpectedToken(const std::string& token) {
        std::lock_guard<std::mutex> lock(g_tokenMutex);
        g_expectedSessionToken = token;
    }

    std::string GetExpectedToken() {
        std::lock_guard<std::mutex> lock(g_tokenMutex);
        return g_expectedSessionToken;
    }

    bool IsConnected() {
        return g_isConnected.load(std::memory_order_acquire);
    }

    bool SendClipboardText(const std::string& text) {
        if (!g_isConnected.load(std::memory_order_acquire)) return false;
        if (text.empty() || text.size() > ClipboardManager::MAX_CLIPBOARD_TEXT_BYTES) return false;
        return g_controlServer.SendPacket(Protocol::MSG_CLIPBOARD_TEXT, text.data(), (int)text.size());
    }
}
