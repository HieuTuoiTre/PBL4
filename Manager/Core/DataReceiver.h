#pragma once
#include "../../Shared/Network.h"

namespace DataReceiver {
    void Start(TcpClient* client);
    void Stop();
}