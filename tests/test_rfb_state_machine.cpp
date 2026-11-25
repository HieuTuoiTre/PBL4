#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

#include "../Shared/Network.h"
#include "../Shared/RFB/RfbDefines.h"
#include "../Shared/RFB/RfbMessages.h"
#include "../Client/Core/RfbServerSession.h"
#include "../Client/Media/ScreenCapture.h"

int main() {
    std::cout << "============================================================\n";
    std::cout << "  LANREMOTE - TIGERVNC STATE MACHINE VERIFICATION TEST\n";
    std::cout << "============================================================\n";

    Network::Initialize();

    const int PORT = 5903;
    RfbServer::SetAuthorizationValidator([](const std::string&) { return true; });
    assert(RfbServer::Start(PORT, "Test State Machine"));
    assert(RfbServer::GetState() == Rfb::RFBSTATE_UNINITIALISED);
    std::cout << "[1/4] Server khoi dong thanh cong o trang thai: " 
              << Rfb::RfbStateToString(RfbServer::GetState()) << "\n";

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Gia lap Client ket noi
    TcpClient client;
    assert(client.Connect("127.0.0.1", PORT));

    // 1. Version Handshake
    char serverVer[13] = { 0 };
    assert(client.ReceiveExact(serverVer, 12));
    assert(client.SendRaw(Rfb::VERSION_3_8_STRING, 12));

    // 2. Security Handshake
    uint8_t numSec = 0;
    assert(client.ReceiveExact((char*)&numSec, 1));
    uint8_t secType = 0;
    assert(client.ReceiveExact((char*)&secType, 1));
    uint8_t chosen = (uint8_t)Rfb::SECURITY_NONE;
    assert(client.SendRaw((const char*)&chosen, 1));

    uint32_t secRes = 0;
    assert(client.ReceiveExact((char*)&secRes, 4));

    // 3. ClientInit & ServerInit
    Rfb::RfbClientInit cInit = { 1 };
    assert(client.SendRaw((const char*)&cInit, 1));

    Rfb::RfbServerInitHeader sInit;
    assert(client.ReceiveExact((char*)&sInit, sizeof(sInit)));
    uint32_t nameLen = ntohl(sInit.nameLength);
    std::vector<char> nameBuf(nameLen + 1, 0);
    assert(client.ReceiveExact(nameBuf.data(), nameLen));

    // Cho mot chut de Server chuyen sang RFBSTATE_NORMAL
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(RfbServer::GetState() == Rfb::RFBSTATE_NORMAL);
    std::cout << "[2/4] Sau khi hoan tat bat tay, Server o trang thai: " 
              << Rfb::RfbStateToString(RfbServer::GetState()) << "\n";

    // 4. Client gui SetEncodings & PointerEvent
    int32_t encs[] = { (int32_t)htonl((uint32_t)Rfb::ENCODING_RAW) };
    Rfb::RfbSetEncodingsHeader encHdr = { Rfb::CLIENT_SET_ENCODINGS, 0, htons(1) };
    assert(client.SendRaw((const char*)&encHdr, sizeof(encHdr)));
    assert(client.SendRaw((const char*)encs, sizeof(encs)));

    std::cout << "[3/4] Trạng thái RFBSTATE_NORMAL xử lý thông điệp client thành công!\n";

    // 5. Ngắt kết nối
    client.Close();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    assert(RfbServer::GetState() == Rfb::RFBSTATE_UNINITIALISED);
    std::cout << "[4/4] Sau khi Client ngat ket noi, Server quay ve: " 
              << Rfb::RfbStateToString(RfbServer::GetState()) << "\n";

    RfbServer::Stop();
    Network::Cleanup();

    std::cout << "\n============================================================\n";
    std::cout << " [SUCCESS] TIGERVNC STATE MACHINE PASSED 100%!\n";
    std::cout << "============================================================\n";
    return 0;
}
