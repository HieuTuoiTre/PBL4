#pragma once

#include "../../Shared/Network.h"
#include "../../Shared/Protocol.h"

#include <windows.h>

namespace RemoteWindow {

    HWND Create(HINSTANCE hInst, TcpClient* client, int initWidth = 0, int initHeight = 0);

    HWND GetHwnd();

    void UpdateFrame(const Protocol::VideoDeltaHeader& delta, const char* jpegData, int jpegSize);

    void UpdateRawFrame(int x, int y, int w, int h, const uint32_t* rawPixels, bool repaint = true);

    void PresentFrame();

    bool IsFullscreen();

    void ToggleFullscreen();

    void Cleanup();
}
