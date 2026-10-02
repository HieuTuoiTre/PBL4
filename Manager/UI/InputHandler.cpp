#include "InputHandler.h"
#include <algorithm>

void InputHandler::SetClient(TcpClient* pClient) {
    client = pClient;
}

void InputHandler::SetRemoteResolution(int width, int height) {
    if (width > 0 && height > 0) {
        remoteWidth = width;
        remoteHeight = height;
    }
}

void InputHandler::ConvertToRemoteCoords(HWND hwndCanvas, int localX, int localY, int& outX, int& outY) {
    RECT rect{};
    GetClientRect(hwndCanvas, &rect);
    int canvasWidth = rect.right - rect.left;
    int canvasHeight = rect.bottom - rect.top;

    if (canvasWidth <= 0 || canvasHeight <= 0) {
        outX = localX;
        outY = localY;
        return;
    }

    outX = std::clamp(static_cast<int>((static_cast<long long>(localX) * remoteWidth) / canvasWidth), 0, remoteWidth - 1);
    outY = std::clamp(static_cast<int>((static_cast<long long>(localY) * remoteHeight) / canvasHeight), 0, remoteHeight - 1);
}

void InputHandler::HandleMouseMove(HWND hwndCanvas, int x, int y) {
    if (!client) return;

    int remoteX = 0, remoteY = 0;
    ConvertToRemoteCoords(hwndCanvas, x, y, remoteX, remoteY);

    Protocol::MousePayload payload{};
    payload.actionType = Protocol::MOUSE_ACTION_MOVE;
    payload.buttonType = Protocol::BUTTON_NONE;
    payload.x = static_cast<int16_t>(remoteX);
    payload.y = static_cast<int16_t>(remoteY);
    payload.wheelDelta = 0;

    client->SendPacket(Protocol::MSG_MOUSE_EVENT, reinterpret_cast<const char*>(&payload), sizeof(payload));
}

void InputHandler::HandleMouseButton(HWND hwndCanvas, Protocol::MouseEventType actionType, Protocol::MouseButtonType buttonType, int x, int y) {
    if (!client) return;

    int remoteX = 0, remoteY = 0;
    ConvertToRemoteCoords(hwndCanvas, x, y, remoteX, remoteY);

    Protocol::MousePayload payload{};
    payload.actionType = static_cast<uint8_t>(actionType);
    payload.buttonType = static_cast<uint8_t>(buttonType);
    payload.x = static_cast<int16_t>(remoteX);
    payload.y = static_cast<int16_t>(remoteY);
    payload.wheelDelta = 0;

    client->SendPacket(Protocol::MSG_MOUSE_EVENT, reinterpret_cast<const char*>(&payload), sizeof(payload));
}

void InputHandler::HandleMouseWheel(HWND hwndCanvas, int x, int y, short delta) {
    if (!client) return;

    int remoteX = 0, remoteY = 0;
    ConvertToRemoteCoords(hwndCanvas, x, y, remoteX, remoteY);

    Protocol::MousePayload payload{};
    payload.actionType = Protocol::MOUSE_ACTION_WHEEL;
    payload.buttonType = Protocol::BUTTON_NONE;
    payload.x = static_cast<int16_t>(remoteX);
    payload.y = static_cast<int16_t>(remoteY);
    payload.wheelDelta = delta;

    client->SendPacket(Protocol::MSG_MOUSE_EVENT, reinterpret_cast<const char*>(&payload), sizeof(payload));
}

void InputHandler::HandleKeyEvent(UINT vkCode, bool isKeyDown) {
    if (!client) return;

    Protocol::KeyboardPayload payload{};
    payload.vkCode = static_cast<uint32_t>(vkCode);
    payload.isDown = isKeyDown ? 1 : 0;

    client->SendPacket(Protocol::MSG_KEY_EVENT, reinterpret_cast<const char*>(&payload), sizeof(payload));
}

