#pragma once

#include "../../Shared/Network.h"
#include "../../Shared/Protocol.h"

#include <windows.h>
#include <string>

namespace FileExplorerWindow {

    void Show(HWND hParent, TcpClient* client);

    void OnDriveListResponse(const std::string& content);

    void OnDirResponse(const std::string& content);

    // Legacy file info response
    void OnFileInfo(const Protocol::FileInfoPayload& info);

    // Advanced file & folder batch info
    void OnFileHeader(const Protocol::FileHeaderPayload& header);

    void OnFileBatchStart(const Protocol::FileBatchStartPayload& batch);

    void OnFileBatchEnd();

    void OnFileChunk(const char* data, int size);

    void OnFileEnd();

    void OnFileEndPayload(const Protocol::FileEndPayload& endP);

    void OnFileCancel();

    void OnFileError(const std::string& errMsg);

    bool IsTransferActive();

    void CancelCurrentTransfer();
}
