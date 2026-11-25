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
#include "../Client/Core/RfbServerSession.h"
#include "../Client/Media/ScreenCapture.h"

int main() {
    std::cout << "Testing RFB Raw Fallback for RealVNC...\n";
    Network::Initialize();

    const int PORT = 5909;
    assert(RfbServer::Start(PORT, "Test Raw"));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    TcpClient client;
    assert(client.Connect("127.0.0.1", PORT));

    char ver[13] = {0};
    assert(client.ReceiveExact(ver, 12));
    assert(client.SendRaw(Rfb::VERSION_3_8_STRING, 12));

    uint8_t numSec = 0;
    assert(client.ReceiveExact((char*)&numSec, 1));
    std::vector<uint8_t> sec(numSec);
    assert(client.ReceiveExact((char*)sec.data(), numSec));
    uint8_t chosen = (uint8_t)Rfb::SECURITY_NONE;
    assert(client.SendRaw((char*)&chosen, 1));

    uint32_t secRes = 0;
    assert(client.ReceiveExact((char*)&secRes, 4));

    Rfb::RfbClientInit cInit = { 1 };
    assert(client.SendRaw((char*)&cInit, 1));

    Rfb::RfbServerInitHeader sInit;
    assert(client.ReceiveExact((char*)&sInit, sizeof(sInit)));
    uint16_t fbW = ntohs(sInit.framebufferWidth);
    uint16_t fbH = ntohs(sInit.framebufferHeight);
    uint32_t nameLen = ntohl(sInit.nameLength);
    std::vector<char> name(nameLen);
    assert(client.ReceiveExact(name.data(), nameLen));

    // SIMULATE REALVNC: ONLY ADVERTISE RAW (0)! DO NOT ADVERTISE TIGHT (7)!
    std::cout << "Simulating RealVNC: Sending SetEncodings = { ENCODING_RAW (0) }\n";
    int32_t encs[] = { (int32_t)htonl((uint32_t)Rfb::ENCODING_RAW) };
    Rfb::RfbSetEncodingsHeader encHdr;
    encHdr.messageType = Rfb::CLIENT_SET_ENCODINGS;
    encHdr.padding = 0;
    encHdr.numEncodings = htons(1);
    std::vector<char> encPkt(sizeof(encHdr) + sizeof(encs));
    std::memcpy(encPkt.data(), &encHdr, sizeof(encHdr));
    std::memcpy(encPkt.data() + sizeof(encHdr), encs, sizeof(encs));
    assert(client.SendRaw(encPkt.data(), (int)encPkt.size()));

    // Request full frame
    Rfb::RfbFramebufferUpdateReq req;
    req.messageType = Rfb::CLIENT_FRAMEBUFFER_UPDATE_REQUEST;
    req.incremental = 0;
    req.x = 0; req.y = 0;
    req.width = htons(fbW); req.height = htons(fbH);
    assert(client.SendRaw((char*)&req, sizeof(req)));

    // Receive FramebufferUpdate
    uint8_t msgType = 0;
    assert(client.ReceiveExact((char*)&msgType, 1));
    assert(msgType == Rfb::SERVER_FRAMEBUFFER_UPDATE);

    uint8_t pad = 0;
    uint16_t numRects = 0;
    assert(client.ReceiveExact((char*)&pad, 1));
    assert(client.ReceiveExact((char*)&numRects, 2));
    numRects = ntohs(numRects);
    std::cout << "Received FramebufferUpdate with " << numRects << " rects\n";
    assert(numRects >= 1);

    for (int i = 0; i < numRects; ++i) {
        Rfb::RfbRectangleHeader rh;
        assert(client.ReceiveExact((char*)&rh, sizeof(rh)));
        uint16_t rx = ntohs(rh.x);
        uint16_t ry = ntohs(rh.y);
        uint16_t rw = ntohs(rh.width);
        uint16_t rhh = ntohs(rh.height);
        int32_t enc = (int32_t)ntohl((uint32_t)rh.encodingType);
        std::cout << "Rect " << i << ": (" << rx << "," << ry << ") " << rw << "x" << rhh << ", Encoding=" << enc << "\n";
        
        // CRITICAL CHECK: Encoding MUST be ENCODING_RAW (0)!
        assert(enc == Rfb::ENCODING_RAW);

        size_t expectedBytes = (size_t)rw * rhh * 4;
        std::cout << "Reading Raw pixel bytes: " << expectedBytes << " bytes (" << (expectedBytes / 1024) << " KB)\n";
        std::vector<char> rawPixels(expectedBytes);
        assert(client.ReceiveExact(rawPixels.data(), (int)expectedBytes));
        std::cout << "Successfully read " << rawPixels.size() << " bytes of raw pixels!\n";
    }

    client.Close();
    RfbServer::Stop();
    Network::Cleanup();
    std::cout << "[SUCCESS] REALVNC RAW ENCODING 0 TEST 100% PASS!\n";
    return 0;
}
