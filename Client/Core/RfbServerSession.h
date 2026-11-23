#pragma once

#include "../../Shared/RFB/RfbDefines.h"
#include "../../Shared/RFB/RfbMessages.h"
#include "../../Shared/RFB/KeySymMapping.h"
#include "../../Shared/Network.h"

#include <string>
#include <atomic>

namespace RfbServer {

    /**
     * @brief Starts the RFB 3.8 / VNC Server listening on the specified port (default 5900).
     * @param port TCP Port to listen on (default: 5900).
     * @param serverName Desktop name advertised in ServerInit message.
     * @return true if socket bound and listening successfully.
     */
    bool Start(int port = Rfb::PORT_VNC, const std::string& serverName = "PBL4 VNC Server");

    /**
     * @brief Stops the RFB Server, disconnects active clients, and releases resources.
     */
    void Stop();

    /**
     * @brief Checks if the RFB Server is actively listening or handling a session.
     */
    bool IsRunning();

    /**
     * @brief Checks if a VNC Viewer client is currently connected.
     */
    bool IsClientConnected();

    /**
     * @brief Returns the current TigerVNC State Machine state of the server session.
     */
    Rfb::RfbState GetState();

    /**
     * @brief Forces a full screen framebuffer update to be sent to the client on the next cycle.
     */
    void ForceFullUpdate();

} // namespace RfbServer
