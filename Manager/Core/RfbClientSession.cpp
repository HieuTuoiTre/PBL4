#include "RfbClientSession.h"
#include "../UI/RemoteWindow.h"

#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <sstream>

namespace {
    std::atomic<bool> g_isConnected(false);
    std::atomic<bool> g_shouldStop(false);

    TcpClient g_client;
    std::thread g_workerThread;

    int g_serverWidth = 1920;
    int g_serverHeight = 1080;
    std::string g_serverDesktopName = "Remote VNC Desktop";

    RfbClient::PacketLogCallback g_logCallback = nullptr;

    // TigerVNC CConnection State Machine (§7 RFC 6143)
    std::atomic<Rfb::RfbState> g_state(Rfb::RFBSTATE_UNINITIALISED);

    void LogPacket(const std::string& dir, const std::string& type, size_t size, const std::string& desc) {
        if (g_logCallback) {
            g_logCallback(dir, type, size, desc);
        }
    }

    void SetState(Rfb::RfbState newState) {
        Rfb::RfbState oldState = g_state.exchange(newState, std::memory_order_acq_rel);
        if (oldState != newState) {
            LogPacket("STATE", Rfb::RfbStateToString(newState), 0,
                      std::string("Chuyển trạng thái: ") + Rfb::RfbStateToString(oldState) + " -> " + Rfb::RfbStateToString(newState));
        }
    }

    bool PerformClientHandshake(std::string& outError) {
        // State: RFBSTATE_PROTOCOL_VERSION (§7.1.1)
        SetState(Rfb::RFBSTATE_PROTOCOL_VERSION);

        // 1. Nhận ProtocolVersion từ Server (12 bytes)
        char serverVer[13] = { 0 };
        if (!g_client.ReceiveExact(serverVer, (int)Rfb::VERSION_STRING_LENGTH)) {
            outError = "Không thể nhận chuỗi phiên bản ProtocolVersion từ VNC Server!";
            SetState(Rfb::RFBSTATE_UNINITIALISED);
            return false;
        }
        LogPacket("RECV", "ProtocolVersion", 12, std::string(serverVer, 12));

        // 2. Gửi lại ProtocolVersion chuẩn RFB 003.008\n
        if (!g_client.SendRaw(Rfb::VERSION_3_8_STRING, (int)Rfb::VERSION_STRING_LENGTH)) {
            outError = "Không thể gửi phản hồi ProtocolVersion tới VNC Server!";
            SetState(Rfb::RFBSTATE_UNINITIALISED);
            return false;
        }
        LogPacket("SEND", "ProtocolVersion", 12, "RFB 003.008\\n");

        // State: RFBSTATE_SECURITY_TYPES (§7.1.2)
        SetState(Rfb::RFBSTATE_SECURITY_TYPES);

        // 3. Nhận danh sách loại bảo mật (Security Types)
        uint8_t numSecTypes = 0;
        if (!g_client.ReceiveExact((char*)&numSecTypes, 1)) {
            outError = "Không thể nhận số lượng Security Types từ Server!";
            SetState(Rfb::RFBSTATE_UNINITIALISED);
            return false;
        }

        if (numSecTypes == 0) {
            // Lỗi kết nối bị từ chối từ Server (kèm chuỗi lý do)
            uint32_t reasonLen = 0;
            g_client.ReceiveExact((char*)&reasonLen, 4);
            reasonLen = ntohl(reasonLen);
            std::string reason = "Bị từ chối kết nối";
            if (reasonLen > 0 && reasonLen < 1024) {
                std::vector<char> rBuf(reasonLen + 1, 0);
                g_client.ReceiveExact(rBuf.data(), reasonLen);
                reason = rBuf.data();
            }
            outError = "VNC Server từ chối kết nối: " + reason;
            SetState(Rfb::RFBSTATE_UNINITIALISED);
            return false;
        }

        std::vector<uint8_t> secList(numSecTypes);
        if (!g_client.ReceiveExact((char*)secList.data(), numSecTypes)) {
            outError = "Lỗi đọc danh sách Security Types từ Server!";
            SetState(Rfb::RFBSTATE_UNINITIALISED);
            return false;
        }

        // Kiểm tra xem Server có hỗ trợ SECURITY_NONE (1) hay không
        bool hasNone = false;
        for (uint8_t st : secList) {
            if (st == (uint8_t)Rfb::SECURITY_NONE) {
                hasNone = true;
                break;
            }
        }

        if (!hasNone) {
            outError = "VNC Server (ví dụ TigerVNC) đang yêu cầu mật khẩu bảo mật (VncAuth / VeNCrypt)!\n\n"
                       "Để kết nối thành công với Manager:\n"
                       "1. Mở cấu hình TigerVNC Server -> Options -> Security.\n"
                       "2. Chọn kiểu bảo mật là 'None' (hoặc tham số SecurityTypes=None).\n"
                       "3. Thử kết nối lại.";
            SetState(Rfb::RFBSTATE_UNINITIALISED);
            return false;
        }

        std::stringstream ssSec;
        ssSec << "Server hỗ trợ " << (int)numSecTypes << " kiểu bảo mật. Chọn None (1)";
        LogPacket("RECV", "SecurityTypes", 1 + numSecTypes, ssSec.str());

        // Chọn kiểu bảo mật None (1)
        uint8_t chosenSec = (uint8_t)Rfb::SECURITY_NONE;
        if (!g_client.SendRaw((const char*)&chosenSec, 1)) {
            outError = "Lỗi gửi loại bảo mật được chọn!";
            SetState(Rfb::RFBSTATE_UNINITIALISED);
            return false;
        }
        LogPacket("SEND", "SecurityChoice", 1, "Chọn SECURITY_NONE (1)");

        // State: RFBSTATE_SECURITY (§7.1.3)
        SetState(Rfb::RFBSTATE_SECURITY);

        // 4. Nhận SecurityResult (4 bytes Big-Endian: 0 = OK)
        uint32_t secResult = 0;
        if (!g_client.ReceiveExact((char*)&secResult, 4)) {
            outError = "Lỗi nhận kết quả xác thực SecurityResult từ Server!";
            SetState(Rfb::RFBSTATE_UNINITIALISED);
            return false;
        }
        secResult = ntohl(secResult);
        if (secResult != Rfb::SECURITY_RESULT_OK) {
            uint32_t reasonLen = 0;
            std::string reason = "";
            if (g_client.ReceiveExact((char*)&reasonLen, 4)) {
                reasonLen = ntohl(reasonLen);
                if (reasonLen > 0 && reasonLen < 1024) {
                    std::vector<char> rBuf(reasonLen + 1, 0);
                    g_client.ReceiveExact(rBuf.data(), reasonLen);
                    reason = ": " + std::string(rBuf.data());
                }
            }
            outError = "Xác thực VNC thất bại (SecurityResult != 0)" + reason;
            SetState(Rfb::RFBSTATE_UNINITIALISED);
            return false;
        }
        LogPacket("RECV", "SecurityResult", 4, "Xác thực thành công (OK 0)");

        // State: RFBSTATE_INITIALISATION (§7.3)
        SetState(Rfb::RFBSTATE_INITIALISATION);

        // 5. Gửi ClientInit (1 byte shared-flag = 1)
        Rfb::RfbClientInit cInit;
        cInit.sharedFlag = 1;
        if (!g_client.SendRaw((const char*)&cInit, sizeof(cInit))) {
            outError = "Lỗi gửi ClientInit!";
            SetState(Rfb::RFBSTATE_UNINITIALISED);
            return false;
        }
        LogPacket("SEND", "ClientInit", 1, "sharedFlag = 1 (Chia sẻ phiên)");

        // 6. Nhận ServerInit Header (24 bytes)
        Rfb::RfbServerInitHeader sInit;
        if (!g_client.ReceiveExact((char*)&sInit, sizeof(sInit))) {
            outError = "Lỗi nhận ServerInit Header từ Server!";
            SetState(Rfb::RFBSTATE_UNINITIALISED);
            return false;
        }

        g_serverWidth = (int)ntohs(sInit.framebufferWidth);
        g_serverHeight = (int)ntohs(sInit.framebufferHeight);
        uint32_t nameLen = ntohl(sInit.nameLength);

        std::vector<char> nameBuf(nameLen + 1, 0);
        if (nameLen > 0 && nameLen < 2048) {
            g_client.ReceiveExact(nameBuf.data(), nameLen);
            g_serverDesktopName = nameBuf.data();
        }

        std::stringstream ssInit;
        ssInit << "Độ phân giải: " << g_serverWidth << "x" << g_serverHeight 
               << ", Tên: '" << g_serverDesktopName << "'";
        LogPacket("RECV", "ServerInit", sizeof(sInit) + nameLen, ssInit.str());

        // 7. Gửi SetEncodings (Thông báo hỗ trợ Tight/JPEG, Tight Quality 8, Raw, DesktopSize)
        int32_t encodings[] = {
            (int32_t)htonl((uint32_t)Rfb::ENCODING_TIGHT),
            (int32_t)htonl((uint32_t)(Rfb::ENCODING_JPEG_QUALITY_0 + 8)), // Yêu cầu Tight JPEG Quality 8
            (int32_t)htonl((uint32_t)Rfb::ENCODING_RAW),
            (int32_t)htonl((uint32_t)Rfb::ENCODING_DESKTOP_SIZE)
        };
        uint16_t numEnc = 4;

        Rfb::RfbSetEncodingsHeader encHdr;
        encHdr.messageType = Rfb::CLIENT_SET_ENCODINGS;
        encHdr.padding = 0;
        encHdr.numEncodings = htons(numEnc);

        std::vector<char> encPacket(sizeof(encHdr) + sizeof(encodings));
        std::memcpy(encPacket.data(), &encHdr, sizeof(encHdr));
        std::memcpy(encPacket.data() + sizeof(encHdr), encodings, sizeof(encodings));

        if (!g_client.SendRaw(encPacket.data(), (int)encPacket.size())) {
            outError = "Lỗi gửi SetEncodings!";
            SetState(Rfb::RFBSTATE_UNINITIALISED);
            return false;
        }
        LogPacket("SEND", "SetEncodings", encPacket.size(), "Hỗ trợ Tight/JPEG (Q8), Raw, DesktopSize");

        // State: RFBSTATE_NORMAL (§7.5 & §7.6)
        SetState(Rfb::RFBSTATE_NORMAL);
        return true;
    }

    void WorkerLoop() {
        std::vector<char> jpegBuf;
        jpegBuf.reserve(512 * 1024);

        while (g_isConnected && !g_shouldStop) {
            uint8_t msgType = 0;
            if (!g_client.ReceiveExact((char*)&msgType, 1)) {
                break;
            }

            switch (msgType) {
                case Rfb::SERVER_FRAMEBUFFER_UPDATE: {
                    uint8_t pad = 0;
                    uint16_t numRects = 0;
                    if (!g_client.ReceiveExact((char*)&pad, 1) || !g_client.ReceiveExact((char*)&numRects, 2)) {
                        return;
                    }
                    numRects = ntohs(numRects);

                    for (uint16_t i = 0; i < numRects; ++i) {
                        Rfb::RfbRectangleHeader rectHdr;
                        if (!g_client.ReceiveExact((char*)&rectHdr, sizeof(rectHdr))) {
                            return;
                        }

                        uint16_t rx = ntohs(rectHdr.x);
                        uint16_t ry = ntohs(rectHdr.y);
                        uint16_t rw = ntohs(rectHdr.width);
                        uint16_t rh = ntohs(rectHdr.height);
                        int32_t enc = (int32_t)ntohl((uint32_t)rectHdr.encodingType);

                        if (enc == Rfb::ENCODING_TIGHT) {
                            // Đọc 1 byte Compression Control
                            uint8_t compCtrl = 0;
                            if (!g_client.ReceiveExact((char*)&compCtrl, 1)) return;

                            if (compCtrl == 0x80) {
                                // Tight Solid Fill Rectangle (RFC 6143 / Tight spec: 3 bytes RGB)
                                uint8_t rgb[3] = {0};
                                if (!g_client.ReceiveExact((char*)rgb, 3)) return;
                                uint32_t color = 0xFF000000 | ((uint32_t)rgb[0] << 16) | ((uint32_t)rgb[1] << 8) | (uint32_t)rgb[2];
                                std::vector<uint32_t> fillBuf((size_t)rw * rh, color);
                                RemoteWindow::UpdateRawFrame((int)rx, (int)ry, (int)rw, (int)rh, fillBuf.data());

                                std::stringstream ss;
                                ss << "Rect=(" << rx << "," << ry << " " << rw << "x" << rh << ") Tight/Fill Solid Color";
                                LogPacket("RECV", "FramebufferUpdate", 3, ss.str());
                            } else {
                                // Tight JPEG (compCtrl 0x90) hoặc Sub-encoding có compact length
                                uint32_t jpegLen = 0;
                                uint8_t b1 = 0;
                                if (!g_client.ReceiveExact((char*)&b1, 1)) return;
                                jpegLen = b1 & 0x7F;

                                if (b1 & 0x80) {
                                    uint8_t b2 = 0;
                                    if (!g_client.ReceiveExact((char*)&b2, 1)) return;
                                    jpegLen |= (uint32_t)(b2 & 0x7F) << 7;

                                    if (b2 & 0x80) {
                                        uint8_t b3 = 0;
                                        if (!g_client.ReceiveExact((char*)&b3, 1)) return;
                                        jpegLen |= (uint32_t)b3 << 14;
                                    }
                                }

                                if (jpegLen > 0 && jpegLen <= 32 * 1024 * 1024) {
                                    jpegBuf.resize(jpegLen);
                                    if (!g_client.ReceiveExact(jpegBuf.data(), (int)jpegLen)) return;

                                    Protocol::VideoDeltaHeader delta = { (int16_t)rx, (int16_t)ry, (int16_t)rw, (int16_t)rh };
                                    RemoteWindow::UpdateFrame(delta, jpegBuf.data(), (int)jpegLen);

                                    std::stringstream ss;
                                    ss << "Rect=(" << rx << "," << ry << " " << rw << "x" << rh << ") Tight/JPEG " << (jpegLen / 1024) << "KB";
                                    LogPacket("RECV", "FramebufferUpdate", jpegLen, ss.str());
                                }
                            }
                        } else if (enc == Rfb::ENCODING_RAW) {
                            size_t rawBytes = (size_t)rw * rh * 4;
                            std::vector<char> rawPixels(rawBytes);
                            if (!g_client.ReceiveExact(rawPixels.data(), (int)rawBytes)) return;

                            RemoteWindow::UpdateRawFrame((int)rx, (int)ry, (int)rw, (int)rh, (const uint32_t*)rawPixels.data());

                            std::stringstream ss;
                            ss << "Rect=(" << rx << "," << ry << " " << rw << "x" << rh << ") Raw " << (rawBytes / 1024) << "KB";
                            LogPacket("RECV", "FramebufferUpdate", rawBytes, ss.str());
                        }
                    }

                    // Tiếp tục gửi FramebufferUpdateRequest để yêu cầu frame tiếp theo
                    RfbClient::RequestUpdate(true);
                    break;
                }

                case Rfb::SERVER_CUT_TEXT: {
                    char pad[3];
                    uint32_t len = 0;
                    if (!g_client.ReceiveExact(pad, 3) || !g_client.ReceiveExact((char*)&len, 4)) {
                        return;
                    }
                    len = ntohl(len);
                    if (len > 0 && len <= 1024 * 1024) {
                        std::vector<char> textBuf(len + 1, 0);
                        g_client.ReceiveExact(textBuf.data(), len);
                    }
                    break;
                }

                case Rfb::SERVER_BELL:
                    MessageBeep(MB_OK);
                    break;

                default:
                    break;
            }
        }
    }
} // namespace

namespace RfbClient {

    bool Connect(const std::string& ip, int port, std::string& outError) {
        Disconnect();

        if (!g_client.Connect(ip, port)) {
            outError = "Không thể kết nối tới VNC Server tại " + ip + ":" + std::to_string(port) + "!\n"
                       "Vui lòng đảm bảo Client đang chạy trên máy đích và cổng 5900 không bị chặn.";
            return false;
        }

        if (!PerformClientHandshake(outError)) {
            g_client.Close();
            return false;
        }

        g_isConnected.store(true, std::memory_order_release);
        g_shouldStop.store(false, std::memory_order_release);

        return true;
    }

    void Start() {
        if (!g_isConnected.load(std::memory_order_relaxed)) return;
        if (g_workerThread.joinable()) return;

        g_shouldStop.store(false, std::memory_order_release);
        g_workerThread = std::thread(WorkerLoop);

        // Yêu cầu khung hình đầy đủ (non-incremental = false) ngay khi RemoteWindow đã sẵn sàng
        RequestUpdate(false);
    }

    void Disconnect() {
        if (!g_isConnected) return;

        g_shouldStop.store(true, std::memory_order_release);
        g_isConnected.store(false, std::memory_order_release);
        SetState(Rfb::RFBSTATE_UNINITIALISED);

        g_client.Close();

        if (g_workerThread.joinable()) {
            g_workerThread.join();
        }
    }

    bool IsConnected() {
        return g_isConnected.load(std::memory_order_relaxed);
    }

    Rfb::RfbState GetState() {
        return g_state.load(std::memory_order_relaxed);
    }

    void SendPointer(uint8_t buttonMask, uint16_t x, uint16_t y) {
        if (!g_isConnected) return;

        Rfb::RfbPointerEvent pe;
        pe.messageType = Rfb::CLIENT_POINTER_EVENT;
        pe.buttonMask = buttonMask;
        pe.x = htons(x);
        pe.y = htons(y);

        g_client.SendRaw((const char*)&pe, sizeof(pe));
    }

    void SendKey(WPARAM vk, bool isDown, bool isShiftDown) {
        if (!g_isConnected) return;

        uint32_t keysym = WinVkToKeySym(vk, isShiftDown);
        if (keysym == 0) return;

        Rfb::RfbKeyEvent ke;
        ke.messageType = Rfb::CLIENT_KEY_EVENT;
        ke.downFlag = isDown ? 1 : 0;
        ke.padding = 0;
        ke.key = htonl(keysym);

        g_client.SendRaw((const char*)&ke, sizeof(ke));
    }

    void RequestUpdate(bool incremental) {
        if (!g_isConnected) return;

        Rfb::RfbFramebufferUpdateReq req;
        req.messageType = Rfb::CLIENT_FRAMEBUFFER_UPDATE_REQUEST;
        req.incremental = incremental ? 1 : 0;
        req.x = htons(0);
        req.y = htons(0);
        req.width = htons((uint16_t)g_serverWidth);
        req.height = htons((uint16_t)g_serverHeight);

        g_client.SendRaw((const char*)&req, sizeof(req));
    }

    void SetPacketLogCallback(PacketLogCallback cb) {
        g_logCallback = cb;
    }

    int GetServerWidth() { return g_serverWidth; }
    int GetServerHeight() { return g_serverHeight; }
    std::string GetServerDesktopName() { return g_serverDesktopName; }

} // namespace RfbClient
