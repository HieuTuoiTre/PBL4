#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <functional>

namespace AudioCapture {
    // Truyền hàm callback nhận dữ liệu âm thanh (buffer, size) ngay khi thu được
    bool Start(std::function<void(const char*, int)> onAudioData); //[cite: 5]
    
    // Dừng thu âm và giải phóng bộ nhớ[cite: 5]
    void Stop(); //[cite: 5]
}