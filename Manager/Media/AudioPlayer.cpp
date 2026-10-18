#include "AudioPlayer.h"
#include <mmsystem.h>
#include <vector>
#include <cstdint>
#include <cstring>

#define AUDIO_RING_SIZE 64
#define MIN_BUFFER_SAMPLES 3840

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
        g_stagingBuffer.clear();
        g_stagingBuffer.reserve(MIN_BUFFER_SAMPLES * 2);

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
                wfx.nChannels = 2;
                wfx.nSamplesPerSec = 48000;
                wfx.wBitsPerSample = 16;
                wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
                wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

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

        int sampleCount = byteSize / (int)sizeof(float);
        const float* floatSamples = (const float*)rawData;

        for (int i = 0; i < sampleCount; i++) {
            float s = floatSamples[i];
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;
            g_stagingBuffer.push_back((int16_t)(s * 32767.0f));
        }

        if (g_stagingBuffer.size() < MIN_BUFFER_SAMPLES) {
            LeaveCriticalSection(&g_audioCs);
            return;
        }

        int queuedBuffers = 0;
        for (int i = 0; i < AUDIO_RING_SIZE; i++) {
            if ((g_waveHeaders[i].dwFlags & WHDR_PREPARED) && !(g_waveHeaders[i].dwFlags & WHDR_DONE)) {
                queuedBuffers++;
            }
        }
        if (queuedBuffers > 12 && pWaveOutReset) {
            pWaveOutReset(g_hWaveOut);
        }

        WAVEHDR& hdr = g_waveHeaders[g_audioIdx];
        if (hdr.dwFlags & WHDR_PREPARED) {
            if (!(hdr.dwFlags & WHDR_DONE)) {
                g_stagingBuffer.clear();
                LeaveCriticalSection(&g_audioCs);
                return;
            }
            pWaveOutUnprepareHeader(g_hWaveOut, &hdr, sizeof(WAVEHDR));
        }

        g_audioBuffers[g_audioIdx] = g_stagingBuffer;
        g_stagingBuffer.clear();

        memset(&hdr, 0, sizeof(WAVEHDR));
        hdr.lpData = (LPSTR)g_audioBuffers[g_audioIdx].data();
        hdr.dwBufferLength = (DWORD)(g_audioBuffers[g_audioIdx].size() * sizeof(int16_t));

        if (pWaveOutPrepareHeader(g_hWaveOut, &hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
            pWaveOutWrite(g_hWaveOut, &hdr, sizeof(WAVEHDR));
        }

        g_audioIdx = (g_audioIdx + 1) % AUDIO_RING_SIZE;
        LeaveCriticalSection(&g_audioCs);
    }

    void ToggleMute() {
        if (!g_csInitialized) return;
        EnterCriticalSection(&g_audioCs);
        g_isMuted = !g_isMuted;
        if (g_isMuted && g_hWaveOut && pWaveOutReset) {
            pWaveOutReset(g_hWaveOut);
            g_stagingBuffer.clear();
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
                if (g_waveHeaders[i].dwFlags & WHDR_PREPARED) {
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
        LeaveCriticalSection(&g_audioCs);
        DeleteCriticalSection(&g_audioCs);
        g_csInitialized = false;
    }
}