#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <windows.h>
#include <string>
#include <functional>

struct DiscoveredClient {
    std::string ip;
    std::wstring computerName;
    std::wstring userName;
    bool isBusy;
    DWORD lastSeenTick = 0;
};

namespace DiscoveryScanner {

    using ClientUpdateCallback = std::function<void(const DiscoveredClient& client, bool isOnline)>;

    bool Start(ClientUpdateCallback onUpdate);

    void TriggerScan();

    void Stop();
}
