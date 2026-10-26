#pragma once
#include "../../Shared/Protocol.h"
#include <vector>
#include <cstdint>

namespace SysMonitor {
    // Gọi 1 lần lúc khởi chạy ứng dụng để lấy mốc thời gian CPU ban đầu
    void Initialize();

    // Lấy phần trăm sử dụng hiện tại của CPU, RAM và Ổ đĩa C
    Protocol::SysInfoPayload GetSystemInfo();
    // 2 hàm mới cho Task Manager
    std::vector<Protocol::ProcessInfoPayload> GetProcessList();
    bool KillProcess(uint32_t pid);
}