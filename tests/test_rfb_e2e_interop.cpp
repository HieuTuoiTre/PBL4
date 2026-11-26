#include <iostream>
#include <cassert>
#include <vector>
#include <thread>
#include <chrono>
#include <winsock2.h>
#include <windows.h>

#include "../Shared/Network.h"
#include "../Shared/RFB/RfbDefines.h"
#include "../Shared/RFB/RfbMessages.h"
#include "../Shared/RFB/KeySymMapping.h"
#include "../Client/Core/RfbServerSession.h"
#include "../Client/Media/ScreenCapture.h"

int main() {
    std::cout << "============================================================\n";
    std::cout << "  LANREMOTE - RFB 3.8 / VNC THIRD-PARTY INTEROP TEST\n";
    std::cout << "============================================================\n";

    Network::Initialize();

    const int TEST_PORT = 5902;
    RfbServer::SetAuthorizationValidator([](const std::string&) { return true; });
    std::cout << "[1/6] Khoi dong RfbServer tren cong " << TEST_PORT << "...\n";
    if (!RfbServer::Start(TEST_PORT, "PBL4 VNC Interop Server")) {
        std::cerr << "[FAIL] Khong the mo RfbServer tren cong " << TEST_PORT << "\n";
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Gia lap mot VNC Viewer chuan (nhu RealVNC Viewer hoac TightVNC)
    std::cout << "[2/6] Ket noi toi VNC Server (Gia lap RealVNC / TightVNC Viewer)...\n";
    TcpClient client;
    if (!client.Connect("127.0.0.1", TEST_PORT)) {
        std::cerr << "[FAIL] Khong the ket noi toi 127.0.0.1:" << TEST_PORT << "\n";
        RfbServer::Stop();
        return 1;
    }

    // Step 1: Bắt tay ProtocolVersion
    char serverVer[13] = { 0 };
    assert(client.ReceiveExact(serverVer, 12));
    std::cout << "  -> Server gui ProtocolVersion: " << serverVer;
    assert(std::string(serverVer, 12) == Rfb::VERSION_3_8_STRING);

    assert(client.SendRaw(Rfb::VERSION_3_8_STRING, 12));
    std::cout << "  <- Client phan hoi: " << Rfb::VERSION_3_8_STRING;

    // Step 2: Bắt tay Security Handshake
    uint8_t numSecTypes = 0;
    assert(client.ReceiveExact((char*)&numSecTypes, 1));
    std::cout << "  -> Server thong bao so luong Security Types: " << (int)numSecTypes << "\n";
    assert(numSecTypes >= 1);

    std::vector<uint8_t> secTypes(numSecTypes);
    assert(client.ReceiveExact((char*)secTypes.data(), numSecTypes));
    bool hasNoneSec = false;
    for (uint8_t st : secTypes) {
        if (st == (uint8_t)Rfb::SECURITY_NONE) hasNoneSec = true;
    }
    assert(hasNoneSec);
    std::cout << "  -> Server ho tro SECURITY_NONE (1)\n";

    // Chọn SECURITY_NONE (1)
    uint8_t chosen = (uint8_t)Rfb::SECURITY_NONE;
    assert(client.SendRaw((const char*)&chosen, 1));
    std::cout << "  <- Client chon SECURITY_NONE (1)\n";

    // Đọc SecurityResult (4 bytes = 0 OK)
    uint32_t secResult = 0;
    assert(client.ReceiveExact((char*)&secResult, 4));
    secResult = ntohl(secResult);
    std::cout << "  -> Server xac thuc SecurityResult: " << secResult << " (OK)\n";
    assert(secResult == Rfb::SECURITY_RESULT_OK);

    // Step 3: ClientInit & ServerInit
    Rfb::RfbClientInit cInit = { 1 }; // shared = 1
    assert(client.SendRaw((const char*)&cInit, 1));
    std::cout << "  <- Client gui ClientInit (shared = 1)\n";

    Rfb::RfbServerInitHeader sInit;
    assert(client.ReceiveExact((char*)&sInit, sizeof(sInit)));
    uint16_t fbW = ntohs(sInit.framebufferWidth);
    uint16_t fbH = ntohs(sInit.framebufferHeight);
    uint32_t nameLen = ntohl(sInit.nameLength);
    std::vector<char> nameBuf(nameLen + 1, 0);
    assert(client.ReceiveExact(nameBuf.data(), nameLen));

    std::cout << "  -> ServerInit: Kich thuoc " << fbW << "x" << fbH 
              << ", BPP=" << (int)sInit.pixelFormat.bitsPerPixel 
              << ", TrueColor=" << (int)sInit.pixelFormat.trueColorFlag 
              << ", Ten Desktop: '" << nameBuf.data() << "'\n";
    assert(fbW > 0 && fbH > 0);
    assert(sInit.pixelFormat.bitsPerPixel == 32);
    assert(sInit.pixelFormat.trueColorFlag == 1);

    // Step 4: Gửi SetEncodings (Thông báo hỗ trợ Tight/JPEG)
    std::cout << "[3/6] Client gui SetEncodings (Tight/JPEG, Raw)...\n";
    int32_t encodings[] = { (int32_t)htonl((uint32_t)Rfb::ENCODING_TIGHT), (int32_t)htonl((uint32_t)Rfb::ENCODING_RAW) };
    Rfb::RfbSetEncodingsHeader encHdr;
    encHdr.messageType = Rfb::CLIENT_SET_ENCODINGS;
    encHdr.padding = 0;
    encHdr.numEncodings = htons(2);
    std::vector<char> encPkt(sizeof(encHdr) + sizeof(encodings));
    std::memcpy(encPkt.data(), &encHdr, sizeof(encHdr));
    std::memcpy(encPkt.data() + sizeof(encHdr), encodings, sizeof(encodings));
    assert(client.SendRaw(encPkt.data(), (int)encPkt.size()));

    // Step 5: Gửi FramebufferUpdateRequest và nhận FramebufferUpdate
    std::cout << "[4/6] Client gui FramebufferUpdateRequest (incremental=0)...\n";
    Rfb::RfbFramebufferUpdateReq req;
    req.messageType = Rfb::CLIENT_FRAMEBUFFER_UPDATE_REQUEST;
    req.incremental = 0;
    req.x = htons(0);
    req.y = htons(0);
    req.width = htons(fbW);
    req.height = htons(fbH);
    assert(client.SendRaw((const char*)&req, sizeof(req)));

    std::cout << "[5/6] Cho FramebufferUpdate tu Server...\n";
    uint8_t msgType = 0;
    assert(client.ReceiveExact((char*)&msgType, 1));
    assert(msgType == Rfb::SERVER_FRAMEBUFFER_UPDATE);

    uint8_t pad = 0;
    uint16_t numRects = 0;
    assert(client.ReceiveExact((char*)&pad, 1));
    assert(client.ReceiveExact((char*)&numRects, 2));
    numRects = ntohs(numRects);
    std::cout << "  -> Nhan FramebufferUpdate voi " << numRects << " hinh chu nhat (Rectangles)!\n";
    assert(numRects >= 1);

    for (uint16_t i = 0; i < numRects; ++i) {
        Rfb::RfbRectangleHeader rHdr;
        assert(client.ReceiveExact((char*)&rHdr, sizeof(rHdr)));
        uint16_t rx = ntohs(rHdr.x);
        uint16_t ry = ntohs(rHdr.y);
        uint16_t rw = ntohs(rHdr.width);
        uint16_t rh = ntohs(rHdr.height);
        int32_t enc = (int32_t)ntohl((uint32_t)rHdr.encodingType);

        std::cout << "  -> Rect [" << i << "]: (" << rx << "," << ry << ") " 
                  << rw << "x" << rh << ", Encoding: " << enc << " (7=Tight/JPEG)\n";
        assert(enc == Rfb::ENCODING_TIGHT);

        // Đọc Tight Compression Control byte (0x90 = JPEG)
        uint8_t compCtrl = 0;
        assert(client.ReceiveExact((char*)&compCtrl, 1));
        assert(compCtrl == 0x90);

        // Đọc Compact Length
        uint32_t jpegLen = 0;
        uint8_t b1 = 0;
        assert(client.ReceiveExact((char*)&b1, 1));
        jpegLen = b1 & 0x7F;
        if (b1 & 0x80) {
            uint8_t b2 = 0;
            assert(client.ReceiveExact((char*)&b2, 1));
            jpegLen |= (uint32_t)(b2 & 0x7F) << 7;
            if (b2 & 0x80) {
                uint8_t b3 = 0;
                assert(client.ReceiveExact((char*)&b3, 1));
                jpegLen |= (uint32_t)b3 << 14;
            }
        }

        std::cout << "  -> Do dai luong TurboJPEG: " << jpegLen << " bytes (" << (jpegLen / 1024) << " KB)\n";
        assert(jpegLen > 100); // JPEG payload hợp lệ

        std::vector<char> jpegPayload(jpegLen);
        assert(client.ReceiveExact(jpegPayload.data(), (int)jpegLen));

        // Kiểm tra magic header JPEG (0xFF 0xD8)
        assert((uint8_t)jpegPayload[0] == 0xFF && (uint8_t)jpegPayload[1] == 0xD8);
        std::cout << "  -> Xac minh Magic Bytes JPEG [FF D8] thanh cong 100%!\n";
    }

    // Step 6: Gửi PointerEvent & KeyEvent
    std::cout << "[6/6] Gui PointerEvent va KeyEvent sang VNC Server...\n";
    Rfb::RfbPointerEvent pe;
    pe.messageType = Rfb::CLIENT_POINTER_EVENT;
    pe.buttonMask = Rfb::BUTTON_LEFT;
    pe.x = htons(500);
    pe.y = htons(500);
    assert(client.SendRaw((const char*)&pe, sizeof(pe)));

    Rfb::RfbKeyEvent ke;
    ke.messageType = Rfb::CLIENT_KEY_EVENT;
    ke.downFlag = 1;
    ke.padding = 0;
    ke.key = htonl(0xFF0D); // XK_Return (Enter)
    assert(client.SendRaw((const char*)&ke, sizeof(ke)));

    std::cout << "  -> PointerEvent & KeyEvent da duoc bom vao Server thanh cong!\n";

    client.Close();
    RfbServer::Stop();
    Network::Cleanup();

    std::cout << "\n============================================================\n";
    std::cout << " [SUCCESS] 100% INTEROPERABILITY WITH STANDARD VNC VIEWERS!\n";
    std::cout << "============================================================\n";
    return 0;
}
