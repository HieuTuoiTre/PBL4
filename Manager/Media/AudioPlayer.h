// ============================================================================
// File: AudioPlayer.h
// Mô tả: Khai báo giao diện của module phát âm thanh AudioPlayer trên Manager.
// Chịu trách nhiệm:
//   - Khởi tạo thiết bị phát âm thanh của Windows (sử dụng WinMM WaveOut API).
//   - Tiếp nhận các chunk âm thanh PCM 16-bit 48kHz Stereo gửi từ Client.
//   - Hỗ trợ khuếch đại kỹ thuật số (Pre-amp gain), bộ giới hạn mềm chống vỡ tiếng (Soft-Limiter).
//   - Hỗ trợ bật/tắt tiếng (Mute) và điều chỉnh âm lượng linh hoạt.
// ============================================================================

#pragma once

#ifndef NOMINMAX
#define NOMINMAX // Ngăn chặn Windows.h định nghĩa macro min/max gây xung đột với std::min/std::max
#endif

#include <windows.h> // Thư viện Windows API

namespace AudioPlayer {
    // Khởi tạo hệ thống phát âm thanh waveOut với cấu hình chuẩn PCM 48.000 Hz, 16-bit Stereo
    // Tham số: defaultMuted - true nếu muốn mặc định tắt tiếng khi vừa kết nối (VD: test trên localhost 127.0.0.1)
    void Initialize(bool defaultMuted = false);

    // Đẩy một khối dữ liệu âm thanh nhận từ mạng vào bộ đệm để phát ra loa
    void PlayChunk(const char* rawData, int byteSize);

    // Chuyển đổi qua lại giữa trạng thái bật âm thanh và tắt tiếng (Mute toggle)
    void ToggleMute();

    // Kiểm tra xem hiện tại âm thanh có đang bị tắt tiếng hay không
    bool IsMuted();

    // Thiết lập hệ số âm lượng khuếch đại (từ 0.0f đến 3.0f, mặc định là 1.5f = 150%)
    void SetVolume(float volume);

    // Lấy hệ số âm lượng hiện tại
    float GetVolume();

    // Tăng âm lượng thêm 10% (+0.1f)
    void IncreaseVolume();

    // Giảm âm lượng đi 10% (-0.1f)
    void DecreaseVolume();

    // Dọn dẹp thiết bị WaveOut, giải phóng bộ nhớ đệm và đóng thư viện winmm.dll
    void Cleanup();
}
