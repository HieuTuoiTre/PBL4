#pragma once

namespace SessionManager {
    // Hàm mở server chờ Manager kết nối tới
    void StartServer(int port);
    
    // Hàm đóng kết nối và dọn dẹp
    void Stop();
}