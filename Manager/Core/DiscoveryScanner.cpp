// ============================================================================
// File: DiscoveryScanner.cpp
// Mô tả: Hiện thực module quét mạng LAN bằng giao thức UDP Broadcast.
// Chịu trách nhiệm:
//   1. Mở socket UDP tại cổng 8088 (PORT_DISCOVERY_MGR) để lắng nghe phản hồi.
//   2. Gửi các gói tin UDP Broadcast tới cổng 8089 (PORT_DISCOVERY_CLIENT) để tìm kiếm Client.
//   3. Lọc bỏ các gói tin rác hoặc gói tin do chính mình phát ra.
//   4. Chuyển đổi thông tin Client (IP, Tên máy, User, isBusy) và kích hoạt Callback thông báo.
// ============================================================================

// Nạp file header khai báo giao diện của DiscoveryScanner
#include "DiscoveryScanner.h"

// Nạp các cấu trúc gói tin giao thức mạng (DiscoveryPacket, DiscoveryType, ...)
#include "../../Shared/Protocol.h"

// Nạp các hàm tiện ích chuyển đổi chuỗi UTF-8 sang Unicode UTF-16
#include "../../Shared/Utils.h"

// Nạp thư viện Winsock2 và các hàm xử lý địa chỉ IP (inet_ntop, inet_pton)
#include <winsock2.h>
#include <ws2tcpip.h>

// Nạp thư viện đa luồng chuẩn C++
#include <thread>

// Nạp biến nguyên tử std::atomic
#include <atomic>

// Nạp các hàm sao chép bộ nhớ
#include <cstring>

// Định nghĩa mã điều khiển IOCTL để khắc phục lỗi UDP trên Windows (WSAECONNRESET khi gửi broadcast)
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

namespace {
    // Socket UDP dùng chung cho cả việc gửi broadcast quét mạng và nhận phản hồi
    SOCKET g_udpSock = INVALID_SOCKET;

    // Biến cờ kiểm soát vòng đời luồng lắng nghe ngầm
    std::atomic<bool> g_isRunning(false);

    // Luồng chạy ngầm để tiếp nhận các phản hồi UDP từ Client
    std::thread g_listenThread;

    // Con trỏ hàm callback được truyền vào từ ConnectWindow để cập nhật giao diện
    DiscoveryScanner::ClientUpdateCallback g_callback = nullptr;

    // ========================================================================
    // Hàm: ListenLoop
    // Mô tả: Vòng lặp chạy trên luồng phụ, liên tục đón nhận các gói UDP gửi tới cổng 8088.
    // ========================================================================
    void ListenLoop() {
        while (g_isRunning) {
            // Cấu trúc lưu địa chỉ IP và cổng của phía gửi gói tin đến
            sockaddr_in senderAddr = {};
            int senderLen = sizeof(senderAddr);

            // Bộ đệm lưu nội dung gói tin phản hồi
            Protocol::DiscoveryPacket pkt = {};

            // Đón nhận gói tin UDP từ socket (hàm này chặn luồng cho đến khi có dữ liệu đến hoặc socket đóng)
            int bytes = recvfrom(g_udpSock, (char*)&pkt, sizeof(pkt), 0, (sockaddr*)&senderAddr, &senderLen);

            // Kiểm tra kết quả nhận dữ liệu
            if (bytes <= 0) {
                // Nếu cờ g_isRunning đã tắt thì thoát hẳn vòng lặp
                if (!g_isRunning) break;

                int err = WSAGetLastError();
                // Bỏ qua lỗi WSAECONNRESET (10054 do ICMP Port Unreachable) hoặc WSAEINTR để luồng không bị ngắt ngang
                if (err == WSAECONNRESET || err == WSAEINTR) {
                    continue;
                }
                continue;
            }

            // Kiểm tra tính hợp lệ của gói tin: Đúng kích thước cấu trúc và có Magic Header "LAND"
            if (bytes == sizeof(Protocol::DiscoveryPacket) && memcmp(pkt.magic, "LAND", 4) == 0) {

                // Bỏ qua gói tin Query do chính Manager phát đi để không tự quét thấy chính mình
                if (pkt.type == Protocol::DISCOVERY_QUERY) {
                    continue;
                }

                // Chuyển đổi địa chỉ IP dạng nhị phân trong senderAddr thành chuỗi văn bản (VD: "192.168.1.15")
                char ipStr[INET_ADDRSTRLEN] = {0};
                inet_ntop(AF_INET, &senderAddr.sin_addr, ipStr, sizeof(ipStr));

                // Khởi tạo cấu trúc DiscoveredClient và trích xuất dữ liệu từ gói tin
                DiscoveredClient client;
                client.ip = ipStr;                                     // Lưu địa chỉ IP
                client.computerName = Utils::Utf8ToWide(pkt.computerName); // Đổi tên máy tính sang UTF-16
                client.userName = Utils::Utf8ToWide(pkt.userName);         // Đổi tên user đăng nhập sang UTF-16
                client.isBusy = (pkt.isBusy != 0);                     // 0 = Sẵn sàng, 1 = Đang bận

                // Phân loại xử lý dựa vào loại gói tin
                if (pkt.type == Protocol::DISCOVERY_BYE) {
                    // Client thông báo chuẩn bị tắt app -> Gọi callback với isOnline = false để xoá khỏi bảng
                    if (g_callback) {
                        g_callback(client, false);
                    }
                } else if (pkt.type == Protocol::DISCOVERY_RESPONSE || pkt.type == Protocol::DISCOVERY_HELLO) {
                    // Client vừa phản hồi yêu cầu quét hoặc vừa khởi động -> Gọi callback với isOnline = true
                    if (g_callback) {
                        g_callback(client, true);
                    }
                }
            }
        }
    }
}

namespace DiscoveryScanner {

    // ========================================================================
    // Hàm: Start
    // Mô tả: Khởi tạo socket UDP trên cổng 8088 và bắt đầu luồng lắng nghe.
    // Tham số: onUpdate - Hàm callback được gọi khi có Client phản hồi.
    // Trả về: true nếu mở socket thành công, false nếu thất bại.
    // ========================================================================
    bool Start(ClientUpdateCallback onUpdate) {
        if (g_isRunning) return true; // Nếu đã chạy thì không tạo lại

        g_callback = onUpdate; // Lưu con trỏ hàm callback

        // 1. Tạo socket giao thức UDP (SOCK_DGRAM)
        g_udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (g_udpSock == INVALID_SOCKET) {
            return false;
        }

        // 2. Tắt cơ chế tự ngắt kết nối của Windows khi nhận phản hồi ICMP Port Unreachable (SIO_UDP_CONNRESET)
        BOOL bNewBehavior = FALSE;
        DWORD dwBytesReturned = 0;
        WSAIoctl(g_udpSock, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior), NULL, 0, &dwBytesReturned, NULL, NULL);

        // 3. Cho phép tái sử dụng địa chỉ/cổng (SO_REUSEADDR) để không bị kẹt socket khi mở lại ứng dụng
        BOOL reuse = TRUE;
        setsockopt(g_udpSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

        // 4. Bật quyền cho phép socket gửi các gói tin Broadcast toàn mạng (SO_BROADCAST)
        BOOL bcast = TRUE;
        setsockopt(g_udpSock, SOL_SOCKET, SO_BROADCAST, (const char*)&bcast, sizeof(bcast));

        // 5. Ràng buộc (bind) socket vào cổng PORT_DISCOVERY_MGR (8088) trên mọi card mạng
        sockaddr_in bindAddr = {};
        bindAddr.sin_family = AF_INET;
        bindAddr.sin_port = htons(Protocol::PORT_DISCOVERY_MGR);
        bindAddr.sin_addr.s_addr = htonl(INADDR_ANY); // Lắng nghe trên tất cả card mạng (WiFi, LAN, ...)

        if (bind(g_udpSock, (sockaddr*)&bindAddr, sizeof(bindAddr)) == SOCKET_ERROR) {
            closesocket(g_udpSock);
            g_udpSock = INVALID_SOCKET;
            return false;
        }

        // Đánh dấu cờ hoạt động và khởi chạy luồng lắng nghe phản hồi
        g_isRunning = true;
        g_listenThread = std::thread(ListenLoop);

        // Kích hoạt quét mạng ngay lập tức trong lần chạy đầu tiên
        TriggerScan();
        return true;
    }

    // ========================================================================
    // Hàm: TriggerScan
    // Mô tả: Gửi gói tin broadcast DISCOVERY_QUERY tới toàn mạng LAN để tìm Client.
    // ========================================================================
    void TriggerScan() {
        if (g_udpSock == INVALID_SOCKET) return;

        // Chuẩn bị gói tin truy vấn
        Protocol::DiscoveryPacket queryPkt = {};
        memcpy(queryPkt.magic, "LAND", 4);               // Điền Magic Header
        queryPkt.type = Protocol::DISCOVERY_QUERY;      // Đặt loại gói tin là Query (truy vấn)

        // 1. Gửi gói tin Broadcast tới toàn bộ mạng LAN trên cổng PORT_DISCOVERY_CLIENT (8089)
        sockaddr_in bcastAddr = {};
        bcastAddr.sin_family = AF_INET;
        bcastAddr.sin_port = htons(Protocol::PORT_DISCOVERY_CLIENT);
        bcastAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST); // 255.255.255.255
        sendto(g_udpSock, (const char*)&queryPkt, sizeof(queryPkt), 0, (sockaddr*)&bcastAddr, sizeof(bcastAddr));

        // 2. Gửi thêm vào địa chỉ nội bộ Loopback 127.0.0.1:8089 để hỗ trợ chạy thử cả Client và Manager trên 1 máy
        sockaddr_in loopbackAddr = {};
        loopbackAddr.sin_family = AF_INET;
        loopbackAddr.sin_port = htons(Protocol::PORT_DISCOVERY_CLIENT);
        inet_pton(AF_INET, "127.0.0.1", &loopbackAddr.sin_addr);
        sendto(g_udpSock, (const char*)&queryPkt, sizeof(queryPkt), 0, (sockaddr*)&loopbackAddr, sizeof(loopbackAddr));
    }

    // ========================================================================
    // Hàm: Stop
    // Mô tả: Dừng luồng quét, đóng socket và thu dọn tài nguyên.
    // ========================================================================
    void Stop() {
        if (!g_isRunning) return;

        g_isRunning = false; // Tắt cờ chạy

        // Đóng socket UDP để giải phóng cổng và làm lệnh recvfrom bị ngắt thoát ra
        if (g_udpSock != INVALID_SOCKET) {
            closesocket(g_udpSock);
            g_udpSock = INVALID_SOCKET;
        }

        // Đợi luồng lắng nghe kết thúc an toàn
        if (g_listenThread.joinable()) {
            g_listenThread.join();
        }
    }
}
