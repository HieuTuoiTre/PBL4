#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include "../../Shared/Network.h"
#include "../../Shared/Protocol.h"

// Custom Windows Message constants for UI thread updates
constexpr UINT WM_DATA_FRAME = WM_USER + 101;
constexpr UINT WM_DATA_SYSINFO = WM_USER + 102;
constexpr UINT WM_DATA_DISCONNECT = WM_USER + 103;
constexpr UINT WM_DATA_PROCLIST = WM_USER + 104;

class DataReceiver {
private:
    TcpClient* client = nullptr;
    HWND notifyWindow = nullptr;
    std::thread workerThread;
    std::atomic<bool> isRunning{false};
    std::atomic<bool> hasFramePending{false};

    std::function<void(const std::vector<char>&)> frameCallback;
    std::function<void(const Protocol::SysInfoPayload&)> sysInfoCallback;
    std::function<void(const std::vector<Protocol::ProcessInfoPayload>&)> procListCallback;
    std::function<void(const std::vector<char>&)> audioCallback;
    std::function<void(const std::vector<char>&)> fileCallback;

public:
    DataReceiver() = default;
    ~DataReceiver();

    void SetCallbacks(
        std::function<void(const std::vector<char>&)> onFrame,
        std::function<void(const Protocol::SysInfoPayload&)> onSysInfo = nullptr,
        std::function<void(const std::vector<Protocol::ProcessInfoPayload>&)> onProcList = nullptr,
        std::function<void(const std::vector<char>&)> onAudio = nullptr,
        std::function<void(const std::vector<char>&)> onFile = nullptr
    );

    bool Start(TcpClient* pClient, HWND hwndNotify);
    void Stop();
    bool IsRunning() const { return isRunning.load(); }
    void ResetFramePending() { hasFramePending.store(false); }

private:
    void ReceiveLoop();
};

