#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <windows.h>

#include <string>
#include <functional>

namespace ConnectWindow {

    using ConnectCallback = std::function<bool(const std::string& ip, std::string& outError)>;

    bool Show(HINSTANCE hInstance, std::string& outSelectedIP, ConnectCallback onConnect);
}
