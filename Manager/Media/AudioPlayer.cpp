// ============================================================================
// File: AudioPlayer.cpp
// Mô tả: Hiện thực module phát âm thanh AudioPlayer trên Manager.
// Chịu trách nhiệm:
//   - Động nạp thư viện winmm.dll (hỗ trợ tương thích ngược tối đa trên mọi phiên bản Windows).
//   - Mở thiết bị âm thanh waveOut với định dạng 48.000 Hz, 16-bit Stereo.
//   - Bộ đệm vòng (Ring Buffer) 32 slots để phát âm thanh mượt mà, không bị giật/khựng.
//   - Kỹ thuật gom đệm trước (Pre-buffering) ~80ms để chống jitter trễ mạng.
//   - Xử lý khuếch đại (Pre-amp Gain) và bộ nén mềm (Soft-Limiter hàm tanh) chống vỡ tiếng.
//   - Đồng bộ hóa đa luồng an toàn bằng Critical Section.
// ============================================================================

// Nạp file header khai báo giao diện AudioPlayer
#include "AudioPlayer.h"

// Nạp thư viện Multimedia API cổ điển của Windows (WaveOut)
#include <mmsystem.h>

// Nạp các cấu trúc dữ liệu mảng động
#include <vector>

// Nạp các kiểu số nguyên có độ dài cố định (int16_t, uint32_t, ...)
#include <cstdint>

// Nạp hàm thao tác bộ nhớ (memset, memcpy)
#include <cstring>

// Nạp các thuật toán chuẩn (std::min, std::max, std::clamp)
#include <algorithm>

// Nạp hàm toán học (std::tanh, std::abs)
#include <cmath>

// Số lượng slot trong bộ đệm vòng (Ring Buffer). 32 slots x 40ms = tổng khả năng đệm tối đa 1,28 giây
#define AUDIO_RING_SIZE 32

// Số lượng mẫu âm thanh (samples) trong một chunk chuẩn 40ms:
// 48.000 frames/giây * 0.040 giây = 960 frames
// 960 frames * 2 kênh (Stereo) = 1920 samples (tương đương 3840 bytes)
#define SAMPLES_PER_CHUNK 1920

// Số lượng chunks cần gom đủ trước khi bắt đầu phát ra loa (~80ms) để chống rung pha (network jitter)
#define PREBUFFER_CHUNKS 2

namespace {
    // Định nghĩa các kiểu con trỏ hàm API của thư viện winmm.dll:
    typedef MMRESULT (WINAPI *PFN_waveOutOpen)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
    typedef MMRESULT (WINAPI *PFN_waveOutPrepareHeader)(HWAVEOUT, LPWAVEHDR, UINT);
    typedef MMRESULT (WINAPI *PFN_waveOutWrite)(HWAVEOUT, LPWAVEHDR, UINT);
    typedef MMRESULT (WINAPI *PFN_waveOutUnprepareHeader)(HWAVEOUT, LPWAVEHDR, UINT);
    typedef MMRESULT (WINAPI *PFN_waveOutReset)(HWAVEOUT);
    typedef MMRESULT (WINAPI *PFN_waveOutClose)(HWAVEOUT);

    // Handle của thư viện winmm.dll nạp vào bộ nhớ bằng LoadLibraryA
    HMODULE g_hWinMM = NULL;

    // Các biến con trỏ hàm trỏ tới các hàm WaveOut tương ứng
    PFN_waveOutOpen pWaveOutOpen = nullptr;
    PFN_waveOutPrepareHeader pWaveOutPrepareHeader = nullptr;
    PFN_waveOutWrite pWaveOutWrite = nullptr;
    PFN_waveOutUnprepareHeader pWaveOutUnprepareHeader = nullptr;
    PFN_waveOutReset pWaveOutReset = nullptr;
    PFN_waveOutClose pWaveOutClose = nullptr;

    // Handle của thiết bị đầu ra âm thanh WaveOut
    HWAVEOUT g_hWaveOut = NULL;

    // Mảng 32 cấu trúc tiêu đề WAVEHDR quản lý từng slot âm thanh gửi cho Windows
    WAVEHDR g_waveHeaders[AUDIO_RING_SIZE] = {};

    // Mảng 32 bộ đệm chứa các mẫu âm thanh PCM 16-bit tương ứng với các slot
    std::vector<int16_t> g_audioBuffers[AUDIO_RING_SIZE];

    // Bộ đệm trung gian (staging buffer) gom góp dữ liệu nhận từ mạng trước khi chia thành các chunk 40ms chuẩn
    std::vector<int16_t> g_stagingBuffer;

    // Chỉ số index của slot tiếp theo sẽ được nạp trong bộ đệm vòng
    int g_audioIdx = 0;

    // Trạng thái tắt tiếng (Mute)
    bool g_isMuted = false;

    // Cờ đánh dấu đang trong giai đoạn gom đệm trước (pre-buffering)
    bool g_prebuffering = true;

    // Hệ số âm lượng (1.5f = 150%, cho âm thanh to rõ ràng hơn)
    float g_volume = 1.5f;

    // Đối tượng Critical Section đồng bộ hóa đa luồng giữa luồng nhận mạng và luồng UI
    CRITICAL_SECTION g_audioCs;

    // Cờ đánh dấu Critical Section đã được khởi tạo hay chưa
    bool g_csInitialized = false;
}

namespace AudioPlayer {
    // ========================================================================
    // Hàm: Initialize
    // Mô tả: Khởi tạo hệ thống phát âm thanh WaveOut và các cấu trúc bộ đệm.
    // ========================================================================
    void Initialize(bool defaultMuted) {
        // Khởi tạo Critical Section một lần duy nhất
        if (!g_csInitialized) {
            InitializeCriticalSection(&g_audioCs);
            g_csInitialized = true;
        }

        // Khóa đồng bộ trước khi cấu hình các biến toàn cục
        EnterCriticalSection(&g_audioCs);
        g_isMuted = defaultMuted;  // Thiết lập trạng thái tắt tiếng mặc định
        g_prebuffering = true;    // Kích hoạt gom đệm trước
        g_audioIdx = 0;           // Đặt lại chỉ số slot về 0
        g_stagingBuffer.clear();  // Xóa sạch bộ đệm trung gian cũ
        g_stagingBuffer.reserve(SAMPLES_PER_CHUNK * 4); // Cấp phát sẵn bộ nhớ tránh cấp phát lại

        // Khởi tạo kích thước cho toàn bộ 32 slot trong bộ đệm vòng
        for (int i = 0; i < AUDIO_RING_SIZE; i++) {
            memset(&g_waveHeaders[i], 0, sizeof(WAVEHDR));
            g_audioBuffers[i].resize(SAMPLES_PER_CHUNK, 0);
        }

        // Nạp động thư viện winmm.dll
        g_hWinMM = LoadLibraryA("winmm.dll");
        if (g_hWinMM) {
            // Lấy địa chỉ các hàm API cần thiết
            pWaveOutOpen = (PFN_waveOutOpen)GetProcAddress(g_hWinMM, "waveOutOpen");
            pWaveOutPrepareHeader = (PFN_waveOutPrepareHeader)GetProcAddress(g_hWinMM, "waveOutPrepareHeader");
            pWaveOutWrite = (PFN_waveOutWrite)GetProcAddress(g_hWinMM, "waveOutWrite");
            pWaveOutUnprepareHeader = (PFN_waveOutUnprepareHeader)GetProcAddress(g_hWinMM, "waveOutUnprepareHeader");
            pWaveOutReset = (PFN_waveOutReset)GetProcAddress(g_hWinMM, "waveOutReset");
            pWaveOutClose = (PFN_waveOutClose)GetProcAddress(g_hWinMM, "waveOutClose");

            if (pWaveOutOpen) {
                // Thiết lập cấu hình định dạng âm thanh chuẩn PCM
                WAVEFORMATEX wfx = {};
                wfx.wFormatTag = WAVE_FORMAT_PCM;        // Định dạng PCM chuẩn không nén
                wfx.nChannels = 2;                        // 2 kênh (Stereo: Trái + Phải)
                wfx.nSamplesPerSec = 48000;                // Tần số lấy mẫu 48.000 Hz
                wfx.wBitsPerSample = 16;                   // 16-bit trên mỗi mẫu âm thanh
                wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8; // 4 bytes cho mỗi frame (2 kênh x 2 bytes)
                wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign; // 192.000 bytes/giây

                // Mở thiết bị phát âm thanh mặc định của Windows (WAVE_MAPPER)
                pWaveOutOpen(&g_hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
            }
        }
        LeaveCriticalSection(&g_audioCs);
    }

    // ========================================================================
    // Hàm: PlayChunk
    // Mô tả: Đón nhận dữ liệu âm thanh nhận từ mạng, áp dụng âm lượng và nạp vào card âm thanh.
    // ========================================================================
    void PlayChunk(const char* rawData, int byteSize) {
        if (!g_csInitialized || byteSize <= 0 || !rawData) return;

        EnterCriticalSection(&g_audioCs);
        // Nếu thiết bị chưa sẵn sàng hoặc đang bị tắt tiếng thì bỏ qua không phát
        if (!g_hWaveOut || g_isMuted) {
            LeaveCriticalSection(&g_audioCs);
            return;
        }

        // Ép kiểu mảng byte nhận được sang con trỏ các mẫu âm thanh 16-bit (int16_t)
        const int16_t* pcmSamples = (const int16_t*)rawData;
        int sampleCount = byteSize / (int)sizeof(int16_t);

        // Áp dụng hệ số âm lượng và thuật toán giới hạn mềm (Soft-Limiter)
        float gain = g_volume;
        if (gain != 1.0f) {
            std::vector<int16_t> amplified(sampleCount);
            const float THRESHOLD = 26000.0f; // Ngưỡng bắt đầu áp dụng nén mềm
            for (int i = 0; i < sampleCount; i++) {
                float x = pcmSamples[i] * gain; // Nhân hệ số khuếch đại
                // Nếu vượt ngưỡng dương -> nén mềm bằng hàm tang hypebol (tanh) để âm thanh mượt, không rè vỡ
                if (x > THRESHOLD) {
                    float excess = x - THRESHOLD;
                    x = THRESHOLD + (32767.0f - THRESHOLD) * std::tanh(excess / (32767.0f - THRESHOLD));
                } else if (x < -THRESHOLD) { // Nếu vượt ngưỡng âm
                    float excess = -x - THRESHOLD;
                    x = -THRESHOLD - (32767.0f - THRESHOLD) * std::tanh(excess / (32767.0f - THRESHOLD));
                }
                // Giới hạn trong khoảng chuẩn của số nguyên 16-bit có dấu [-32768, 32767]
                amplified[i] = (int16_t)std::clamp(x, -32768.0f, 32767.0f);
            }
            // Đẩy dữ liệu đã khuếch đại vào bộ đệm gom trung gian
            g_stagingBuffer.insert(g_stagingBuffer.end(), amplified.begin(), amplified.end());
        } else {
            // Nếu âm lượng = 1.0f thì đưa thẳng vào bộ đệm không cần tính toán
            g_stagingBuffer.insert(g_stagingBuffer.end(), pcmSamples, pcmSamples + sampleCount);
        }

        // Giai đoạn gom đệm trước (Pre-buffering): Đợi đủ ít nhất 2 chunks (~80ms) mới kích hoạt phát
        if (g_prebuffering) {
            if (g_stagingBuffer.size() < (size_t)(SAMPLES_PER_CHUNK * PREBUFFER_CHUNKS)) {
                LeaveCriticalSection(&g_audioCs);
                return;
            }
            g_prebuffering = false; // Đã gom đủ đệm, bắt đầu phát
        }

        // Vòng lặp chia nhỏ dữ liệu trong stagingBuffer thành các chunk chuẩn 1920 samples (40ms)
        while (g_stagingBuffer.size() >= SAMPLES_PER_CHUNK) {
            // Tìm slot đang rảnh trong bộ đệm vòng (ưu tiên slot tại g_audioIdx hoặc slot đã phát xong)
            int targetSlot = -1;
            for (int i = 0; i < AUDIO_RING_SIZE; i++) {
                int idx = (g_audioIdx + i) % AUDIO_RING_SIZE;
                WAVEHDR& h = g_waveHeaders[idx];
                // Slot rảnh là slot chưa chuẩn bị hoặc cờ WHDR_DONE đã được Windows đánh dấu phát xong
                if (!(h.dwFlags & WHDR_PREPARED) || (h.dwFlags & WHDR_DONE)) {
                    targetSlot = idx;
                    break;
                }
            }

            // Nếu toàn bộ 32 slot đều đang bận phát (đã đệm hơn 1,2 giây), ghi đè slot cũ nhất
            if (targetSlot == -1) {
                targetSlot = g_audioIdx;
            }

            WAVEHDR& hdr = g_waveHeaders[targetSlot];
            // Hủy đăng ký header cũ với hệ điều hành nếu đã chuẩn bị trước đó
            if (hdr.dwFlags & WHDR_PREPARED) {
                pWaveOutUnprepareHeader(g_hWaveOut, &hdr, sizeof(WAVEHDR));
            }

            // Sao chép đúng 1920 samples vào slot mục tiêu
            g_audioBuffers[targetSlot].assign(g_stagingBuffer.begin(), g_stagingBuffer.begin() + SAMPLES_PER_CHUNK);
            // Xóa bỏ phần dữ liệu đã nạp khỏi bộ đệm trung gian
            g_stagingBuffer.erase(g_stagingBuffer.begin(), g_stagingBuffer.begin() + SAMPLES_PER_CHUNK);

            // Thiết lập thông số cho cấu trúc WAVEHDR
            memset(&hdr, 0, sizeof(WAVEHDR));
            hdr.lpData = (LPSTR)g_audioBuffers[targetSlot].data(); // Con trỏ tới vùng nhớ âm thanh
            hdr.dwBufferLength = (DWORD)(SAMPLES_PER_CHUNK * sizeof(int16_t)); // Độ dài tính bằng bytes (3840 bytes)

            // Đăng ký header với driver âm thanh của Windows
            if (pWaveOutPrepareHeader(g_hWaveOut, &hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
                // Đẩy khối âm thanh vào hàng đợi của phần cứng để phát ra loa
                pWaveOutWrite(g_hWaveOut, &hdr, sizeof(WAVEHDR));
            }

            // Nhảy chỉ số slot tiếp theo trong vòng tròn
            g_audioIdx = (targetSlot + 1) % AUDIO_RING_SIZE;
        }

        LeaveCriticalSection(&g_audioCs);
    }

    // ========================================================================
    // Hàm: ToggleMute
    // Mô tả: Đảo trạng thái tắt tiếng.
    // ========================================================================
    void ToggleMute() {
        if (!g_csInitialized) return;
        EnterCriticalSection(&g_audioCs);
        g_isMuted = !g_isMuted;
        // Nếu chuyển sang tắt tiếng -> Dừng phát ngay lập tức và xóa sạch bộ đệm âm thanh còn thừa
        if (g_isMuted && g_hWaveOut && pWaveOutReset) {
            pWaveOutReset(g_hWaveOut);
            g_stagingBuffer.clear();
            g_prebuffering = true;
        }
        LeaveCriticalSection(&g_audioCs);
    }

    // ========================================================================
    // Hàm: IsMuted
    // Mô tả: Kiểm tra trạng thái tắt tiếng.
    // ========================================================================
    bool IsMuted() {
        if (!g_csInitialized) return false;
        EnterCriticalSection(&g_audioCs);
        bool muted = g_isMuted;
        LeaveCriticalSection(&g_audioCs);
        return muted;
    }

    // ========================================================================
    // Hàm: SetVolume
    // Mô tả: Cài đặt hệ số âm lượng (kẹp trong khoảng 0.0f đến 3.0f).
    // ========================================================================
    void SetVolume(float volume) {
        if (!g_csInitialized) return;
        EnterCriticalSection(&g_audioCs);
        g_volume = std::clamp(volume, 0.0f, 3.0f);
        LeaveCriticalSection(&g_audioCs);
    }

    // ========================================================================
    // Hàm: GetVolume
    // Mô tả: Lấy hệ số âm lượng hiện tại.
    // ========================================================================
    float GetVolume() {
        if (!g_csInitialized) return 1.5f;
        EnterCriticalSection(&g_audioCs);
        float vol = g_volume;
        LeaveCriticalSection(&g_audioCs);
        return vol;
    }

    // ========================================================================
    // Hàm: IncreaseVolume
    // Mô tả: Tăng âm lượng thêm 10%.
    // ========================================================================
    void IncreaseVolume() {
        if (!g_csInitialized) return;
        EnterCriticalSection(&g_audioCs);
        g_volume = (std::min)(3.0f, g_volume + 0.1f);
        LeaveCriticalSection(&g_audioCs);
    }

    // ========================================================================
    // Hàm: DecreaseVolume
    // Mô tả: Giảm âm lượng đi 10%.
    // ========================================================================
    void DecreaseVolume() {
        if (!g_csInitialized) return;
        EnterCriticalSection(&g_audioCs);
        g_volume = (std::max)(0.0f, g_volume - 0.1f);
        LeaveCriticalSection(&g_audioCs);
    }

    // ========================================================================
    // Hàm: Cleanup
    // Mô tả: Dọn dẹp thiết bị âm thanh, giải phóng bộ nhớ và khóa đồng bộ.
    // ========================================================================
    void Cleanup() {
        if (!g_csInitialized) return;
        EnterCriticalSection(&g_audioCs);
        if (g_hWaveOut) {
            // Dừng ngay lập tức các buffer đang phát
            if (pWaveOutReset) pWaveOutReset(g_hWaveOut);
            // Hủy đăng ký toàn bộ các header âm thanh trong bộ đệm vòng
            for (int i = 0; i < AUDIO_RING_SIZE; i++) {
                if (g_waveHeaders[i].dwFlags & WHDR_PREPARED && pWaveOutUnprepareHeader) {
                    pWaveOutUnprepareHeader(g_hWaveOut, &g_waveHeaders[i], sizeof(WAVEHDR));
                }
            }
            // Đóng thiết bị WaveOut
            if (pWaveOutClose) pWaveOutClose(g_hWaveOut);
            g_hWaveOut = NULL;
        }

        // Giải phóng thư viện winmm.dll
        if (g_hWinMM) {
            FreeLibrary(g_hWinMM);
            g_hWinMM = NULL;
        }

        g_stagingBuffer.clear();
        LeaveCriticalSection(&g_audioCs);
        // Hủy bỏ Critical Section
        DeleteCriticalSection(&g_audioCs);
        g_csInitialized = false;
    }
}