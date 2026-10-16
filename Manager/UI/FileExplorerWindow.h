#pragma once
#include "../../Shared/Network.h"
#include "../../Shared/Protocol.h"
#include <windows.h>
#include <string>

namespace FileExplorerWindow {
    void Show(HWND hParent, TcpClient* client);
    void OnDriveListResponse(const std::string& content);
    void OnDirResponse(const std::string& content);
    void OnFileInfo(const Protocol::FileInfoPayload& info);
    void OnFileChunk(const char* data, int size);
    void OnFileEnd();
}