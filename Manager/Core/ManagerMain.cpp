#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include "../UI/MainWindow.h"
#include "../UI/ViewerWindow.h"
#include "../UI/Dialogs.h"
#include "../UI/UITheme.h"
#include "../../Shared/Network.h"

namespace {

void InitializeCommonControls() {
    using InitCommonControlsExProc = BOOL(WINAPI*)(const INITCOMMONCONTROLSEX*);
    HMODULE commonControls = LoadLibraryW(L"comctl32.dll");
    if (!commonControls) {
        return;
    }

    const auto initialize = reinterpret_cast<InitCommonControlsExProc>(
        GetProcAddress(commonControls, "InitCommonControlsEx"));
    if (!initialize) {
        return;
    }

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
    initialize(&controls);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int commandShow) {
    SetProcessDPIAware();
    InitializeCommonControls();

    if (!Network::Initialize()) {
        MessageBoxW(nullptr, L"Không thể khởi tạo Winsock2!", L"Lỗi hệ thống", MB_OK | MB_ICONERROR);
        return 1;
    }

    UITheme::Initialize();

    if (!MainWindow::Register(hInstance) || !ViewerWindow::Register(hInstance) || !Dialogs::Register(hInstance)) {
        MessageBoxW(nullptr, L"Không thể đăng ký lớp cửa sổ!", L"Lỗi hệ thống", MB_OK | MB_ICONERROR);
        UITheme::Cleanup();
        Network::Cleanup();
        return 1;
    }

    HWND mainWindow = MainWindow::Create(hInstance, commandShow);
    if (!mainWindow) {
        MessageBoxW(nullptr, L"Không thể tạo cửa sổ chính!", L"Lỗi hệ thống", MB_OK | MB_ICONERROR);
        UITheme::Cleanup();
        Network::Cleanup();
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UITheme::Cleanup();
    Network::Cleanup();
    return static_cast<int>(msg.wParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    return wWinMain(hInstance, hPrevInstance, nullptr, nCmdShow);
}

int main() {
    return wWinMain(GetModuleHandleW(nullptr), nullptr, GetCommandLineW(), SW_SHOWNORMAL);
}