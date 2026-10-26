#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <string>
#include <functional>

struct DiscoveredClient {
    std::string ip;
    std::wstring computerName;
    std::wstring userName;
    bool isBusy;
    DWORD lastSeenTick = 0;
};

namespace DiscoveryScanner {
    // Callback thông báo khi có Client phản hồi / thay đổi trạng thái
    // - client: Thông tin máy Client (IP, Tên máy, User, Trạng thái bận)
    // - isOnline: true nếu máy đang hoạt động, false nếu vừa tắt Client (DISCOVERY_BYE)
    using ClientUpdateCallback = std::function<void(const DiscoveredClient& client, bool isOnline)>;

    // Bắt đầu luồng lắng nghe UDP Discovery
    bool Start(ClientUpdateCallback onUpdate);

    // Gửi broadcast yêu cầu tất cả các máy Client trong LAN báo danh ngay lập tức
    void TriggerScan();

    // Dừng luồng lắng nghe và giải phóng tài nguyên
    void Stop();
}
