#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include "../../Shared/Protocol.h"
#include "../../Shared/Network.h"

class InputHandler {
private:
    TcpClient* client = nullptr;
    int remoteWidth = 1920;
    int remoteHeight = 1080;

public:
    InputHandler() = default;

    void SetClient(TcpClient* pClient);
    void SetRemoteResolution(int width, int height);

    void HandleMouseMove(HWND hwndCanvas, int x, int y);
    void HandleMouseButton(HWND hwndCanvas, Protocol::MouseEventType actionType, Protocol::MouseButtonType buttonType, int x, int y);
    void HandleMouseWheel(HWND hwndCanvas, int x, int y, short delta);
    void HandleKeyEvent(UINT vkCode, bool isKeyDown);

private:
    void ConvertToRemoteCoords(HWND hwndCanvas, int localX, int localY, int& outX, int& outY);
};

