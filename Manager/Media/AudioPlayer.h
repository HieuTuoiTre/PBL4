#pragma once
#include <windows.h>

namespace AudioPlayer {
    void Initialize(bool defaultMuted = false);
    void PlayChunk(const char* rawData, int byteSize);
    void ToggleMute();
    bool IsMuted();
    void Cleanup();
}