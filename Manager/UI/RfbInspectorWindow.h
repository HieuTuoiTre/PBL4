#pragma once

#include <windows.h>
#include <string>

namespace RfbInspector {

    /**
     * @brief Displays the RFB Protocol Inspector window.
     * @param hParent Parent window handle (RemoteWindow).
     */
    void Show(HWND hParent);

    /**
     * @brief Toggles the visibility of the RFB Protocol Inspector window.
     */
    void Toggle(HWND hParent);

    /**
     * @brief Adds a new packet record to the inspector list view.
     * @param dir "SEND" or "RECV"
     * @param type RFB PDU Message Type name (e.g. "FramebufferUpdate", "PointerEvent")
     * @param size Size in bytes
     * @param desc Detailed description of packet fields
     */
    void LogPacket(const std::string& dir, const std::string& type, size_t size, const std::string& desc);

    /**
     * @brief Closes and cleans up the inspector window.
     */
    void Close();

    bool IsOpen();

} // namespace RfbInspector
