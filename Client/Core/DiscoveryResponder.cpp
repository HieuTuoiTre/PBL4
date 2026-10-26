#include "DiscoveryResponder.h"
#include "../../Shared/Protocol.h"
#include "../../Shared/Utils.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <thread>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

namespace {
    SOCKET g_udpSock = INVALID_SOCKET;
    std::atomic<bool> g_isRunning(false);
    std::atomic<bool> g_isBusy(false);
    std::thread g_listenThread;

    void FillPacket(Protocol::DiscoveryPacket& pkt, uint8_t type) {
        memset(&pkt, 0, sizeof(pkt));
        memcpy(pkt.magic, "LAND", 4);
        pkt.type = type;
        pkt.isBusy = g_isBusy ? 1 : 0;

        wchar_t compW[64] = {0};
        DWORD len = 64;
        if (GetComputerNameW(compW, &len)) {
            std::string comp = Utils::WideToUtf8(compW);
            strncpy_s(pkt.computerName, comp.c_str(), sizeof(pkt.computerName) - 1);
        }

        wchar_t userW[64] = {0};
        len = 64;
        if (GetUserNameW(userW, &len)) {
            std::string user = Utils::WideToUtf8(userW);
            strncpy_s(pkt.userName, user.c_str(), sizeof(pkt.userName) - 1);
        }
    }

    void SendBroadcast(uint8_t type) {
        if (g_udpSock == INVALID_SOCKET) return;

        Protocol::DiscoveryPacket pkt;
        FillPacket(pkt, type);

        // 1. Gửi Broadcast tới 255.255.255.255 trên cổng PORT_DISCOVERY_MGR (8088)
        sockaddr_in bcastAddr = {};
        bcastAddr.sin_family = AF_INET;
        bcastAddr.sin_port = htons(Protocol::PORT_DISCOVERY_MGR);
        bcastAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        sendto(g_udpSock, (const char*)&pkt, sizeof(pkt), 0, (sockaddr*)&bcastAddr, sizeof(bcastAddr));

        // 2. Gửi vào Subnet Directed Broadcast của TẤT CẢ các card mạng (Wi-Fi, Ethernet, ZeroTier, VPN ảo)
        ULONG size = 0;
        GetIpAddrTable(NULL, &size, FALSE);
        if (size > 0) {
            std::vector<BYTE> buf(size);
            PMIB_IPADDRTABLE pTable = (PMIB_IPADDRTABLE)buf.data();
            if (GetIpAddrTable(pTable, &size, FALSE) == NO_ERROR) {
                for (DWORD i = 0; i < pTable->dwNumEntries; i++) {
                    const MIB_IPADDRROW& row = pTable->table[i];
                    if (row.dwAddr == 0) continue;
                    if ((row.dwAddr & 0xFF) == 127) continue; // Bỏ qua loopback

                    sockaddr_in ifBcast = {};
                    ifBcast.sin_family = AF_INET;
                    ifBcast.sin_port = htons(Protocol::PORT_DISCOVERY_MGR);
                    ifBcast.sin_addr.s_addr = (row.dwAddr & row.dwMask) | (~row.dwMask);

                    sendto(g_udpSock, (const char*)&pkt, sizeof(pkt), 0, (sockaddr*)&ifBcast, sizeof(ifBcast));
                }
            }
        }

        // 3. Gửi thêm vào loopback 127.0.0.1:8088 (để hỗ trợ test Client & Manager trên cùng 1 máy)
        sockaddr_in loopbackAddr = {};
        loopbackAddr.sin_family = AF_INET;
        loopbackAddr.sin_port = htons(Protocol::PORT_DISCOVERY_MGR);
        inet_pton(AF_INET, "127.0.0.1", &loopbackAddr.sin_addr);
        sendto(g_udpSock, (const char*)&pkt, sizeof(pkt), 0, (sockaddr*)&loopbackAddr, sizeof(loopbackAddr));

        // 3. Gửi trực tiếp tới địa chỉ Subnet Broadcast của từng card mạng đang bật (Wi-Fi, Ethernet...)
        INTERFACE_INFO ifList[24] = {};
        DWORD bytesRet = 0;
        if (WSAIoctl(g_udpSock, SIO_GET_INTERFACE_LIST, NULL, 0, &ifList, sizeof(ifList), &bytesRet, NULL, NULL) == 0) {
            int count = bytesRet / sizeof(INTERFACE_INFO);
            for (int i = 0; i < count; i++) {
                u_long flags = ifList[i].iiFlags;
                if ((flags & IFF_UP) && (flags & IFF_BROADCAST) && !(flags & IFF_LOOPBACK)) {
                    sockaddr_in subBcast = ifList[i].iiBroadcastAddress.AddressIn;
                    subBcast.sin_port = htons(Protocol::PORT_DISCOVERY_MGR);
                    sendto(g_udpSock, (const char*)&pkt, sizeof(pkt), 0, (sockaddr*)&subBcast, sizeof(subBcast));
                }
            }
        }
    }

    void ListenLoop() {
        while (g_isRunning) {
            sockaddr_in senderAddr = {};
            int senderLen = sizeof(senderAddr);
            Protocol::DiscoveryPacket recvPkt = {};

            int bytes = recvfrom(g_udpSock, (char*)&recvPkt, sizeof(recvPkt), 0, (sockaddr*)&senderAddr, &senderLen);
            if (bytes <= 0) {
                if (!g_isRunning) break;
                int err = WSAGetLastError();
                // Bỏ qua lỗi WSAECONNRESET (10054) hoặc WSAEINTR để luồng không bị chết
                if (err == WSAECONNRESET || err == WSAEINTR) {
                    continue;
                }
                continue;
            }

            if (bytes == sizeof(Protocol::DiscoveryPacket) && memcmp(recvPkt.magic, "LAND", 4) == 0) {
                if (recvPkt.type == Protocol::DISCOVERY_QUERY) {
                    // Nhận được yêu cầu quét từ Manager -> Phản hồi trực tiếp về IP và Port của Manager
                    Protocol::DiscoveryPacket respPkt;
                    FillPacket(respPkt, Protocol::DISCOVERY_RESPONSE);

                    sockaddr_in replyAddr = senderAddr;
                    replyAddr.sin_port = htons(Protocol::PORT_DISCOVERY_MGR);

                    sendto(g_udpSock, (const char*)&respPkt, sizeof(respPkt), 0, (sockaddr*)&replyAddr, sizeof(replyAddr));
                }
            }
        }
    }
}

namespace DiscoveryResponder {

    bool Start() {
        if (g_isRunning) return true;

        g_udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (g_udpSock == INVALID_SOCKET) {
            return false;
        }

        // Vô hiệu hóa lỗi WSAECONNRESET do gói tin ICMP Port Unreachable trả về
        BOOL bNewBehavior = FALSE;
        DWORD dwBytesReturned = 0;
        WSAIoctl(g_udpSock, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior), NULL, 0, &dwBytesReturned, NULL, NULL);

        BOOL reuse = TRUE;
        setsockopt(g_udpSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

        BOOL bcast = TRUE;
        setsockopt(g_udpSock, SOL_SOCKET, SO_BROADCAST, (const char*)&bcast, sizeof(bcast));

        sockaddr_in bindAddr = {};
        bindAddr.sin_family = AF_INET;
        bindAddr.sin_port = htons(Protocol::PORT_DISCOVERY_CLIENT);
        bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(g_udpSock, (sockaddr*)&bindAddr, sizeof(bindAddr)) == SOCKET_ERROR) {
            closesocket(g_udpSock);
            g_udpSock = INVALID_SOCKET;
            return false;
        }

        g_isRunning = true;
        g_listenThread = std::thread(ListenLoop);

        // Báo danh lần đầu tiên khi vừa khởi động
        SendBroadcast(Protocol::DISCOVERY_HELLO);
        return true;
    }

    void Stop() {
        if (!g_isRunning) return;

        // Báo danh tắt app trước khi hủy socket
        SendBroadcast(Protocol::DISCOVERY_BYE);

        g_isRunning = false;
        if (g_udpSock != INVALID_SOCKET) {
            closesocket(g_udpSock);
            g_udpSock = INVALID_SOCKET;
        }

        if (g_listenThread.joinable()) {
            g_listenThread.join();
        }
    }

    void SetBusy(bool busy) {
        g_isBusy = busy;
        // Gửi thông báo cập nhật trạng thái mới ra toàn mạng
        SendBroadcast(Protocol::DISCOVERY_HELLO);
    }
}
