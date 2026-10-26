#include "../../Shared/Network.h"

#include "DataReceiver.h"

#include "../UI/RemoteWindow.h"

#include "../UI/InputHandler.h"

#include "../Media/AudioPlayer.h"

#include "../UI/ConnectWindow.h"

#include <windows.h>
#include <timeapi.h>

#include <gdiplus.h>

#include <cstring>
#include <string>

#pragma comment(lib, "gdiplus.lib")

TcpClient g_controlClient;

TcpClient g_videoClient;

TcpClient g_audioClient;

bool ConnectAll(const std::string& ip, std::string& outError) {

    if (!g_controlClient.Connect(ip, Protocol::PORT_CONTROL)) {
        outError = "Không thể kết nối tới kênh Điều khiển (Port " + std::to_string(Protocol::PORT_CONTROL) + ") tại " + ip + "!\n\n"
                   "Nguyên nhân thường gặp:\n"
                   "1. Ứng dụng Client chưa được khởi chạy trên máy đích (" + ip + ").\n"
                   "2. Windows Firewall trên máy Client đang chặn cổng " + std::to_string(Protocol::PORT_CONTROL) + ".\n"
                   "3. Hai máy tính không cùng một mạng LAN / Wi-Fi hoặc địa chỉ IP chưa đúng.";
        return false;
    }

    if (!g_videoClient.Connect(ip, Protocol::PORT_VIDEO)) {
        g_controlClient.Close();
        outError = "Đã kết nối kênh Điều khiển nhưng thất bại ở kênh Hình ảnh (Port " + std::to_string(Protocol::PORT_VIDEO) + ")!\n\n"
                   "Vui lòng kiểm tra lại cấu hình tường lửa (Firewall) hoặc khởi động lại Client.";
        return false;
    }

    if (!g_audioClient.Connect(ip, Protocol::PORT_AUDIO)) {
        g_controlClient.Close();
        g_videoClient.Close();
        outError = "Đã kết nối kênh Hình ảnh nhưng thất bại ở kênh Âm thanh (Port " + std::to_string(Protocol::PORT_AUDIO) + ")!\n\n"
                   "Vui lòng kiểm tra lại cấu hình tường lửa (Firewall) hoặc khởi động lại Client.";
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

    timeBeginPeriod(1);

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

            return ConnectAll(ip, outError);
        });

        if (!ok || targetIP.empty()) {
            break;
        }

        InputHandler::Initialize(&g_controlClient);

        AudioPlayer::Initialize(targetIP == "127.0.0.1");

        RemoteWindow::Create(hInst, &g_controlClient);

        DataReceiver::Start(&g_controlClient, &g_videoClient, &g_audioClient);

        MSG msg;

        while (GetMessageW(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        CloseAll();

        DataReceiver::Stop();

        AudioPlayer::Cleanup();

        RemoteWindow::Cleanup();

    }

    Gdiplus::GdiplusShutdown(gdiToken);

    Network::Cleanup();

    if (hComCtl) FreeLibrary(hComCtl);

    timeEndPeriod(1);

    return 0;
}
