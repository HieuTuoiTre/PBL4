#include "AudioPlayer.h"

#include <mmsystem.h>

#include <vector>

#include <cstdint>

#include <cstring>

#include <algorithm>

#include <cmath>

#define AUDIO_RING_SIZE 32
#define SAMPLES_PER_CHUNK 960  // 40ms ở 24.000 Hz, Mono 16-bit (960 frames * 1 channel = 960 samples = 1920 bytes)
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

    float g_volume = 1.0f;

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

        g_isMuted = defaultMuted;
        g_prebuffering = true;
        g_audioIdx = 0;
        g_stagingBuffer.clear();
        g_stagingBuffer.reserve(SAMPLES_PER_CHUNK * 8);

        for (int i = 0; i < AUDIO_RING_SIZE; i++) {
            memset(&g_waveHeaders[i], 0, sizeof(WAVEHDR));
            g_audioBuffers[i].assign(SAMPLES_PER_CHUNK, 0);
        }

        if (!g_hWinMM) {
            g_hWinMM = LoadLibraryA("winmm.dll");
        }
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
                wfx.nChannels = 1;              // Mono
                wfx.nSamplesPerSec = 24000;      // 24 kHz Mono
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

        // Dữ liệu nhận từ Client đã được chuẩn hóa về 16-bit PCM Mono 24kHz
        const int16_t* pcmSamples = (const int16_t*)rawData;
        int sampleCount = byteSize / (int)sizeof(int16_t);

        float gain = g_volume;
        if (gain != 1.0f) {
            std::vector<int16_t> amplified(sampleCount);
            const float THRESHOLD = 26000.0f;
            for (int i = 0; i < sampleCount; i++) {
                float x = pcmSamples[i] * gain;

                if (x > THRESHOLD) {
                    float excess = x - THRESHOLD;
                    x = THRESHOLD + (32767.0f - THRESHOLD) * std::tanh(excess / (32767.0f - THRESHOLD));
                } else if (x < -THRESHOLD) {
                    float excess = -x - THRESHOLD;
                    x = -THRESHOLD - (32767.0f - THRESHOLD) * std::tanh(excess / (32767.0f - THRESHOLD));
                }

                amplified[i] = (int16_t)std::clamp(x, -32768.0f, 32767.0f);
            }

            g_stagingBuffer.insert(g_stagingBuffer.end(), amplified.begin(), amplified.end());
        } else {

            g_stagingBuffer.insert(g_stagingBuffer.end(), pcmSamples, pcmSamples + sampleCount);
        }

        const size_t MAX_STAGING_SAMPLES = SAMPLES_PER_CHUNK * 6;
        if (g_stagingBuffer.size() > MAX_STAGING_SAMPLES) {
            size_t excess = g_stagingBuffer.size() - MAX_STAGING_SAMPLES;
            g_stagingBuffer.erase(g_stagingBuffer.begin(), g_stagingBuffer.begin() + excess);
        }

        if (g_prebuffering) {
            if (g_stagingBuffer.size() < (size_t)(SAMPLES_PER_CHUNK * PREBUFFER_CHUNKS)) {
                LeaveCriticalSection(&g_audioCs);
                return;
            }
            g_prebuffering = false;
        }

        while (g_stagingBuffer.size() >= SAMPLES_PER_CHUNK) {

            int targetSlot = -1;
            for (int i = 0; i < AUDIO_RING_SIZE; i++) {
                int idx = (g_audioIdx + i) % AUDIO_RING_SIZE;
                WAVEHDR& h = g_waveHeaders[idx];

                if (!(h.dwFlags & WHDR_PREPARED) || (h.dwFlags & WHDR_DONE)) {
                    targetSlot = idx;
                    break;
                }
            }

            if (targetSlot == -1) {
                break;
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

    void SetVolume(float volume) {
        if (!g_csInitialized) return;
        EnterCriticalSection(&g_audioCs);
        g_volume = std::clamp(volume, 0.0f, 3.0f);
        LeaveCriticalSection(&g_audioCs);
    }

    float GetVolume() {
        if (!g_csInitialized) return 1.0f;
        EnterCriticalSection(&g_audioCs);
        float vol = g_volume;
        LeaveCriticalSection(&g_audioCs);
        return vol;
    }

    void IncreaseVolume() {
        if (!g_csInitialized) return;
        EnterCriticalSection(&g_audioCs);
        g_volume = (std::min)(3.0f, g_volume + 0.1f);
        LeaveCriticalSection(&g_audioCs);
    }

    void DecreaseVolume() {
        if (!g_csInitialized) return;
        EnterCriticalSection(&g_audioCs);
        g_volume = (std::max)(0.0f, g_volume - 0.1f);
        LeaveCriticalSection(&g_audioCs);
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
