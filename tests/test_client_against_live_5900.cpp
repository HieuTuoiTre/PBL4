#include <iostream>
#include <cassert>
#include <vector>
#include <winsock2.h>
#include <windows.h>

#include "../Shared/Network.h"
#include "../Shared/RFB/RfbDefines.h"
#include "../Shared/RFB/RfbMessages.h"

int main() {
    Network::Initialize();
    TcpClient client;
    if (!client.Connect("127.0.0.1", 5900)) {
        std::cerr << "Connect failed\n";
        return 1;
    }

    char serverVer[13] = {0};
    assert(client.ReceiveExact(serverVer, 12));
    std::cout << "Server ver: " << serverVer;
    assert(client.SendRaw(Rfb::VERSION_3_8_STRING, 12));

    uint8_t numSecTypes = 0;
    assert(client.ReceiveExact((char*)&numSecTypes, 1));
    std::vector<uint8_t> sec(numSecTypes);
    assert(client.ReceiveExact((char*)sec.data(), numSecTypes));
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
    std::vector<char> name(nameLen + 1, 0);
    assert(client.ReceiveExact(name.data(), nameLen));
    std::cout << "Desktop: " << fbW << "x" << fbH << " '" << name.data() << "'\n";

    // SetEncodings
    int32_t encs[] = { (int32_t)htonl((uint32_t)Rfb::ENCODING_TIGHT), (int32_t)htonl((uint32_t)Rfb::ENCODING_RAW) };
    Rfb::RfbSetEncodingsHeader encHdr;
    encHdr.messageType = Rfb::CLIENT_SET_ENCODINGS;
    encHdr.padding = 0;
    encHdr.numEncodings = htons(2);
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
    std::cout << "Received msgType: " << (int)msgType << "\n";
    assert(msgType == Rfb::SERVER_FRAMEBUFFER_UPDATE);

    uint8_t pad = 0;
    uint16_t numRects = 0;
    assert(client.ReceiveExact((char*)&pad, 1));
    assert(client.ReceiveExact((char*)&numRects, 2));
    numRects = ntohs(numRects);
    std::cout << "Rectangles count: " << numRects << "\n";

    for (int i = 0; i < numRects; ++i) {
        Rfb::RfbRectangleHeader rh;
        assert(client.ReceiveExact((char*)&rh, sizeof(rh)));
        int32_t enc = (int32_t)ntohl((uint32_t)rh.encodingType);
        std::cout << "Rect " << i << " enc=" << enc << "\n";
        if (enc == Rfb::ENCODING_TIGHT) {
            uint8_t compCtrl = 0;
            assert(client.ReceiveExact((char*)&compCtrl, 1));
            uint8_t b1 = 0;
            assert(client.ReceiveExact((char*)&b1, 1));
            uint32_t len = b1 & 0x7F;
            if (b1 & 0x80) {
                uint8_t b2 = 0;
                assert(client.ReceiveExact((char*)&b2, 1));
                len |= (uint32_t)(b2 & 0x7F) << 7;
                if (b2 & 0x80) {
                    uint8_t b3 = 0;
                    assert(client.ReceiveExact((char*)&b3, 1));
                    len |= (uint32_t)b3 << 14;
                }
            }
            std::cout << "Tight JPEG len: " << len << "\n";
            std::vector<char> jpeg(len);
            assert(client.ReceiveExact(jpeg.data(), (int)len));
            std::cout << "Magic bytes: 0x" << std::hex << (int)(uint8_t)jpeg[0] << " 0x" << (int)(uint8_t)jpeg[1] << std::dec << "\n";
        }
    }

    client.Close();
    Network::Cleanup();
    std::cout << "ALL OK!\n";
    return 0;
}
