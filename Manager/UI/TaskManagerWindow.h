#pragma once

#include "../../Shared/Network.h"
#include "../../Shared/Protocol.h"

#include <windows.h>

namespace TaskManagerWindow {

    void Show(HWND hParent, TcpClient* client);

    void UpdateSysInfo(const Protocol::SysInfoPayload& sys);

    void UpdateProcessList(const Protocol::ProcessInfoPayload* pList, int count);
}
