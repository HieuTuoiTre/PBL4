#pragma once

#include "../../Shared/Network.h"
#include "../../Shared/Protocol.h"

#include <windows.h>

namespace RemoteWindow {

    HWND Create(HINSTANCE hInst, TcpClient* client);

    HWND GetHwnd();

    void UpdateFrame(const Protocol::VideoDeltaHeader& delta, const char* jpegData, int jpegSize);

    bool IsFullscreen();

    void ToggleFullscreen();

    void Cleanup();
}
