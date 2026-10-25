#pragma once

namespace DiscoveryResponder {
    // Khởi động lắng nghe UDP discovery và phát thông báo "Chào sân" (DISCOVERY_HELLO)
    bool Start();

    // Dừng lắng nghe và phát thông báo "Tắt app" (DISCOVERY_BYE)
    void Stop();

    // Cập nhật trạng thái đang bận (đang có Manager kết nối) hay rảnh
    void SetBusy(bool busy);
}

