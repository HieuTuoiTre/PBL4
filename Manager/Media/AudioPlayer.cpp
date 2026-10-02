#include "AudioPlayer.h"

namespace {

using fnWaveOutOpen = MMRESULT (WINAPI *)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
using fnWaveOutPrepareHeader = MMRESULT (WINAPI *)(HWAVEOUT, LPWAVEHDR, UINT);
using fnWaveOutWrite = MMRESULT (WINAPI *)(HWAVEOUT, LPWAVEHDR, UINT);
using fnWaveOutUnprepareHeader = MMRESULT (WINAPI *)(HWAVEOUT, LPWAVEHDR, UINT);
using fnWaveOutReset = MMRESULT (WINAPI *)(HWAVEOUT);
using fnWaveOutClose = MMRESULT (WINAPI *)(HWAVEOUT);

struct WinmmProcs {
    HMODULE hModule = nullptr;
    fnWaveOutOpen pWaveOutOpen = nullptr;
    fnWaveOutPrepareHeader pWaveOutPrepareHeader = nullptr;
    fnWaveOutWrite pWaveOutWrite = nullptr;
    fnWaveOutUnprepareHeader pWaveOutUnprepareHeader = nullptr;
    fnWaveOutReset pWaveOutReset = nullptr;
    fnWaveOutClose pWaveOutClose = nullptr;

    bool Load() {
        if (hModule) return true;
        hModule = LoadLibraryW(L"winmm.dll");
        if (!hModule) return false;

        pWaveOutOpen = reinterpret_cast<fnWaveOutOpen>(GetProcAddress(hModule, "waveOutOpen"));
        pWaveOutPrepareHeader = reinterpret_cast<fnWaveOutPrepareHeader>(GetProcAddress(hModule, "waveOutPrepareHeader"));
        pWaveOutWrite = reinterpret_cast<fnWaveOutWrite>(GetProcAddress(hModule, "waveOutWrite"));
        pWaveOutUnprepareHeader = reinterpret_cast<fnWaveOutUnprepareHeader>(GetProcAddress(hModule, "waveOutUnprepareHeader"));
        pWaveOutReset = reinterpret_cast<fnWaveOutReset>(GetProcAddress(hModule, "waveOutReset"));
        pWaveOutClose = reinterpret_cast<fnWaveOutClose>(GetProcAddress(hModule, "waveOutClose"));

        return pWaveOutOpen && pWaveOutPrepareHeader && pWaveOutWrite &&
               pWaveOutUnprepareHeader && pWaveOutReset && pWaveOutClose;
    }
};

static WinmmProcs gWinmm;

static void CALLBACK WaveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR /*dwInstance*/, DWORD_PTR dwParam1, DWORD_PTR /*dwParam2*/) {
    if (uMsg == WOM_DONE && gWinmm.pWaveOutUnprepareHeader) {
        WAVEHDR* pHdr = reinterpret_cast<WAVEHDR*>(dwParam1);
        if (pHdr) {
            gWinmm.pWaveOutUnprepareHeader(hwo, pHdr, sizeof(WAVEHDR));
            delete[] reinterpret_cast<char*>(pHdr->lpData);
            delete pHdr;
        }
    }
}

} // namespace

AudioPlayer::AudioPlayer() = default;

AudioPlayer::~AudioPlayer() {
    Cleanup();
}

bool AudioPlayer::Initialize(int sampleRate, int channels, int bitsPerSample) {
    std::lock_guard<std::mutex> lock(audioMutex);
    Cleanup();

    if (!gWinmm.Load()) {
        return false;
    }

    waveFormat.wFormatTag = WAVE_FORMAT_PCM;
    waveFormat.nChannels = static_cast<WORD>(channels);
    waveFormat.nSamplesPerSec = static_cast<DWORD>(sampleRate);
    waveFormat.wBitsPerSample = static_cast<WORD>(bitsPerSample);
    waveFormat.nBlockAlign = (waveFormat.nChannels * waveFormat.wBitsPerSample) / 8;
    waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
    waveFormat.cbSize = 0;

    MMRESULT result = gWinmm.pWaveOutOpen(&hWaveOut, WAVE_MAPPER, &waveFormat,
                                          reinterpret_cast<DWORD_PTR>(WaveOutProc),
                                          0, CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR) {
        hWaveOut = nullptr;
        isInitialized = false;
        return false;
    }

    isInitialized = true;
    return true;
}

bool AudioPlayer::PlayChunk(const char* data, int length) {
    if (!isInitialized || !hWaveOut || !data || length <= 0 || !gWinmm.hModule) {
        return false;
    }

    std::lock_guard<std::mutex> lock(audioMutex);
    char* buffer = new char[length];
    memcpy(buffer, data, length);

    WAVEHDR* pHdr = new WAVEHDR{};
    pHdr->lpData = buffer;
    pHdr->dwBufferLength = length;
    pHdr->dwFlags = 0;

    if (gWinmm.pWaveOutPrepareHeader(hWaveOut, pHdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        delete[] buffer;
        delete pHdr;
        return false;
    }

    if (gWinmm.pWaveOutWrite(hWaveOut, pHdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        gWinmm.pWaveOutUnprepareHeader(hWaveOut, pHdr, sizeof(WAVEHDR));
        delete[] buffer;
        delete pHdr;
        return false;
    }

    return true;
}

void AudioPlayer::Stop() {
    std::lock_guard<std::mutex> lock(audioMutex);
    if (hWaveOut && gWinmm.pWaveOutReset) {
        gWinmm.pWaveOutReset(hWaveOut);
    }
}

void AudioPlayer::Cleanup() {
    if (hWaveOut) {
        if (gWinmm.pWaveOutReset) gWinmm.pWaveOutReset(hWaveOut);
        if (gWinmm.pWaveOutClose) gWinmm.pWaveOutClose(hWaveOut);
        hWaveOut = nullptr;
    }
    isInitialized = false;
}

