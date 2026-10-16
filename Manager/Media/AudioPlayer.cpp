#include "AudioPlayer.h"
#include <mmsystem.h>
#include <vector>
#include <cstdint>

#define AUDIO_RING_SIZE 16

namespace {
    typedef MMRESULT (WINAPI *PFN_waveOutOpen)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
    typedef MMRESULT (WINAPI *PFN_waveOutPrepareHeader)(HWAVEOUT, LPWAVEHDR, UINT);
    typedef MMRESULT (WINAPI *PFN_waveOutWrite)(HWAVEOUT, LPWAVEHDR, UINT);
    typedef MMRESULT (WINAPI *PFN_waveOutUnprepareHeader)(HWAVEOUT, LPWAVEHDR, UINT);
    typedef MMRESULT (WINAPI *PFN_waveOutClose)(HWAVEOUT);

    PFN_waveOutOpen pWaveOutOpen = nullptr;
    PFN_waveOutPrepareHeader pWaveOutPrepareHeader = nullptr;
    PFN_waveOutWrite pWaveOutWrite = nullptr;
    PFN_waveOutUnprepareHeader pWaveOutUnprepareHeader = nullptr;
    PFN_waveOutClose pWaveOutClose = nullptr;

    HWAVEOUT g_hWaveOut = NULL;
    WAVEHDR g_waveHeaders[AUDIO_RING_SIZE] = {};
    std::vector<int16_t> g_audioBuffers[AUDIO_RING_SIZE];
    int g_audioIdx = 0;
    bool g_isMuted = false;
}

namespace AudioPlayer {
    void Initialize(bool defaultMuted) {
        g_isMuted = defaultMuted;
        HMODULE hWinMM = LoadLibraryA("winmm.dll");
        if (!hWinMM) return;

        pWaveOutOpen = (PFN_waveOutOpen)GetProcAddress(hWinMM, "waveOutOpen");
        pWaveOutPrepareHeader = (PFN_waveOutPrepareHeader)GetProcAddress(hWinMM, "waveOutPrepareHeader");
        pWaveOutWrite = (PFN_waveOutWrite)GetProcAddress(hWinMM, "waveOutWrite");
        pWaveOutUnprepareHeader = (PFN_waveOutUnprepareHeader)GetProcAddress(hWinMM, "waveOutUnprepareHeader");
        pWaveOutClose = (PFN_waveOutClose)GetProcAddress(hWinMM, "waveOutClose");

        if (pWaveOutOpen) {
            WAVEFORMATEX wfx = {};
            wfx.wFormatTag = WAVE_FORMAT_PCM;
            wfx.nChannels = 2;
            wfx.nSamplesPerSec = 48000;
            wfx.wBitsPerSample = 16;
            wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
            wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

            pWaveOutOpen(&g_hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
        }
    }

    void PlayChunk(const char* rawData, int byteSize) {
        if (!g_hWaveOut || g_isMuted || byteSize <= 0) return;

        WAVEHDR& hdr = g_waveHeaders[g_audioIdx];
        if (hdr.dwFlags & WHDR_PREPARED) {
            if (!(hdr.dwFlags & WHDR_DONE)) return;
            pWaveOutUnprepareHeader(g_hWaveOut, &hdr, sizeof(WAVEHDR));
        }

        int sampleCount = byteSize / sizeof(float);
        const float* floatSamples = (const float*)rawData;
        g_audioBuffers[g_audioIdx].resize(sampleCount);

        for (int i = 0; i < sampleCount; i++) {
            float s = floatSamples[i];
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;
            g_audioBuffers[g_audioIdx][i] = (int16_t)(s * 32767.0f);
        }

        memset(&hdr, 0, sizeof(WAVEHDR));
        hdr.lpData = (LPSTR)g_audioBuffers[g_audioIdx].data();
        hdr.dwBufferLength = sampleCount * sizeof(int16_t);

        if (pWaveOutPrepareHeader(g_hWaveOut, &hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
            pWaveOutWrite(g_hWaveOut, &hdr, sizeof(WAVEHDR));
        }

        g_audioIdx = (g_audioIdx + 1) % AUDIO_RING_SIZE;
    }

    void ToggleMute() {
        g_isMuted = !g_isMuted;
    }

    bool IsMuted() {
        return g_isMuted;
    }

    void Cleanup() {
        if (g_hWaveOut && pWaveOutClose) {
            pWaveOutClose(g_hWaveOut);
            g_hWaveOut = NULL;
        }
    }
}