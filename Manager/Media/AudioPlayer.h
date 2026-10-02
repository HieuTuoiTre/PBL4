#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#include <vector>
#include <mutex>

class AudioPlayer {
private:
    HWAVEOUT hWaveOut = nullptr;
    WAVEFORMATEX waveFormat{};
    std::mutex audioMutex;
    bool isInitialized = false;

public:
    AudioPlayer();
    ~AudioPlayer();

    bool Initialize(int sampleRate = 44100, int channels = 2, int bitsPerSample = 16);
    bool PlayChunk(const char* data, int length);
    void Stop();
    void Cleanup();
    bool IsInitialized() const { return isInitialized; }
};

