#include "../../Shared/Network.h"
#include "DataReceiver.h"
#include "../UI/RemoteWindow.h"
#include "../UI/InputHandler.h"
#include "../Media/AudioPlayer.h"
#include "../UI/ConnectWindow.h"
#include <windows.h>
#include <gdiplus.h>
#include <cstring>
#include <string>

#pragma comment(lib, "gdiplus.lib")

TcpClient g_controlClient;
TcpClient g_videoClient;
TcpClient g_audioClient;

bool ConnectAll(const std::string& ip) {
    if (!g_controlClient.Connect(ip, Protocol::PORT_CONTROL)) {
        return false;
    }
    if (!g_videoClient.Connect(ip, Protocol::PORT_VIDEO)) {
        g_controlClient.Close();
        return false;
    }
    if (!g_audioClient.Connect(ip, Protocol::PORT_AUDIO)) {
        g_controlClient.Close();
        g_videoClient.Close();
        return false;
    }
    return true;
}

void CloseAll() {
    g_controlClient.Close();
    g_videoClient.Close();
    g_audioClient.Close();
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    SetProcessDPIAware();

    HMODULE hComCtl = LoadLibraryA("comctl32.dll");
    if (hComCtl) {
        typedef void (WINAPI *INITPROC)(void);
        INITPROC pInit = (INITPROC)GetProcAddress(hComCtl, "InitCommonControls");
        if (pInit) pInit();
    }

    Network::Initialize();
    ULONG_PTR gdiToken;
    Gdiplus::GdiplusStartupInput gdiInput;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, NULL);

    while (true) {
        std::string targetIP = "";
        bool ok = ConnectWindow::Show(hInst, targetIP, [](const std::string& ip, std::string& outError) {
            if (ConnectAll(ip)) {
                return true;
            } else {
                outError = "Không thể kết nối tới " + ip + "!\nHãy kiểm tra lại địa chỉ IP và đảm bảo Client đang chạy.";
                return false;
            }
        });

        // Người dùng đóng cửa sổ kết nối hoặc bấm "Thoát"
        if (!ok || targetIP.empty()) {
            break;
        }

        // Bắt đầu phiên điều khiển từ xa
        InputHandler::Initialize(&g_controlClient);
        AudioPlayer::Initialize(targetIP == "127.0.0.1");
        RemoteWindow::Create(hInst, &g_controlClient);
        DataReceiver::Start(&g_controlClient, &g_videoClient, &g_audioClient);

        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Dọn dẹp phiên kết nối hiện tại
        CloseAll();
        DataReceiver::Stop();
        AudioPlayer::Cleanup();
        RemoteWindow::Cleanup();
    }

    Gdiplus::GdiplusShutdown(gdiToken);
    Network::Cleanup();
    if (hComCtl) FreeLibrary(hComCtl);
    return 0;
}