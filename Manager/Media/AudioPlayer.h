#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace AudioPlayer {
    void Initialize(bool defaultMuted = false);
    void PlayChunk(const char* rawData, int byteSize);
    void ToggleMute();
    bool IsMuted();
    void SetVolume(float volume);
    float GetVolume();
    void IncreaseVolume();
    void DecreaseVolume();
    void Cleanup();
}