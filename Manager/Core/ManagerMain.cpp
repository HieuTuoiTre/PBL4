#include "../../Shared/Network.h"
#include "../../Shared/Crypto.h"
#include "../../Shared/ControlEnvelope.h"
#include "../../Shared/RFB/RfbDefines.h"

#include "DataReceiver.h"
#include "RfbClientSession.h"

#include "../UI/RemoteWindow.h"
#include "../UI/InputHandler.h"
#include "../UI/RfbInspectorWindow.h"
#include "../Media/AudioPlayer.h"
#include "../UI/ConnectWindow.h"

#include <windows.h>
#include <timeapi.h>

#include <gdiplus.h>

#include <cstring>
#include <string>

#pragma comment(lib, "gdiplus.lib")

TcpClient g_controlClient;

TcpClient g_audioClient;

std::string g_managerSessionToken = Protocol::DEFAULT_AUTH_TOKEN;

void SetManagerSessionToken(const std::string& token) {
    g_managerSessionToken = token;
}

std::string GetManagerSessionToken() {
    return g_managerSessionToken;
}

bool ConnectAll(const std::string& ip, std::string& outError) {

    // 1. Thử kết nối giao thức VNC / RFB 3.8 (Port 5900) trước tiên
    std::string rfbError = "";
    bool rfbOk = RfbClient::Connect(ip, Rfb::PORT_VNC, rfbError);

    // 2. Thử kết nối kênh Điều khiển (Port 8080) cho các tính năng quản lý (File, Task Manager, Chat)
    bool ctrlOk = false;
    if (g_controlClient.Connect(ip, Protocol::PORT_CONTROL)) {
        Protocol::AuthRequestPayload authReq = {0};
        strncpy_s(authReq.sessionToken, g_managerSessionToken.c_str(), sizeof(authReq.sessionToken) - 1);
        authReq.clientVersion = Protocol::PROTOCOL_VERSION;

        if (g_controlClient.SendPacket(Protocol::MSG_AUTH_REQUEST, (const char*)&authReq, sizeof(authReq))) {
            SOCKET ctrlSock = g_controlClient.GetConnectSocket();
            fd_set readFds;
            FD_ZERO(&readFds);
            FD_SET(ctrlSock, &readFds);
            timeval tv = { 2, 0 }; // 2s timeout
            int selRet = select(0, &readFds, NULL, NULL, &tv);
            if (selRet > 0) {
                Protocol::PacketHeader respHeader = { (Protocol::MessageType)0, 0 };
                if (g_controlClient.ReceiveExact((char*)&respHeader, sizeof(respHeader)) &&
                    respHeader.type == Protocol::MSG_AUTH_RESPONSE &&
                    respHeader.size == sizeof(Protocol::AuthResponsePayload)) {
                    Protocol::AuthResponsePayload authResp = {0};
                    if (g_controlClient.ReceiveExact((char*)&authResp, sizeof(authResp)) &&
                        authResp.status == Protocol::AUTH_SUCCESS) {
                        uint8_t sessionKey[Crypto::KEY_SIZE];
                        Crypto::DeriveKey(g_managerSessionToken, nullptr, 0, sessionKey);
                        g_controlClient.SetSessionKey(sessionKey);
                        ctrlOk = true;
                    }
                }
            }
        }
        if (!ctrlOk) {
            g_controlClient.Close();
        }
    }

    // 3. Nếu RFB 3.8 (Port 5900) kết nối thành công:
    // Video và Input chạy 100% qua chuẩn RFB 3.8 theo RFC 6143
    if (rfbOk) {
        // Kết nối thêm kênh Audio nếu Client đang chạy
        g_audioClient.Connect(ip, Protocol::PORT_AUDIO);
        return true;
    }

    // 4. Nếu RFB 3.8 không kết nối được:
    outError = "Không thể kết nối tới máy đích " + ip + "!\n\n"
               "1. Giao thức VNC (Port 5900): " + (rfbError.empty() ? "Không phản hồi" : rfbError) + "\n"
               "2. Kênh điều khiển (Port " + std::to_string(Protocol::PORT_CONTROL) + "): " + (ctrlOk ? "Đã sẵn sàng" : "Chưa kết nối") + "\n\n"
               "Nguyên nhân thường gặp:\n"
               "- Ứng dụng Client chưa được khởi chạy trên máy đích (" + ip + ").\n"
               "- Windows Firewall trên máy Client đang chặn cổng 5900 hoặc 8080.\n"
               "- Hai máy tính không cùng một mạng LAN / Wi-Fi hoặc địa chỉ IP chưa đúng.";
    return false;
}

void CloseAll() {
    RfbClient::Disconnect();
    g_controlClient.Close();
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

    RfbClient::SetPacketLogCallback(RfbInspector::LogPacket);

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

        RemoteWindow::Create(hInst, &g_controlClient, RfbClient::GetServerWidth(), RfbClient::GetServerHeight());

        RfbClient::Start();

        DataReceiver::Start(&g_controlClient, &g_audioClient);

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

    RfbInspector::Close();

    Gdiplus::GdiplusShutdown(gdiToken);

    Network::Cleanup();

    if (hComCtl) FreeLibrary(hComCtl);

    timeEndPeriod(1);

    return 0;
}
