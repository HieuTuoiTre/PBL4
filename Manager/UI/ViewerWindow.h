#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

class TcpClient;

namespace ViewerWindow {

constexpr wchar_t kWindowClassName[] = L"RemoteViewerWindow";

struct ProcessItem {
    uint32_t pid = 0;
    std::wstring name;
    std::wstring cpu;
    std::wstring memory;
    std::wstring disk;
    int usageLevel = 1;
};

bool Register(HINSTANCE hInstance);
void CreateViewer(HWND owner, const std::wstring& ip, const std::wstring& machineName, TcpClient* client);
void UpdateProcessList(const std::vector<ProcessItem>& processList);

} // namespace ViewerWindow

