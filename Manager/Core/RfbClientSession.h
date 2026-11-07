#pragma once

#include "../../Shared/RFB/RfbDefines.h"
#include "../../Shared/RFB/RfbMessages.h"
#include "../../Shared/RFB/KeySymMapping.h"
#include "../../Shared/Network.h"

#include <string>
#include <atomic>
#include <functional>

namespace RfbClient {

    typedef std::function<void(const std::string& dir, const std::string& type, size_t size, const std::string& desc)> PacketLogCallback;

    /**
     * @brief Connects to a VNC / RFB 3.8 Server at the specified IP and port.
     *        Performs the RFB handshake, exchanges security, and sets encodings.
     * @param ip Server IP address.
     * @param port Server VNC port (default: 5900).
     * @param outError Output string containing error description if connection fails.
     * @return true if connected and RFB 3.8 handshake completed successfully.
     */
    bool Connect(const std::string& ip, int port, std::string& outError);

    /**
     * @brief Starts the RFB client worker thread and requests the initial full frame.
     *        Should be called after the display window (RemoteWindow) is created.
     */
    void Start();

    /**
     * @brief Closes the connection to the VNC Server and terminates worker threads.
     */
    void Disconnect();

    /**
     * @brief Checks if connected to the VNC Server.
     */
    bool IsConnected();

    /**
     * @brief Returns the current TigerVNC State Machine state of the client connection.
     */
    Rfb::RfbState GetState();

    /**
     * @brief Sends a standard RFB PointerEvent (mouse action).
     */
    void SendPointer(uint8_t buttonMask, uint16_t x, uint16_t y);

    /**
     * @brief Sends a standard RFB KeyEvent (keyboard action).
     */
    void SendKey(WPARAM vk, bool isDown, bool isShiftDown);

    /**
     * @brief Sends a FramebufferUpdateRequest to ask the server for screen changes.
     * @param incremental 0 for full screen refresh, 1 for dirty/incremental rects.
     */
    void RequestUpdate(bool incremental = true);

    /**
     * @brief Registers a callback for the Protocol Inspector to log RFB packets in real-time.
     */
    void SetPacketLogCallback(PacketLogCallback cb);

    int GetServerWidth();
    int GetServerHeight();
    std::string GetServerDesktopName();

} // namespace RfbClient
