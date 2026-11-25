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
#include "RfbServerSession.h"
#include "../../Shared/CRC32.h"
#include "../../Shared/PathSanitizer.h"
#include <filesystem>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <cstring>
#include <mutex>

namespace {
    TcpServer g_controlServer;
    TcpServer g_audioServer;

    StatusOverlay g_overlay;
    std::atomic<uint64_t> g_lastInputTimeMs(0);

    std::atomic<bool> g_isRunning(false);
    std::atomic<bool> g_isConnected(false);

    std::atomic<bool> g_fileTransferCancel(false);
    FileWorker g_clientUploadWorker;
    std::string g_uploadTargetDir = "";

    std::string g_expectedSessionToken = "";
    std::mutex g_tokenMutex;

    std::string g_authorizedPeerIP = "";
    uint64_t g_authorizedPeerExpiryTick = 0;
    std::mutex g_authPeerMutex;

    std::string g_currentSessionId = "";
    std::mutex g_sessionIdMutex;

    // Rate-limit chống brute-force mã PIN theo IP (tối đa 5 lần sai -> khóa 30 giây)
    struct AuthRateEntry {
        std::string ip;
        int failCount = 0;
        uint64_t lockoutUntilMs = 0;
    };
    std::vector<AuthRateEntry> g_authRateLimits;
    std::mutex g_rateLimitMutex;

    std::thread g_downloadThread;

    bool IsIpRateLimited(const std::string& ip) {
        if (ip.empty()) return false;
        std::lock_guard<std::mutex> lock(g_rateLimitMutex);
        uint64_t now = GetTickCount64();
        for (const auto& entry : g_authRateLimits) {
            if (entry.ip == ip && entry.lockoutUntilMs > now) {
                return true;
            }
        }
        return false;
    }

    void RecordAuthFailure(const std::string& ip) {
        if (ip.empty()) return;
        std::lock_guard<std::mutex> lock(g_rateLimitMutex);
        uint64_t now = GetTickCount64();
        for (auto& entry : g_authRateLimits) {
            if (entry.ip == ip) {
                if (entry.lockoutUntilMs > 0 && now >= entry.lockoutUntilMs) {
                    entry.failCount = 0;
                    entry.lockoutUntilMs = 0;
                }
                entry.failCount++;
                if (entry.failCount >= 5) {
                    entry.lockoutUntilMs = now + 30000;
                }
                return;
            }
        }
        if (g_authRateLimits.size() >= 64) {
            g_authRateLimits.erase(g_authRateLimits.begin());
        }
        g_authRateLimits.push_back({ip, 1, 0});
    }

    void ResetAuthRateLimit(const std::string& ip) {
        std::lock_guard<std::mutex> lock(g_rateLimitMutex);
        for (auto it = g_authRateLimits.begin(); it != g_authRateLimits.end(); ++it) {
            if (it->ip == ip) {
                g_authRateLimits.erase(it);
                return;
            }
        }
    }

    // Helper: Xác thực handshake Challenge-Response HMAC-SHA256 với select() timeout 3 giây
    bool PerformAuthHandshake(TcpServer& server) {
        SOCKET ctrlSock = server.GetClientSocket();
        if (ctrlSock == INVALID_SOCKET) return false;

        std::string clientIP = server.GetClientIP();
        if (IsIpRateLimited(clientIP)) {
            Protocol::AuthResponsePayload resp = {0};
            resp.status = Protocol::AUTH_REJECTED;
            strncpy_s(resp.message, "Too many failed attempts. Retry later.", sizeof(resp.message) - 1);
            server.SendPacket(Protocol::MSG_AUTH_RESPONSE, (const char*)&resp, sizeof(resp));
            return false;
        }

        // 0. Sinh và gửi serverChallenge (16-byte ngẫu nhiên) trước khi nhận proof từ Manager
        Protocol::AuthChallengePayload challenge = {0};
        Crypto::GenerateRandomBytes(challenge.serverChallenge, sizeof(challenge.serverChallenge));
        if (!server.SendPacket(Protocol::MSG_AUTH_CHALLENGE, (const char*)&challenge, sizeof(challenge))) {
            return false;
        }

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

        // 4.1. Kiểm tra phiên bản giao thức (N-1)
        if (req.clientVersion != Protocol::PROTOCOL_VERSION) {
            Protocol::AuthResponsePayload resp = {0};
            resp.status = Protocol::AUTH_VERSION_MISMATCH;
            strncpy_s(resp.message, "Protocol version mismatch", sizeof(resp.message) - 1);
            server.SendPacket(Protocol::MSG_AUTH_RESPONSE, (const char*)&resp, sizeof(resp));
            return false;
        }

        // 5. Tính HMAC-SHA256(PIN, clientChallenge || serverChallenge) và đối chiếu trong constant time (C-2)
        std::string expectedToken = SessionManager::GetExpectedToken();
        if (expectedToken.empty()) {
            Protocol::AuthResponsePayload resp = {0};
            resp.status = Protocol::AUTH_REJECTED;
            strncpy_s(resp.message, "Session PIN not initialized", sizeof(resp.message) - 1);
            server.SendPacket(Protocol::MSG_AUTH_RESPONSE, (const char*)&resp, sizeof(resp));
            return false;
        }
        uint8_t salt[32];
        std::memcpy(salt, req.clientChallenge, 16);
        std::memcpy(salt + 16, challenge.serverChallenge, 16);

        uint8_t expectedProof[Crypto::SHA256_DIGEST_SIZE];
        Crypto::HmacSha256(
            reinterpret_cast<const uint8_t*>(expectedToken.data()),
            expectedToken.size(),
            salt,
            sizeof(salt),
            expectedProof
        );

        bool proofOk = Crypto::ConstantTimeCompare(
            reinterpret_cast<const uint8_t*>(req.sessionToken),
            expectedProof,
            Crypto::SHA256_DIGEST_SIZE
        );
        Crypto::SecureZero(expectedProof, sizeof(expectedProof));

        if (!proofOk) {
            Crypto::SecureZero(salt, sizeof(salt));
            RecordAuthFailure(clientIP);
            Protocol::AuthResponsePayload resp = {0};
            resp.status = Protocol::AUTH_INVALID_TOKEN;
            strncpy_s(resp.message, "Invalid session token", sizeof(resp.message) - 1);
            server.SendPacket(Protocol::MSG_AUTH_RESPONSE, (const char*)&resp, sizeof(resp));
            return false;
        }
        ResetAuthRateLimit(clientIP);

        // 6. Xác thực thành công: Phản hồi SUCCESS kèm lại đúng serverChallenge để hai bên đồng bộ salt
        Protocol::AuthResponsePayload resp = {0};
        resp.status = Protocol::AUTH_SUCCESS;
        strncpy_s(resp.message, "Authenticated successfully", sizeof(resp.message) - 1);
        std::memcpy(resp.serverChallenge, challenge.serverChallenge, sizeof(resp.serverChallenge));

        if (!server.SendPacket(Protocol::MSG_AUTH_RESPONSE, (const char*)&resp, sizeof(resp))) {
            Crypto::SecureZero(salt, sizeof(salt));
            return false;
        }

        // 6.1. Sinh correlation session ID cho phiên (N-2)
        uint8_t randId[4];
        Crypto::GenerateRandomBytes(randId, sizeof(randId));
        char sessIdBuf[16];
        snprintf(sessIdBuf, sizeof(sessIdBuf), "SESS-%02X%02X%02X%02X", randId[0], randId[1], randId[2], randId[3]);
        {
            std::lock_guard<std::mutex> lock(g_sessionIdMutex);
            g_currentSessionId = sessIdBuf;
        }

        // Cấp quyền cho IP này trên cổng RFB 5900 (hạn chờ 30 giây để hoàn tất kết nối RFB)
        SessionManager::AuthorizePeer(clientIP, 30000);

        // 7. Sinh khóa phiên đối xứng ChaCha20 từ PIN + 32-byte salt (PBKDF2-HMAC-SHA256)
        uint8_t sessionKey[Crypto::KEY_SIZE];
        Crypto::DeriveKey(expectedToken, salt, sizeof(salt), sessionKey);
        server.SetSessionKey(sessionKey);
        Crypto::SecureZero(sessionKey, sizeof(sessionKey));
        Crypto::SecureZero(salt, sizeof(salt));

        // 8. Khôi phục SO_RCVTIMEO = 0 (vô hạn) cho luồng nhận điều khiển sau handshake
        DWORD zeroTimeout = 0;
        setsockopt(ctrlSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&zeroTimeout, sizeof(zeroTimeout));
        return true;
    }

    // ==========================================
    // ĐIỀU PHỐI TIN NHẮN DỊCH VỤ QUẢN TRỊ (PORT 8080)
    // ==========================================
    void DispatchClientControlMessage(Protocol::MessageType type, const char* payload, uint32_t size) {
        switch (type) {
            case Protocol::MSG_FILE_DOWNLOAD_REQ: {
                if (payload && size > 0) {
                    std::string reqPath(payload, size);
                    g_fileTransferCancel = true;
                    if (g_downloadThread.joinable()) {
                        g_downloadThread.join();
                    }
                    g_downloadThread = std::thread([reqPath]() {
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
                    });
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
                    if (batch.rootName[0] && PathSanitizer::IsSafeRelativePath(batch.rootName)) {
                        g_uploadTargetDir = batch.rootName;
                    } else {
                        g_uploadTargetDir = "uploads";
                    }
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

                    std::string safeBaseDir = g_uploadTargetDir.empty() ? "uploads" : g_uploadTargetDir;
                    std::string safePath;
                    if (!PathSanitizer::ResolveSafePath(safeBaseDir, hdr.relativePath, safePath)) {
                        std::string err = "ERROR: Phat hien Path Traversal hoac duong dan khong hop le: " + std::string(hdr.relativePath);
                        g_controlServer.SendPacket(Protocol::MSG_FILE_ERROR, err.c_str(), (int)err.size());
                        break;
                    }

                    if (hdr.isDirectory) {
                        try {
                            std::filesystem::create_directories(std::filesystem::u8path(safePath));
                        } catch (...) {}
                    } else {
                        g_clientUploadWorker.OpenFileForWrite(safePath, hdr.fileSize, hdr.expectedCrc32);
                    }
                } else if (payload && size >= sizeof(Protocol::FileInfoPayload)) {
                    Protocol::FileInfoPayload info;
                    memcpy(&info, payload, sizeof(info));
                    info.fileName[sizeof(info.fileName) - 1] = '\0';
                    std::string safeBaseDir = g_uploadTargetDir.empty() ? "uploads" : g_uploadTargetDir;
                    std::string safeFileName = PathSanitizer::SanitizeFileName(info.fileName);
                    std::string safePath;
                    if (PathSanitizer::ResolveSafePath(safeBaseDir, safeFileName, safePath)) {
                        g_clientUploadWorker.OpenFileForWrite(safePath, info.fileSize, 0);
                    } else {
                        std::string err = "ERROR: Ten file khong hop le!";
                        g_controlServer.SendPacket(Protocol::MSG_FILE_ERROR, err.c_str(), (int)err.size());
                    }
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

            // Kiểm tra header size và tính hợp lệ của message type (N-1)
            if (header.size > ControlEnvelope::MAX_CONTROL_PAYLOAD || !Protocol::ValidatePacketSize(header.type, header.size)) {
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
                uint8_t key[Crypto::KEY_SIZE];
                if (!g_controlServer.CopySessionKey(key)) {
                    g_isConnected = false;
                    break;
                }

                Protocol::PacketHeader innerHdr;
                bool unwrapOk = ControlEnvelope::UnwrapPacket(key, (const uint8_t*)rawBuf.data(), header.size, innerHdr, plainPayload);
                Crypto::SecureZero(key, sizeof(key));
                if (!unwrapOk) {
                    g_isConnected = false;
                    break;
                }

                // Kiểm tra tính hợp lệ của gói tin bên trong sau khi giải mã (N-1) và cấm lồng gói mã hóa
                if (innerHdr.type == Protocol::MSG_ENCRYPTED_CONTROL ||
                    !Protocol::ValidatePacketSize(innerHdr.type, innerHdr.size)) {
                    g_isConnected = false;
                    break;
                }

                const char* innerData = (innerHdr.size > 0) ? (const char*)plainPayload.data() : nullptr;
                DispatchClientControlMessage(innerHdr.type, innerData, innerHdr.size);
            } else {
                // C-3: Chặn hạ cấp gói tin rõ khi phiên đã kích hoạt mã hóa
                if (g_controlServer.HasSessionKey()) {
                    g_isConnected = false;
                    break;
                }
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
        int portAudio   = basePort + 2;

        if (!g_controlServer.Start(portControl) ||
            !g_audioServer.Start(portAudio)) {
            g_controlServer.Close();
            g_audioServer.Close();
            return;
        }

        while (g_isRunning) {
            // Bước 1: Chờ Manager kết nối tới cổng Control (kiểm tra g_isRunning mỗi 1 giây)
            if (!g_controlServer.AcceptConnection(1000)) {
                continue;
            }

            // Bước 1.5: Xác thực phiên trên Port 8080 với 3s timeout.
            if (!PerformAuthHandshake(g_controlServer)) {
                g_controlServer.CloseClient();
                continue;
            }

            // Bước 2: Chấp nhận kết nối Audio (8082) tùy chọn trong 1 giây và xác minh IP đã ủy quyền (I-5)
            if (g_audioServer.AcceptConnection(1000)) {
                std::string audioIP = g_audioServer.GetClientIP();
                if (!SessionManager::IsPeerAuthorized(audioIP)) {
                    g_audioServer.CloseClient();
                }
            }

            // Phiên kết nối quản trị đã sẵn sàng!
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
                if (g_isConnected && g_audioServer.GetClientSocket() != INVALID_SOCKET) {
                    g_audioServer.SendPacket(Protocol::MSG_AUDIO_CHUNK, buffer, size);
                }
            });

            // Khởi chạy luồng nhận điều khiển dịch vụ quản trị
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
            g_fileTransferCancel = true;
            SessionManager::RevokeAuthorizedPeer();
            RfbServer::DisconnectClient();
            ClipboardManager::Reset();
            DiscoveryResponder::SetBusy(false);
            InputInjector::ReleaseAllModifiers();
            AudioCapture::Stop();
            ClientChatWindow::Close();
            g_controlServer.CloseClient();
            g_audioServer.CloseClient();

            // Cập nhật trạng thái và ẩn overlay khi Manager ngắt kết nối
            g_overlay.SetState(OverlayState::Disconnected);
            g_overlay.SetVisible(false);

            if (rxThread.joinable()) rxThread.join();
            if (g_downloadThread.joinable()) g_downloadThread.join();
        }

        SessionManager::RevokeAuthorizedPeer();
        RfbServer::DisconnectClient();
        g_controlServer.Close();
        g_audioServer.Close();
        g_overlay.Destroy();
        ClientChatWindow::Close();
    }

    void Stop() {
        g_isRunning = false;
        g_isConnected = false;
        g_fileTransferCancel = true;
        SessionManager::RevokeAuthorizedPeer();
        RfbServer::DisconnectClient();
        ClipboardManager::Reset();
        DiscoveryResponder::SetBusy(false);
        InputInjector::ReleaseAllModifiers();
        AudioCapture::Stop();
        ClientChatWindow::Close();
        g_controlServer.Close();
        g_audioServer.Close();
        if (g_downloadThread.joinable() && std::this_thread::get_id() != g_downloadThread.get_id()) {
            g_downloadThread.join();
        }
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

    std::string GenerateNewPin() {
        {
            std::lock_guard<std::mutex> lock(g_rateLimitMutex);
            g_authRateLimits.clear();
        }
        // Sinh mã PIN ngẫu nhiên gồm 6 chữ số (100000 - 999999) sử dụng CSPRNG
        uint32_t val = 0;
        Crypto::GenerateRandomBytes(reinterpret_cast<uint8_t*>(&val), sizeof(val));
        uint32_t pinNum = 100000 + (val % 900000);
        std::string newPin = std::to_string(pinNum);
        SetExpectedToken(newPin);
        return newPin;
    }

    bool IsPeerAuthorized(const std::string& ip) {
        std::lock_guard<std::mutex> lock(g_authPeerMutex);
        if (g_authorizedPeerIP.empty()) return false;

        bool ipMatch = (ip == g_authorizedPeerIP);
        if (!ipMatch && (ip == "127.0.0.1" || ip == "localhost") &&
            (g_authorizedPeerIP == "127.0.0.1" || g_authorizedPeerIP == "localhost")) {
            ipMatch = true;
        }
        if (!ipMatch) return false;

        if (g_isConnected.load(std::memory_order_acquire)) return true;
        return (GetTickCount64() < g_authorizedPeerExpiryTick);
    }

    void AuthorizePeer(const std::string& ip, uint64_t durationMs) {
        std::lock_guard<std::mutex> lock(g_authPeerMutex);
        g_authorizedPeerIP = ip;
        g_authorizedPeerExpiryTick = GetTickCount64() + durationMs;
    }

    void RevokeAuthorizedPeer() {
        std::lock_guard<std::mutex> lock(g_authPeerMutex);
        g_authorizedPeerIP.clear();
        g_authorizedPeerExpiryTick = 0;
        {
            std::lock_guard<std::mutex> sLock(g_sessionIdMutex);
            g_currentSessionId.clear();
        }
    }

    std::string GetAuthorizedPeerIP() {
        std::lock_guard<std::mutex> lock(g_authPeerMutex);
        return g_authorizedPeerIP;
    }

    std::string GetCurrentSessionId() {
        std::lock_guard<std::mutex> lock(g_sessionIdMutex);
        return g_currentSessionId;
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
