#include "RfbServerSession.h"
#include "../Media/ScreenCapture.h"
#include "../System/InputInjector.h"

#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <cstring>

namespace {
    std::atomic<bool> g_isRunning(false);
    std::atomic<bool> g_isClientConnected(false);
    std::atomic<bool> g_updateRequested(false);
    std::atomic<bool> g_hasReceivedInitialRequest(false);
    std::atomic<bool> g_encodingsReceived(false);
    std::atomic<bool> g_forceFull(true);
    std::atomic<bool> g_useTightJpeg(false);

    TcpServer g_server;

    std::thread g_acceptThread;
    std::thread g_receiveThread;
    std::thread g_transmitThread;

    std::string g_serverName = "PBL4 VNC Server (DirectX 11)";
    int g_listenPort = Rfb::PORT_VNC;

    uint8_t g_prevButtonMask = 0;
    int16_t g_prevMouseX = -1;
    int16_t g_prevMouseY = -1;

    bool PerformHandshake() {
        // 1. Gửi ProtocolVersion ("RFB 003.008\n", 12 bytes)
        if (!g_server.SendRaw(Rfb::VERSION_3_8_STRING, (int)Rfb::VERSION_STRING_LENGTH)) {
            std::cerr << "[RFB Server] Lỗi gửi ProtocolVersion tới Client!\n";
            return false;
        }

        // 2. Đọc ProtocolVersion từ Client (12 bytes)
        char clientVer[13] = { 0 };
        if (!g_server.ReceiveExact(clientVer, (int)Rfb::VERSION_STRING_LENGTH)) {
            std::cerr << "[RFB Server] Lỗi đọc ProtocolVersion từ Client!\n";
            return false;
        }
        std::cout << "[RFB Server] Client kết nối với phiên bản: " << clientVer;

        // 3. Gửi danh sách Security Types (1 loại: None)
        uint8_t secTypes[2] = { 1, (uint8_t)Rfb::SECURITY_NONE };
        if (!g_server.SendRaw((const char*)secTypes, sizeof(secTypes))) {
            return false;
        }

        // 4. Đọc loại bảo mật Client chọn (1 byte)
        uint8_t chosenSec = 0;
        if (!g_server.ReceiveExact((char*)&chosenSec, 1)) {
            return false;
        }
        if (chosenSec != Rfb::SECURITY_NONE) {
            std::cerr << "[RFB Server] Client chọn loại bảo mật không được hỗ trợ: " << (int)chosenSec << "\n";
            return false;
        }

        // 5. Gửi SecurityResult (4 bytes Big-Endian: 0 = OK)
        uint32_t secResult = htonl(Rfb::SECURITY_RESULT_OK);
        if (!g_server.SendRaw((const char*)&secResult, sizeof(secResult))) {
            return false;
        }

        // 6. Đọc ClientInit (1 byte shared-flag)
        Rfb::RfbClientInit clientInit = { 0 };
        if (!g_server.ReceiveExact((char*)&clientInit, sizeof(clientInit))) {
            return false;
        }
        std::cout << "[RFB Server] Nhận ClientInit (shared = " << (int)clientInit.sharedFlag << ")\n";

        // 7. Gửi ServerInit (Width, Height, PixelFormat, Name)
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        if (screenW <= 0 || screenH <= 0) {
            screenW = 1920;
            screenH = 1080;
        }

        Rfb::RfbServerInitHeader sInit = {};
        sInit.framebufferWidth = htons((uint16_t)screenW);
        sInit.framebufferHeight = htons((uint16_t)screenH);

        // Chuẩn định dạng màu BGRX 32-bit tối ưu của Windows & DirectX 11
        sInit.pixelFormat.bitsPerPixel = 32;
        sInit.pixelFormat.depth = 24;
        sInit.pixelFormat.bigEndianFlag = 0; // Little-Endian cho x86 Windows
        sInit.pixelFormat.trueColorFlag = 1;
        sInit.pixelFormat.redMax = htons(255);
        sInit.pixelFormat.greenMax = htons(255);
        sInit.pixelFormat.blueMax = htons(255);
        sInit.pixelFormat.redShift = 16;
        sInit.pixelFormat.greenShift = 8;
        sInit.pixelFormat.blueShift = 0;

        uint32_t nameLen = (uint32_t)g_serverName.size();
        sInit.nameLength = htonl(nameLen);

        std::vector<char> initPayload(sizeof(sInit) + nameLen);
        std::memcpy(initPayload.data(), &sInit, sizeof(sInit));
        std::memcpy(initPayload.data() + sizeof(sInit), g_serverName.data(), nameLen);

        if (!g_server.SendRaw(initPayload.data(), (int)initPayload.size())) {
            std::cerr << "[RFB Server] Lỗi gửi ServerInit!\n";
            return false;
        }

        std::cout << "[RFB Server] Bắt tay RFB 3.8 hoàn tất thành công! Sẵn sàng nhận lệnh.\n";
        return true;
    }

    void ProcessPointerEvent(uint8_t buttonMask, int16_t x, int16_t y) {
        // Ánh xạ sang cấu trúc MousePayload của InputInjector
        Protocol::MousePayload mp = { 0, 0, x, y, 0 };

        // 1. Xử lý nút cuộn chuột (Wheel)
        if (buttonMask & Rfb::WHEEL_UP) {
            mp.actionType = Protocol::MOUSE_ACTION_WHEEL;
            mp.wheelDelta = 120;
            InputInjector::InjectMouse(mp);
        } else if (buttonMask & Rfb::WHEEL_DOWN) {
            mp.actionType = Protocol::MOUSE_ACTION_WHEEL;
            mp.wheelDelta = -120;
            InputInjector::InjectMouse(mp);
        }

        // 2. Xử lý click/nhả các nút chuột
        uint8_t changed = buttonMask ^ g_prevButtonMask;

        if (changed & Rfb::BUTTON_LEFT) {
            mp.buttonType = Protocol::BUTTON_LEFT;
            mp.actionType = (buttonMask & Rfb::BUTTON_LEFT) ? Protocol::MOUSE_ACTION_PRESS : Protocol::MOUSE_ACTION_RELEASE;
            InputInjector::InjectMouse(mp);
        }

        if (changed & Rfb::BUTTON_RIGHT) {
            mp.buttonType = Protocol::BUTTON_RIGHT;
            mp.actionType = (buttonMask & Rfb::BUTTON_RIGHT) ? Protocol::MOUSE_ACTION_PRESS : Protocol::MOUSE_ACTION_RELEASE;
            InputInjector::InjectMouse(mp);
        }

        if (changed & Rfb::BUTTON_MIDDLE) {
            mp.buttonType = Protocol::BUTTON_MIDDLE;
            mp.actionType = (buttonMask & Rfb::BUTTON_MIDDLE) ? Protocol::MOUSE_ACTION_PRESS : Protocol::MOUSE_ACTION_RELEASE;
            InputInjector::InjectMouse(mp);
        }

        // 3. Xử lý di chuyển chuột
        if (x != g_prevMouseX || y != g_prevMouseY) {
            mp.actionType = Protocol::MOUSE_ACTION_MOVE;
            mp.buttonType = Protocol::BUTTON_NONE;
            InputInjector::InjectMouse(mp);
        }

        g_prevButtonMask = buttonMask;
        g_prevMouseX = x;
        g_prevMouseY = y;
    }

    void ReceiveLoop() {
        while (g_isRunning && g_isClientConnected) {
            uint8_t msgType = 0;
            if (!g_server.ReceiveExact((char*)&msgType, 1)) {
                break;
            }

            switch (msgType) {
                case Rfb::CLIENT_SET_PIXEL_FORMAT: {
                    char pad[3];
                    Rfb::RfbPixelFormat fmt;
                    if (!g_server.ReceiveExact(pad, 3) || !g_server.ReceiveExact((char*)&fmt, sizeof(fmt))) {
                        return;
                    }
                    break;
                }

                case Rfb::CLIENT_SET_ENCODINGS: {
                    uint8_t pad = 0;
                    uint16_t numEnc = 0;
                    if (!g_server.ReceiveExact((char*)&pad, 1) || !g_server.ReceiveExact((char*)&numEnc, 2)) {
                        return;
                    }
                    numEnc = ntohs(numEnc);
                    std::vector<int32_t> encodings(numEnc);
                    if (numEnc > 0) {
                        if (!g_server.ReceiveExact((char*)encodings.data(), numEnc * sizeof(int32_t))) {
                            return;
                        }
                    }

                    bool clientSupportsTight = false;
                    for (int32_t enc : encodings) {
                        int32_t hostEnc = (int32_t)ntohl((uint32_t)enc);
                        if (hostEnc == Rfb::ENCODING_TIGHT) {
                            clientSupportsTight = true;
                        }
                    }
                    g_useTightJpeg.store(clientSupportsTight, std::memory_order_release);
                    g_encodingsReceived.store(true, std::memory_order_release);
                    std::cout << "[RFB Server] Client Encodings received (Tight JPEG = " 
                              << (clientSupportsTight ? "YES" : "NO") << ")\n";
                    break;
                }

                case Rfb::CLIENT_FRAMEBUFFER_UPDATE_REQUEST: {
                    uint8_t incremental = 0;
                    uint16_t x, y, w, h;
                    if (!g_server.ReceiveExact((char*)&incremental, 1) ||
                        !g_server.ReceiveExact((char*)&x, 2) ||
                        !g_server.ReceiveExact((char*)&y, 2) ||
                        !g_server.ReceiveExact((char*)&w, 2) ||
                        !g_server.ReceiveExact((char*)&h, 2)) {
                        return;
                    }

                    g_hasReceivedInitialRequest.store(true, std::memory_order_release);
                    if (incremental == 0) {
                        g_forceFull.store(true, std::memory_order_release);
                    }
                    g_updateRequested.store(true, std::memory_order_release);
                    break;
                }

                case Rfb::CLIENT_KEY_EVENT: {
                    uint8_t downFlag = 0;
                    uint16_t pad = 0;
                    uint32_t keysym = 0;
                    if (!g_server.ReceiveExact((char*)&downFlag, 1) ||
                        !g_server.ReceiveExact((char*)&pad, 2) ||
                        !g_server.ReceiveExact((char*)&keysym, 4)) {
                        return;
                    }
                    keysym = ntohl(keysym);
                    WPARAM vk = KeySymToWinVk(keysym);
                    if (vk != 0) {
                        InputInjector::InjectKeyboard({ (uint32_t)vk, downFlag });
                    }
                    break;
                }

                case Rfb::CLIENT_POINTER_EVENT: {
                    uint8_t btnMask = 0;
                    uint16_t x = 0, y = 0;
                    if (!g_server.ReceiveExact((char*)&btnMask, 1) ||
                        !g_server.ReceiveExact((char*)&x, 2) ||
                        !g_server.ReceiveExact((char*)&y, 2)) {
                        return;
                    }
                    ProcessPointerEvent(btnMask, (int16_t)ntohs(x), (int16_t)ntohs(y));
                    break;
                }

                case Rfb::CLIENT_CUT_TEXT: {
                    char pad[3];
                    uint32_t len = 0;
                    if (!g_server.ReceiveExact(pad, 3) || !g_server.ReceiveExact((char*)&len, 4)) {
                        return;
                    }
                    len = ntohl(len);
                    if (len > 0 && len <= 1024 * 1024) {
                        std::vector<char> textBuf(len + 1, 0);
                        g_server.ReceiveExact(textBuf.data(), len);
                        // Cập nhật clipboard nếu có
                    }
                    break;
                }

                default:
                    std::cerr << "[RFB Server] Bỏ qua gói tin không nhận diện được: " << (int)msgType << "\n";
                    break;
            }
        }
    }

    void TransmitLoop() {
        if (!ScreenCapture::Initialize()) return;
        ScreenCapture::ForceFullFrame();

        std::vector<char> frameBuffer;
        frameBuffer.reserve(512 * 1024);
        int x, y, w, h;
        uint64_t lastEmptySendTime = GetTickCount64();

        auto nextFrameTime = std::chrono::steady_clock::now();
        const std::chrono::microseconds frameInterval(1000000 / 60); // 60 FPS

        while (g_isRunning && g_isClientConnected) {
            nextFrameTime += frameInterval;

            if (g_forceFull.exchange(false, std::memory_order_acq_rel)) {
                ScreenCapture::ForceFullFrame();
            }

            // Gửi khung hình khi có yêu cầu từ client (hoặc liên tục khi có chuyển động)
            bool canSend = g_encodingsReceived.load(std::memory_order_acquire) &&
                           (g_updateRequested.load(std::memory_order_acquire) || g_hasReceivedInitialRequest.load(std::memory_order_acquire));
            if (canSend) {
                if (ScreenCapture::CaptureFrame(frameBuffer, x, y, w, h)) {
                    bool useTight = g_useTightJpeg.load(std::memory_order_acquire);
                    std::vector<char> rectPayload;

                    if (useTight) {
                        // Trích xuất JPEG Data từ frameBuffer (sau VideoDeltaHeader)
                        if (frameBuffer.size() > sizeof(Protocol::VideoDeltaHeader)) {
                            const char* jpegPtr = frameBuffer.data() + sizeof(Protocol::VideoDeltaHeader);
                            size_t jpegLen = frameBuffer.size() - sizeof(Protocol::VideoDeltaHeader);

                            rectPayload = Rfb::BuildTightJpegRectangle(
                                (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h,
                                jpegPtr, jpegLen
                            );
                        }
                    } else {
                        // Raw Encoding (RFC 6143 §7.7.1) cho RealVNC Connect Viewer và các client không hỗ trợ Tight
                        int screenW = 0, screenH = 0;
                        const uint32_t* pixels = ScreenCapture::GetCurrentFramePixels(screenW, screenH);
                        if (pixels && screenW > 0 && screenH > 0) {
                            rectPayload = Rfb::BuildRawRectangle(
                                (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h,
                                pixels, screenW
                            );
                        }
                    }

                    if (!rectPayload.empty()) {
                        Rfb::RfbFramebufferUpdateHeader updateHdr = Rfb::BuildUpdateHeader(1);
                        std::vector<char> packet;
                        packet.reserve(sizeof(updateHdr) + rectPayload.size());
                        const char* uhPtr = reinterpret_cast<const char*>(&updateHdr);
                        packet.insert(packet.end(), uhPtr, uhPtr + sizeof(updateHdr));
                        packet.insert(packet.end(), rectPayload.begin(), rectPayload.end());

                        if (!g_server.SendRaw(packet.data(), (int)packet.size())) {
                            break;
                        }

                        g_updateRequested.store(false, std::memory_order_release);
                        lastEmptySendTime = GetTickCount64();
                    }
                } else if (g_updateRequested.load(std::memory_order_acquire)) {
                    // Khi màn hình tĩnh hoàn toàn (0% GPU/CPU idle), nếu sau 500ms vẫn chưa có thay đổi,
                    // gửi gói rỗng (numRectangles = 0) để đáp ứng yêu cầu của client và duy trì keep-alive socket
                    uint64_t nowMs = GetTickCount64();
                    if (nowMs - lastEmptySendTime >= 500) {
                        Rfb::RfbFramebufferUpdateHeader emptyHdr = Rfb::BuildUpdateHeader(0);
                        if (!g_server.SendRaw(reinterpret_cast<const char*>(&emptyHdr), sizeof(emptyHdr))) {
                            break;
                        }
                        g_updateRequested.store(false, std::memory_order_release);
                        lastEmptySendTime = nowMs;
                    }
                }
            }

            // Frame Pacer duy trì nhịp độ chuẩn xác
            auto now = std::chrono::steady_clock::now();
            if (nextFrameTime > now) {
                auto sleepUs = std::chrono::duration_cast<std::chrono::microseconds>(nextFrameTime - now).count();
                if (sleepUs > 2000) {
                    std::this_thread::sleep_for(std::chrono::microseconds(sleepUs - 1000));
                }
                while (std::chrono::steady_clock::now() < nextFrameTime) {
                    std::this_thread::yield();
                }
            } else {
                nextFrameTime = now;
            }
        }

        ScreenCapture::Cleanup();
    }

    void AcceptLoop() {
        while (g_isRunning) {
            std::cout << "[RFB Server] Đang lắng nghe kết nối VNC tại cổng " << g_listenPort << "...\n";
            if (!g_server.AcceptConnection(500)) {
                if (!g_isRunning) break;
                continue;
            }

            std::cout << "[RFB Server] Có kết nối mới! Tiến hành bắt tay RFB 3.8...\n";
            if (!PerformHandshake()) {
                g_server.CloseClient();
                continue;
            }

            g_isClientConnected.store(true, std::memory_order_release);
            g_updateRequested.store(false, std::memory_order_release);
            g_forceFull.store(true, std::memory_order_release);
            g_hasReceivedInitialRequest.store(false, std::memory_order_release);
            g_encodingsReceived.store(false, std::memory_order_release);
            g_useTightJpeg.store(false, std::memory_order_release);

            g_receiveThread = std::thread(ReceiveLoop);
            g_transmitThread = std::thread(TransmitLoop);

            if (g_receiveThread.joinable()) g_receiveThread.join();
            if (g_transmitThread.joinable()) g_transmitThread.join();

            g_isClientConnected.store(false, std::memory_order_release);
            g_server.CloseClient();
            std::cout << "[RFB Server] Client đã ngắt kết nối. Sẵn sàng cho phiên tiếp theo.\n";
        }
    }
} // namespace

namespace RfbServer {

    bool Start(int port, const std::string& serverName) {
        if (g_isRunning) return true;

        g_listenPort = port;
        g_serverName = serverName;
        g_isRunning.store(true, std::memory_order_release);

        if (!g_server.Start(g_listenPort)) {
            std::cerr << "[RFB Server] Không thể mở cổng lắng nghe " << g_listenPort << "!\n";
            g_isRunning.store(false, std::memory_order_release);
            return false;
        }

        g_acceptThread = std::thread(AcceptLoop);
        return true;
    }

    void Stop() {
        if (!g_isRunning) return;

        g_isRunning.store(false, std::memory_order_release);
        g_isClientConnected.store(false, std::memory_order_release);

        g_server.Close();

        if (g_acceptThread.joinable()) g_acceptThread.join();
        if (g_receiveThread.joinable()) g_receiveThread.join();
        if (g_transmitThread.joinable()) g_transmitThread.join();
    }

    bool IsRunning() {
        return g_isRunning.load(std::memory_order_relaxed);
    }

    bool IsClientConnected() {
        return g_isClientConnected.load(std::memory_order_relaxed);
    }

    void ForceFullUpdate() {
        g_forceFull.store(true, std::memory_order_release);
        g_updateRequested.store(true, std::memory_order_release);
    }

} // namespace RfbServer
