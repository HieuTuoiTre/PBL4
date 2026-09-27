#include "SessionManager.h"
#include <chrono>
#include <thread>

namespace SessionManager {
    void StartServer(int port) {
        // Vòng lặp nghỉ giúp ứng dụng chạy ngầm không bị tắt ngay
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    void Stop() {
        // Tạm thời chưa cần làm gì
    }
}