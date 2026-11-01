#include "../../Shared/Network.h"
#include "../../Shared/Crypto.h"
#include "../../Shared/ControlEnvelope.h"

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

std::string g_managerSessionToken = Protocol::DEFAULT_AUTH_TOKEN;

void SetManagerSessionToken(const std::string& token) {
    g_managerSessionToken = token;
}

std::string GetManagerSessionToken() {
    return g_managerSessionToken;
}

bool ConnectAll(const std::string& ip, std::string& outError) {

    // 1. Kết nối kênh Điều khiển (Port 8080)
    if (!g_controlClient.Connect(ip, Protocol::PORT_CONTROL)) {
        outError = "Không thể kết nối tới kênh Điều khiển (Port " + std::to_string(Protocol::PORT_CONTROL) + ") tại " + ip + "!\n\n"
                   "Nguyên nhân thường gặp:\n"
                   "1. Ứng dụng Client chưa được khởi chạy trên máy đích (" + ip + ").\n"
                   "2. Windows Firewall trên máy Client đang chặn cổng " + std::to_string(Protocol::PORT_CONTROL) + ".\n"
                   "3. Hai máy tính không cùng một mạng LAN / Wi-Fi hoặc địa chỉ IP chưa đúng.";
        return false;
    }

    // 2. Thực hiện Handshake xác thực phiên ngay trên Port 8080
    Protocol::AuthRequestPayload authReq = {0};
    strncpy_s(authReq.sessionToken, g_managerSessionToken.c_str(), sizeof(authReq.sessionToken) - 1);
    authReq.clientVersion = Protocol::PROTOCOL_VERSION;

    if (!g_controlClient.SendPacket(Protocol::MSG_AUTH_REQUEST, (const char*)&authReq, sizeof(authReq))) {
        g_controlClient.Close();
        outError = "Kết nối bị từ chối: Không thể gửi yêu cầu xác thực phiên (MSG_AUTH_REQUEST) tới Client!";
        return false;
    }

    // Chờ phản hồi MSG_AUTH_RESPONSE với timeout 4s bằng select()
    SOCKET ctrlSock = g_controlClient.GetConnectSocket();
    fd_set readFds;
    FD_ZERO(&readFds);
    FD_SET(ctrlSock, &readFds);
    timeval tv = { 4, 0 }; // 4s timeout

    int selRet = select(0, &readFds, NULL, NULL, &tv);
    if (selRet <= 0) {
        g_controlClient.Close();
        outError = "Kết nối bị từ chối: Quá thời gian chờ phản hồi xác thực phiên từ Client (Timeout 4s)!";
        return false;
    }

    Protocol::PacketHeader respHeader = { (Protocol::MessageType)0, 0 };
    if (!g_controlClient.ReceiveExact((char*)&respHeader, sizeof(respHeader))) {
        g_controlClient.Close();
        outError = "Kết nối bị từ chối: Kênh điều khiển bị đóng trong quá trình xác thực phiên!";
        return false;
    }

    if (respHeader.type != Protocol::MSG_AUTH_RESPONSE || respHeader.size != sizeof(Protocol::AuthResponsePayload)) {
        g_controlClient.Close();
        outError = "Kết nối bị từ chối: Gói tin phản hồi xác thực phiên từ Client không đúng định dạng!";
        return false;
    }

    Protocol::AuthResponsePayload authResp = {0};
    if (!g_controlClient.ReceiveExact((char*)&authResp, sizeof(authResp))) {
        g_controlClient.Close();
        outError = "Kết nối bị từ chối: Lỗi đọc dữ liệu phản hồi xác thực phiên từ Client!";
        return false;
    }

    authResp.message[sizeof(authResp.message) - 1] = '\0';
    if (authResp.status != Protocol::AUTH_SUCCESS) {
        g_controlClient.Close();
        std::string detail = (authResp.message[0] != '\0') ? authResp.message : "Mã Token không hợp lệ";
        outError = "Kết nối bị từ chối: Xác thực phiên thất bại!\n\n"
                   "Chi tiết từ Client: " + detail + "\n\n"
                   "Vui lòng kiểm tra lại mã Token xác thực giữa Manager và Client.";
        return false;
    }

    // 2.5. Sinh khóa đối xứng và kích hoạt mã hóa phiên cho socket điều khiển
    uint8_t sessionKey[Crypto::KEY_SIZE];
    Crypto::DeriveKey(g_managerSessionToken, nullptr, 0, sessionKey);
    g_controlClient.SetSessionKey(sessionKey);

    // 3. Đã xác thực thành công -> Kết nối tiếp kênh Hình ảnh (Port 8081)
    if (!g_videoClient.Connect(ip, Protocol::PORT_VIDEO)) {
        g_controlClient.Close();
        outError = "Đã xác thực thành công nhưng thất bại ở kênh Hình ảnh (Port " + std::to_string(Protocol::PORT_VIDEO) + ")!\n\n"
                   "Vui lòng kiểm tra lại cấu hình tường lửa (Firewall) hoặc khởi động lại Client.";
        return false;
    }

    // 4. Kết nối kênh Âm thanh (Port 8082)
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
