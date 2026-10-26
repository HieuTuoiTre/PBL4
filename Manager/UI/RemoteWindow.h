#pragma once

#include "../../Shared/Network.h"
#include "../../Shared/Protocol.h"

#include <windows.h>

namespace RemoteWindow {

    HWND Create(HINSTANCE hInst, TcpClient* client);

    void UpdateFrame(const Protocol::VideoDeltaHeader& delta, const char* jpegData, int jpegSize);

    void Cleanup();
}
