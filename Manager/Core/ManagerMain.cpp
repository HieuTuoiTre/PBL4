#include "../../Shared/Network.h"
#include "DataReceiver.h"
#include "../UI/RemoteWindow.h"
#include "../UI/InputHandler.h"
#include "../Media/AudioPlayer.h"
#include <windows.h>
#include <gdiplus.h>
#include <cstdio>
#include <cstring>
#include <string>

#pragma comment(lib, "gdiplus.lib")

TcpClient g_client;

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    SetProcessDPIAware();

    AllocConsole();
    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stdout);
    printf("--- KHOI DONG MANAGER ---\n");
    printf("Nhap IP may Client: ");

    char ipBuf[256] = {0};
    if (fgets(ipBuf, sizeof(ipBuf), stdin)) {
        size_t len = strlen(ipBuf);
        if (len > 0 && ipBuf[len - 1] == '\n') ipBuf[len - 1] = '\0';
    }
    FreeConsole();

    std::string targetIP = ipBuf;
    bool connected = false;

    if (targetIP.empty()) {
        if (MessageBoxA(NULL, "Ban chua nhap IP.\nBan co muon dieu khien localhost (127.0.0.1) khong?", "Chua nhap IP", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            targetIP = "127.0.0.1";
        } else {
            return 0;
        }
    }

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

    if (g_client.Connect(targetIP, 8080)) {
        connected = true;
    } else {
        if (MessageBoxA(NULL, "Khong the ket noi toi IP da nhap!\nBan co muon thu dieu khien localhost (127.0.0.1) khong?", "Ket noi that bai", MB_YESNO | MB_ICONWARNING) == IDYES) {
            if (g_client.Connect("127.0.0.1", 8080)) {
                targetIP = "127.0.0.1";
                connected = true;
            } else {
                MessageBoxA(NULL, "Cung khong the ket noi toi localhost! Kiem tra lai Client.", "Loi", MB_OK | MB_ICONERROR);
            }
        }
    }

    if (connected) {
        InputHandler::Initialize(&g_client);
        AudioPlayer::Initialize(targetIP == "127.0.0.1");
        RemoteWindow::Create(hInst, &g_client);
        DataReceiver::Start(&g_client);

        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        DataReceiver::Stop();
        g_client.Close();
        AudioPlayer::Cleanup();
        RemoteWindow::Cleanup();
    }

    Gdiplus::GdiplusShutdown(gdiToken);
    Network::Cleanup();
    if (hComCtl) FreeLibrary(hComCtl);
    return 0;
}