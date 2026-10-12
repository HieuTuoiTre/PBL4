#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <functional>

namespace AudioCapture {
    // Truyền vào một hàm callback để xử lý dữ liệu âm thanh (buffer, size) ngay khi thu được
    bool Start(std::function<void(const char*, int)> onAudioData);
    
    // Dừng thu âm và giải phóng bộ nhớ
    void Stop();
}