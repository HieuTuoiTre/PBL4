#pragma once

#include "../../Shared/Network.h"

namespace DataReceiver {

    void Start(TcpClient* controlClient, TcpClient* videoClient, TcpClient* audioClient);

    void Stop();
}
