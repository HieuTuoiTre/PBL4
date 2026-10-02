#include "DataReceiver.h"

DataReceiver::~DataReceiver() {
    Stop();
}

void DataReceiver::SetCallbacks(
    std::function<void(const std::vector<char>&)> onFrame,
    std::function<void(const Protocol::SysInfoPayload&)> onSysInfo,
    std::function<void(const std::vector<Protocol::ProcessInfoPayload>&)> onProcList,
    std::function<void(const std::vector<char>&)> onAudio,
    std::function<void(const std::vector<char>&)> onFile
) {
    frameCallback = std::move(onFrame);
    sysInfoCallback = std::move(onSysInfo);
    procListCallback = std::move(onProcList);
    audioCallback = std::move(onAudio);
    fileCallback = std::move(onFile);
}

bool DataReceiver::Start(TcpClient* pClient, HWND hwndNotify) {
    if (!pClient) return false;
    Stop();

    client = pClient;
    notifyWindow = hwndNotify;
    isRunning = true;
    hasFramePending = false;

    workerThread = std::thread(&DataReceiver::ReceiveLoop, this);
    return true;
}

void DataReceiver::Stop() {
    isRunning = false;
    if (workerThread.joinable()) {
        workerThread.join();
    }
    client = nullptr;
    notifyWindow = nullptr;
    hasFramePending = false;
}

void DataReceiver::ReceiveLoop() {
    while (isRunning && client) {
        Protocol::PacketHeader header{};
        if (!client->ReceiveExact(reinterpret_cast<char*>(&header), sizeof(header))) {
            break;
        }

        std::vector<char> payload;
        if (header.size > 0) {
            // Giới hạn kích thước gói tối đa 30MB để tránh tràn bộ nhớ
            if (header.size > 30 * 1024 * 1024) {
                break;
            }
            payload.resize(header.size);
            if (!client->ReceiveExact(payload.data(), header.size)) {
                break;
            }
        }

        switch (header.type) {
            case Protocol::MSG_VIDEO_FRAME:
                if (frameCallback) frameCallback(payload);
                if (notifyWindow && IsWindow(notifyWindow)) {
                    if (!hasFramePending.exchange(true)) {
                        PostMessageW(notifyWindow, WM_DATA_FRAME, 0, 0);
                    }
                }
                break;

            case Protocol::MSG_SYS_INFO_RESPONSE:
                if (payload.size() >= sizeof(Protocol::SysInfoPayload)) {
                    Protocol::SysInfoPayload info{};
                    memcpy(&info, payload.data(), sizeof(Protocol::SysInfoPayload));
                    if (sysInfoCallback) sysInfoCallback(info);
                    if (notifyWindow && IsWindow(notifyWindow)) {
                        PostMessageW(notifyWindow, WM_DATA_SYSINFO, 0, 0);
                    }
                }
                break;

            case Protocol::MSG_PROCESS_LIST_RESPONSE: {
                size_t itemCount = payload.size() / sizeof(Protocol::ProcessInfoPayload);
                if (itemCount > 0) {
                    std::vector<Protocol::ProcessInfoPayload> list(itemCount);
                    memcpy(list.data(), payload.data(), itemCount * sizeof(Protocol::ProcessInfoPayload));
                    if (procListCallback) procListCallback(list);
                }
                break;
            }

            case Protocol::MSG_AUDIO_CHUNK:
                if (audioCallback) audioCallback(payload);
                break;

            case Protocol::MSG_FILE_CHUNK:
                if (fileCallback) fileCallback(payload);
                break;

            case Protocol::MSG_DISCONNECT:
                isRunning = false;
                break;

            default:
                break;
        }
    }

    isRunning = false;
    if (notifyWindow && IsWindow(notifyWindow)) {
        PostMessageW(notifyWindow, WM_DATA_DISCONNECT, 0, 0);
    }
}

