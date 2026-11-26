#include "../../Shared/Network.h"
#include "../../Shared/Crypto.h"
#include "../../Shared/ControlEnvelope.h"
#include "../../Shared/RFB/RfbDefines.h"

#include "DataReceiver.h"
#include "RfbClientSession.h"

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

TcpClient g_audioClient;

std::string g_managerSessionToken = "";

void SetManagerSessionToken(const std::string& token) {
    g_managerSessionToken = token;
}

std::string GetManagerSessionToken() {
    return g_managerSessionToken;
}

bool ConnectAll(const std::string& ip, std::string& outError) {

    // 1. Thử kết nối kênh Quản trị (Port 8080) và thực hiện xác thực Challenge-Response HMAC (không truyền PIN bản rõ)
    bool ctrlOk = false;
    std::string authError = "";
    if (g_controlClient.Connect(ip, Protocol::PORT_CONTROL)) {
        SOCKET ctrlSock = g_controlClient.GetConnectSocket();

        auto waitReadable = [](SOCKET s, int sec) -> bool {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(s, &fds);
            timeval tv = { sec, 0 };
            return select(0, &fds, NULL, NULL, &tv) > 0;
        };

        auto mapAuthError = [](const Protocol::AuthResponsePayload& authResp) -> std::string {
            if (authResp.status == Protocol::AUTH_INVALID_TOKEN) {
                return "Mã PIN xác thực không chính xác! Vui lòng kiểm tra mã PIN hiển thị trên máy Client.";
            } else if (authResp.status == Protocol::AUTH_TIMEOUT) {
                return "Xác thực phiên bị quá thời gian chờ (Timeout).";
            } else if (authResp.status == Protocol::AUTH_VERSION_MISMATCH) {
                return "Phiên bản giao thức không tương thích giữa Manager và Client (Version Mismatch). Vui lòng cập nhật phần mềm lên cùng phiên bản.";
            } else if (authResp.status == Protocol::AUTH_REJECTED) {
                if (authResp.message[0] != '\0') {
                    return std::string("Kết nối bị từ chối: ") + authResp.message;
                }
                return "Kết nối bị từ chối do định dạng gói tin hoặc giới hạn thử mã PIN.";
            }
            return std::string("Máy Client từ chối kết nối: ") + authResp.message;
        };

        if (waitReadable(ctrlSock, 3)) {
            Protocol::PacketHeader chalHeader = { (Protocol::MessageType)0, 0 };
            if (g_controlClient.ReceiveExact((char*)&chalHeader, sizeof(chalHeader))) {
                if (chalHeader.type == Protocol::MSG_AUTH_RESPONSE &&
                    chalHeader.size == sizeof(Protocol::AuthResponsePayload)) {
                    Protocol::AuthResponsePayload earlyResp = {0};
                    if (g_controlClient.ReceiveExact((char*)&earlyResp, sizeof(earlyResp))) {
                        earlyResp.message[sizeof(earlyResp.message) - 1] = '\0';
                        authError = mapAuthError(earlyResp);
                    }
                } else if (chalHeader.type == Protocol::MSG_AUTH_CHALLENGE &&
                           chalHeader.size == sizeof(Protocol::AuthChallengePayload)) {
                    Protocol::AuthChallengePayload chal = {0};
                    if (g_controlClient.ReceiveExact((char*)&chal, sizeof(chal))) {
                        Protocol::AuthRequestPayload authReq = {0};
                        authReq.clientVersion = Protocol::PROTOCOL_VERSION;
                        Crypto::GenerateRandomBytes(authReq.clientChallenge, sizeof(authReq.clientChallenge));

                        uint8_t salt[32];
                        std::memcpy(salt, authReq.clientChallenge, 16);
                        std::memcpy(salt + 16, chal.serverChallenge, 16);

                        uint8_t proof[Crypto::SHA256_DIGEST_SIZE];
                        Crypto::HmacSha256(
                            reinterpret_cast<const uint8_t*>(g_managerSessionToken.data()),
                            g_managerSessionToken.size(),
                            salt, sizeof(salt), proof
                        );
                        std::memcpy(authReq.sessionToken, proof, sizeof(proof));

                        if (g_controlClient.SendPacket(Protocol::MSG_AUTH_REQUEST, (const char*)&authReq, sizeof(authReq))) {
                            if (waitReadable(ctrlSock, 3)) {
                                Protocol::PacketHeader respHeader = { (Protocol::MessageType)0, 0 };
                                if (g_controlClient.ReceiveExact((char*)&respHeader, sizeof(respHeader)) &&
                                    respHeader.type == Protocol::MSG_AUTH_RESPONSE &&
                                    respHeader.size == sizeof(Protocol::AuthResponsePayload)) {
                                    Protocol::AuthResponsePayload authResp = {0};
                                    if (g_controlClient.ReceiveExact((char*)&authResp, sizeof(authResp))) {
                                        authResp.message[sizeof(authResp.message) - 1] = '\0';
                                        if (authResp.status == Protocol::AUTH_SUCCESS &&
                                            Crypto::ConstantTimeCompare(authResp.serverChallenge, chal.serverChallenge, 16)) {
                                            uint8_t sessionKey[Crypto::KEY_SIZE];
                                            Crypto::DeriveKey(g_managerSessionToken, salt, sizeof(salt), sessionKey);
                                            g_controlClient.SetSessionKey(sessionKey);
                                            std::memset(sessionKey, 0, sizeof(sessionKey));
                                            ctrlOk = true;
                                        } else {
                                            authError = mapAuthError(authResp);
                                        }
                                    }
                                }
                            } else {
                                authError = "Quá thời gian chờ phản hồi xác thực từ máy Client (Timeout 3s).";
                            }
                        }
                    }
                } else {
                    authError = "Phản hồi bắt tay xác thực từ máy Client không hợp lệ.";
                }
            }
        } else {
            authError = "Quá thời gian chờ thách thức xác thực từ máy Client (Timeout 3s).";
        }
        if (!ctrlOk) {
            g_controlClient.Close();
        }
    } else {
        authError = "Không thể kết nối tới cổng quản trị " + std::to_string(Protocol::PORT_CONTROL) + ".\n"
                    "- Đảm bảo ứng dụng Client đang được bật trên máy đích (" + ip + ").\n"
                    "- Kiểm tra tường lửa Windows Firewall không chặn cổng 8080.";
    }

    if (!ctrlOk) {
        outError = authError.empty() ? "Xác thực phiên trên cổng 8080 thất bại!" : authError;
        return false;
    }

    // 2. Sau khi xác thực cổng 8080 thành công, Client đã cấp quyền cho IP này trên cổng RFB 5900.
    // Tiến hành kết nối chuẩn RFB 3.8 (Port 5900):
    std::string rfbError = "";
    bool rfbOk = RfbClient::Connect(ip, Rfb::PORT_VNC, rfbError);
    if (!rfbOk) {
        g_controlClient.Close();
        outError = "Kết nối màn hình RFB (Port 5900) thất bại: " + (rfbError.empty() ? "Không phản hồi" : rfbError) + "\n"
                   "Vui lòng kiểm tra cổng 5900 không bị tường lửa chặn.";
        return false;
    }

    // 3. Kết nối kênh Audio nếu Client đang chạy
    g_audioClient.Connect(ip, Protocol::PORT_AUDIO);
    return true;
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

    Gdiplus::GdiplusShutdown(gdiToken);

    Network::Cleanup();

    if (hComCtl) FreeLibrary(hComCtl);

    timeEndPeriod(1);

    return 0;
}
