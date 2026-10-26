#include "DiscoveryScanner.h"

#include "../../Shared/Protocol.h"

#include "../../Shared/Utils.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <thread>

#include <atomic>

#include <cstring>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

namespace {

    SOCKET g_udpSock = INVALID_SOCKET;

    std::atomic<bool> g_isRunning(false);

    std::thread g_listenThread;

    DiscoveryScanner::ClientUpdateCallback g_callback = nullptr;

    void ListenLoop() {
        while (g_isRunning) {

            sockaddr_in senderAddr = {};
            int senderLen = sizeof(senderAddr);

            Protocol::DiscoveryPacket pkt = {};

            int bytes = recvfrom(g_udpSock, (char*)&pkt, sizeof(pkt), 0, (sockaddr*)&senderAddr, &senderLen);

            if (bytes <= 0) {

                if (!g_isRunning) break;

                int err = WSAGetLastError();

                if (err == WSAECONNRESET || err == WSAEINTR) {
                    continue;
                }
                continue;
            }

            if (bytes == sizeof(Protocol::DiscoveryPacket) && memcmp(pkt.magic, "LAND", 4) == 0) {

                if (pkt.type == Protocol::DISCOVERY_QUERY) {
                    continue;
                }

                char ipStr[INET_ADDRSTRLEN] = {0};
                inet_ntop(AF_INET, &senderAddr.sin_addr, ipStr, sizeof(ipStr));

                DiscoveredClient client;
                client.ip = ipStr;
                client.computerName = Utils::Utf8ToWide(pkt.computerName);
                client.userName = Utils::Utf8ToWide(pkt.userName);
                client.isBusy = (pkt.isBusy != 0);

                if (pkt.type == Protocol::DISCOVERY_BYE) {

                    if (g_callback) {
                        g_callback(client, false);
                    }
                } else if (pkt.type == Protocol::DISCOVERY_RESPONSE || pkt.type == Protocol::DISCOVERY_HELLO) {

                    if (g_callback) {
                        g_callback(client, true);
                    }
                }
            }
        }
    }
}

namespace DiscoveryScanner {

    bool Start(ClientUpdateCallback onUpdate) {
        if (g_isRunning) return true;

        g_callback = onUpdate;

        g_udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (g_udpSock == INVALID_SOCKET) {
            return false;
        }

        BOOL bNewBehavior = FALSE;
        DWORD dwBytesReturned = 0;
        WSAIoctl(g_udpSock, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior), NULL, 0, &dwBytesReturned, NULL, NULL);

        BOOL reuse = TRUE;
        setsockopt(g_udpSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

        BOOL bcast = TRUE;
        setsockopt(g_udpSock, SOL_SOCKET, SO_BROADCAST, (const char*)&bcast, sizeof(bcast));

        sockaddr_in bindAddr = {};
        bindAddr.sin_family = AF_INET;
        bindAddr.sin_port = htons(Protocol::PORT_DISCOVERY_MGR);
        bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(g_udpSock, (sockaddr*)&bindAddr, sizeof(bindAddr)) == SOCKET_ERROR) {
            closesocket(g_udpSock);
            g_udpSock = INVALID_SOCKET;
            return false;
        }

        g_isRunning = true;
        g_listenThread = std::thread(ListenLoop);

        TriggerScan();
        return true;
    }

    void TriggerScan() {
        if (g_udpSock == INVALID_SOCKET) return;

        Protocol::DiscoveryPacket queryPkt = {};
        memcpy(queryPkt.magic, "LAND", 4);
        queryPkt.type = Protocol::DISCOVERY_QUERY;

        sockaddr_in bcastAddr = {};
        bcastAddr.sin_family = AF_INET;
        bcastAddr.sin_port = htons(Protocol::PORT_DISCOVERY_CLIENT);
        bcastAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        sendto(g_udpSock, (const char*)&queryPkt, sizeof(queryPkt), 0, (sockaddr*)&bcastAddr, sizeof(bcastAddr));

        sockaddr_in loopbackAddr = {};
        loopbackAddr.sin_family = AF_INET;
        loopbackAddr.sin_port = htons(Protocol::PORT_DISCOVERY_CLIENT);
        inet_pton(AF_INET, "127.0.0.1", &loopbackAddr.sin_addr);
        sendto(g_udpSock, (const char*)&queryPkt, sizeof(queryPkt), 0, (sockaddr*)&loopbackAddr, sizeof(loopbackAddr));

        INTERFACE_INFO ifList[24] = {};
        DWORD bytesRet = 0;
        if (WSAIoctl(g_udpSock, SIO_GET_INTERFACE_LIST, NULL, 0, &ifList, sizeof(ifList), &bytesRet, NULL, NULL) == 0) {
            int count = bytesRet / sizeof(INTERFACE_INFO);
            for (int i = 0; i < count; i++) {
                u_long flags = ifList[i].iiFlags;
                if ((flags & IFF_UP) && (flags & IFF_BROADCAST) && !(flags & IFF_LOOPBACK)) {
                    sockaddr_in subBcast = ifList[i].iiBroadcastAddress.AddressIn;
                    subBcast.sin_port = htons(Protocol::PORT_DISCOVERY_CLIENT);
                    sendto(g_udpSock, (const char*)&queryPkt, sizeof(queryPkt), 0, (sockaddr*)&subBcast, sizeof(subBcast));
                }
            }
        }
    }

    void Stop() {
        if (!g_isRunning) return;

        g_isRunning = false;

        if (g_udpSock != INVALID_SOCKET) {
            closesocket(g_udpSock);
            g_udpSock = INVALID_SOCKET;
        }

        if (g_listenThread.joinable()) {
            g_listenThread.join();
        }
    }
}
