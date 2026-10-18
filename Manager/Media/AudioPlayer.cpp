#include "AudioPlayer.h"
#include <mmsystem.h>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>

#define AUDIO_RING_SIZE 32
#define SAMPLES_PER_CHUNK 1920 // 40ms ở 48.000 Hz, Stereo 16-bit (960 frames * 2 channels = 1920 samples = 3840 bytes)
#define PREBUFFER_CHUNKS 2     // Đệm trước ~80ms để chống jitter mạng

namespace {
    typedef MMRESULT (WINAPI *PFN_waveOutOpen)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
    typedef MMRESULT (WINAPI *PFN_waveOutPrepareHeader)(HWAVEOUT, LPWAVEHDR, UINT);
    typedef MMRESULT (WINAPI *PFN_waveOutWrite)(HWAVEOUT, LPWAVEHDR, UINT);
    typedef MMRESULT (WINAPI *PFN_waveOutUnprepareHeader)(HWAVEOUT, LPWAVEHDR, UINT);
    typedef MMRESULT (WINAPI *PFN_waveOutReset)(HWAVEOUT);
    typedef MMRESULT (WINAPI *PFN_waveOutClose)(HWAVEOUT);

    HMODULE g_hWinMM = NULL;
    PFN_waveOutOpen pWaveOutOpen = nullptr;
    PFN_waveOutPrepareHeader pWaveOutPrepareHeader = nullptr;
    PFN_waveOutWrite pWaveOutWrite = nullptr;
    PFN_waveOutUnprepareHeader pWaveOutUnprepareHeader = nullptr;
    PFN_waveOutReset pWaveOutReset = nullptr;
    PFN_waveOutClose pWaveOutClose = nullptr;

    HWAVEOUT g_hWaveOut = NULL;
    WAVEHDR g_waveHeaders[AUDIO_RING_SIZE] = {};
    std::vector<int16_t> g_audioBuffers[AUDIO_RING_SIZE];
    std::vector<int16_t> g_stagingBuffer;
    int g_audioIdx = 0;
    bool g_isMuted = false;
    bool g_prebuffering = true;

    CRITICAL_SECTION g_audioCs;
    bool g_csInitialized = false;
}

namespace AudioPlayer {
    void Initialize(bool defaultMuted) {
        if (!g_csInitialized) {
            InitializeCriticalSection(&g_audioCs);
            g_csInitialized = true;
        }

        EnterCriticalSection(&g_audioCs);
        g_isMuted = defaultMuted;
        g_prebuffering = true;
        g_audioIdx = 0;
        g_stagingBuffer.clear();
        g_stagingBuffer.reserve(SAMPLES_PER_CHUNK * 4);

        for (int i = 0; i < AUDIO_RING_SIZE; i++) {
            memset(&g_waveHeaders[i], 0, sizeof(WAVEHDR));
            g_audioBuffers[i].resize(SAMPLES_PER_CHUNK, 0);
        }

        g_hWinMM = LoadLibraryA("winmm.dll");
        if (g_hWinMM) {
            pWaveOutOpen = (PFN_waveOutOpen)GetProcAddress(g_hWinMM, "waveOutOpen");
            pWaveOutPrepareHeader = (PFN_waveOutPrepareHeader)GetProcAddress(g_hWinMM, "waveOutPrepareHeader");
            pWaveOutWrite = (PFN_waveOutWrite)GetProcAddress(g_hWinMM, "waveOutWrite");
            pWaveOutUnprepareHeader = (PFN_waveOutUnprepareHeader)GetProcAddress(g_hWinMM, "waveOutUnprepareHeader");
            pWaveOutReset = (PFN_waveOutReset)GetProcAddress(g_hWinMM, "waveOutReset");
            pWaveOutClose = (PFN_waveOutClose)GetProcAddress(g_hWinMM, "waveOutClose");

            if (pWaveOutOpen) {
                WAVEFORMATEX wfx = {};
                wfx.wFormatTag = WAVE_FORMAT_PCM;
                wfx.nChannels = 2;              // Stereo
                wfx.nSamplesPerSec = 48000;      // 48 kHz chuẩn
                wfx.wBitsPerSample = 16;         // 16-bit
                wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8; // 4 bytes/frame
                wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign; // 192.000 B/s

                pWaveOutOpen(&g_hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
            }
        }
        LeaveCriticalSection(&g_audioCs);
    }

    void PlayChunk(const char* rawData, int byteSize) {
        if (!g_csInitialized || byteSize <= 0 || !rawData) return;

        EnterCriticalSection(&g_audioCs);
        if (!g_hWaveOut || g_isMuted) {
            LeaveCriticalSection(&g_audioCs);
            return;
        }

        // Dữ liệu nhận từ Client đã được chuẩn hóa về 16-bit PCM Stereo 48kHz
        const int16_t* pcmSamples = (const int16_t*)rawData;
        int sampleCount = byteSize / (int)sizeof(int16_t);
        g_stagingBuffer.insert(g_stagingBuffer.end(), pcmSamples, pcmSamples + sampleCount);

        // Pre-buffering: gom đủ ít nhất 2 chunks (~80ms) trước khi bắt đầu nạp vào card âm thanh
        if (g_prebuffering) {
            if (g_stagingBuffer.size() < (size_t)(SAMPLES_PER_CHUNK * PREBUFFER_CHUNKS)) {
                LeaveCriticalSection(&g_audioCs);
                return;
            }
            g_prebuffering = false;
        }

        // Đẩy từng chunk chuẩn vào ring buffer của waveOut
        while (g_stagingBuffer.size() >= SAMPLES_PER_CHUNK) {
            // Tìm slot rảnh trong ring buffer (ưu tiên slot tại g_audioIdx hoặc slot đã phát xong WHDR_DONE)
            int targetSlot = -1;
            for (int i = 0; i < AUDIO_RING_SIZE; i++) {
                int idx = (g_audioIdx + i) % AUDIO_RING_SIZE;
                WAVEHDR& h = g_waveHeaders[idx];
                if (!(h.dwFlags & WHDR_PREPARED) || (h.dwFlags & WHDR_DONE)) {
                    targetSlot = idx;
                    break;
                }
            }

            // Nếu toàn bộ 32 slot đều đang phát (đã đệm hơn 1,2 giây phía trước), ghi đè slot cũ nhất
            if (targetSlot == -1) {
                targetSlot = g_audioIdx;
            }

            WAVEHDR& hdr = g_waveHeaders[targetSlot];
            if (hdr.dwFlags & WHDR_PREPARED) {
                pWaveOutUnprepareHeader(g_hWaveOut, &hdr, sizeof(WAVEHDR));
            }

            g_audioBuffers[targetSlot].assign(g_stagingBuffer.begin(), g_stagingBuffer.begin() + SAMPLES_PER_CHUNK);
            g_stagingBuffer.erase(g_stagingBuffer.begin(), g_stagingBuffer.begin() + SAMPLES_PER_CHUNK);

            memset(&hdr, 0, sizeof(WAVEHDR));
            hdr.lpData = (LPSTR)g_audioBuffers[targetSlot].data();
            hdr.dwBufferLength = (DWORD)(SAMPLES_PER_CHUNK * sizeof(int16_t));

            if (pWaveOutPrepareHeader(g_hWaveOut, &hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
                pWaveOutWrite(g_hWaveOut, &hdr, sizeof(WAVEHDR));
            }

            g_audioIdx = (targetSlot + 1) % AUDIO_RING_SIZE;
        }

        LeaveCriticalSection(&g_audioCs);
    }

    void ToggleMute() {
        if (!g_csInitialized) return;
        EnterCriticalSection(&g_audioCs);
        g_isMuted = !g_isMuted;
        if (g_isMuted && g_hWaveOut && pWaveOutReset) {
            pWaveOutReset(g_hWaveOut);
            g_stagingBuffer.clear();
            g_prebuffering = true;
        }
        LeaveCriticalSection(&g_audioCs);
    }

    bool IsMuted() {
        if (!g_csInitialized) return false;
        EnterCriticalSection(&g_audioCs);
        bool muted = g_isMuted;
        LeaveCriticalSection(&g_audioCs);
        return muted;
    }

    void Cleanup() {
        if (!g_csInitialized) return;
        EnterCriticalSection(&g_audioCs);
        if (g_hWaveOut) {
            if (pWaveOutReset) pWaveOutReset(g_hWaveOut);
            for (int i = 0; i < AUDIO_RING_SIZE; i++) {
                if (g_waveHeaders[i].dwFlags & WHDR_PREPARED && pWaveOutUnprepareHeader) {
                    pWaveOutUnprepareHeader(g_hWaveOut, &g_waveHeaders[i], sizeof(WAVEHDR));
                }
            }
            if (pWaveOutClose) pWaveOutClose(g_hWaveOut);
            g_hWaveOut = NULL;
        }
        if (g_hWinMM) {
            FreeLibrary(g_hWinMM);
            g_hWinMM = NULL;
        }
        g_stagingBuffer.clear();
        LeaveCriticalSection(&g_audioCs);
        DeleteCriticalSection(&g_audioCs);
        g_csInitialized = false;
    }
}