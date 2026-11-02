#pragma once

#include "../../Shared/Network.h"

namespace DataReceiver {

    void Start(TcpClient* controlClient, TcpClient* audioClient);

    void Stop();
}
