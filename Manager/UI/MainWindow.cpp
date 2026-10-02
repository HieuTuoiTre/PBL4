#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include "MainWindow.h"
#include "ViewerWindow.h"
#include "Dialogs.h"
#include "UITheme.h"
#include "../../Shared/Network.h"
#include "../../Shared/Utils.h"
#include <commctrl.h>
#include <algorithm>
#include <vector>

namespace MainWindow {

constexpr int ID_IP_INPUT = 1001;
constexpr int ID_CONNECT = 1002;
constexpr int ID_DEVICE_LIST = 1003;

struct DeviceInfo {
    std::wstring id;
    std::wstring name;
    std::wstring ip;
    std::wstring status;
};

struct MainState {
    HWND window = nullptr;
    HWND ipInput = nullptr;
    HWND connectButton = nullptr;
    HWND deviceList = nullptr;
    int selectedDevice = -1;
    std::vector<DeviceInfo> devices;
};

static MainState gMain;

HWND GetHWND() {
    return gMain.window;
}

void Show(bool bShow) {
    if (gMain.window && IsWindow(gMain.window)) {
        if (bShow) {
            EnableWindow(gMain.window, TRUE);
            ShowWindow(gMain.window, SW_SHOWNORMAL);
            SetWindowPos(gMain.window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            SetForegroundWindow(gMain.window);
        } else {
            ShowWindow(gMain.window, SW_HIDE);
        }
    }
}

void AddDevice(const std::wstring& id, const std::wstring& name, const std::wstring& ip, const std::wstring& status) {
    gMain.devices.push_back({ id, name, ip, status });
    if (gMain.deviceList && IsWindow(gMain.deviceList)) {
        int index = static_cast<int>(gMain.devices.size()) - 1;
        UITheme::AddListRow(gMain.deviceList, index, id.c_str());
        UITheme::SetListItemText(gMain.deviceList, index, 1, name.c_str());
        UITheme::SetListItemText(gMain.deviceList, index, 2, ip.c_str());
        UITheme::SetListItemText(gMain.deviceList, index, 3, status.c_str());
    }
}

void ClearDevices() {
    gMain.devices.clear();
    gMain.selectedDevice = -1;
    if (gMain.deviceList && IsWindow(gMain.deviceList)) {
        ListView_DeleteAllItems(gMain.deviceList);
    }
}

static void ResizeMainControls(HWND window) {
    RECT client{};
    GetClientRect(window, &client);
    constexpr int margin = 16;
    constexpr int gap = 12;
    constexpr int rowHeight = 36;
    constexpr int buttonWidth = 170;
    const int width = std::max(1, static_cast<int>(client.right) - margin * 2);
    const int inputWidth = std::max(100, width - buttonWidth - gap);

    MoveWindow(gMain.ipInput, margin, margin, inputWidth, rowHeight, TRUE);
    MoveWindow(gMain.connectButton, margin + inputWidth + gap, margin, buttonWidth, rowHeight, TRUE);
    MoveWindow(gMain.deviceList, margin, margin + rowHeight + 16, width,
               std::max(1, static_cast<int>(client.bottom) - (margin + rowHeight + 32)), TRUE);

    const int listWidth = std::max(1, width);
    const int remaining = std::max(1, listWidth - 55);
    ListView_SetColumnWidth(gMain.deviceList, 0, 55);
    ListView_SetColumnWidth(gMain.deviceList, 1, remaining * 40 / 100);
    ListView_SetColumnWidth(gMain.deviceList, 2, remaining * 25 / 100);
    ListView_SetColumnWidth(gMain.deviceList, 3, remaining * 35 / 100);
}

static void InitializeDeviceListColumns(HWND list) {
    UITheme::AddListColumn(list, 0, L"ID", 55);
    UITheme::AddListColumn(list, 1, L"Tên máy", 270);
    UITheme::AddListColumn(list, 2, L"Địa chỉ IP", 180);
    UITheme::AddListColumn(list, 3, L"Trạng thái", 200);
}

static void HandleConnectRequest(HWND window) {
    const std::wstring ip = UITheme::GetControlText(gMain.ipInput);
    if (ip.empty()) {
        Dialogs::ShowWarning(window, L"Vui lòng nhập địa chỉ IP hợp lệ trước khi kết nối!");
        return;
    }

    std::wstring name = L"Remote Client";
    const int selected = ListView_GetNextItem(gMain.deviceList, -1, LVNI_SELECTED);
    if (selected >= 0 && selected < static_cast<int>(gMain.devices.size())) {
        name = gMain.devices[selected].name;
    }

    // 1. Hộp thoại xác nhận kết nối
    if (!Dialogs::ShowConfirm(window, ip, name)) {
        return;
    }

    // 2. Hộp thoại đang kết nối (hiển thị animation "Đang kết nối tới máy (IP)..." và nút Hủy)
    TcpClient* client = nullptr;
    if (Dialogs::ShowConnecting(window, ip, name, client)) {
        // Kết nối socket thành công -> mở cửa sổ ViewerWindow
        ViewerWindow::CreateViewer(window, ip, name, client);
    } else {
        // Kết nối thất bại
        if (!client) {
            std::wstring errMsg = L"Error: Không thể kết nối tới " + ip + L":8080!\nVui lòng kiểm tra lại địa chỉ IP hoặc đảm bảo Client đang chạy.";
            auto res = Dialogs::ShowError(window, errMsg, true);
            if (res == Dialogs::DialogResult::Retry) {
                HandleConnectRequest(window);
            }
        }
    }
}

static LRESULT CALLBACK MainWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        gMain.window = window;
        gMain.ipInput = CreateWindowExW(0, L"EDIT", L"127.0.0.1", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                        0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_IP_INPUT), nullptr, nullptr);
        gMain.connectButton = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                              0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_CONNECT), nullptr, nullptr);
        gMain.deviceList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                           WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                           0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_DEVICE_LIST), nullptr, nullptr);

        UITheme::SetControlFont(gMain.ipInput);
        UITheme::SetControlFont(gMain.connectButton);
        UITheme::SetControlFont(gMain.deviceList);
        ListView_SetExtendedListViewStyle(gMain.deviceList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        InitializeDeviceListColumns(gMain.deviceList);
        ResizeMainControls(window);

        // Nạp thiết bị mẫu mặc định
        AddDevice(L"1", L"Máy Client cục bộ", L"127.0.0.1", L"Sẵn sàng");
        AddDevice(L"2", L"Phòng máy - Lab 01", L"192.168.1.105", L"Chờ kết nối");
        return 0;
    }
    case WM_SIZE:
        ResizeMainControls(window);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_CONNECT && HIWORD(wParam) == BN_CLICKED) {
            HandleConnectRequest(window);
        }
        return 0;
    case WM_NOTIFY: {
        const NMHDR* notification = reinterpret_cast<const NMHDR*>(lParam);
        if (notification->idFrom == ID_DEVICE_LIST) {
            if (notification->code == LVN_ITEMCHANGED) {
                const NMLISTVIEW* change = reinterpret_cast<const NMLISTVIEW*>(lParam);
                if (change->iItem >= 0 && (change->uNewState & LVIS_SELECTED) != 0) {
                    gMain.selectedDevice = change->iItem;
                    if (change->iItem < static_cast<int>(gMain.devices.size())) {
                        SetWindowTextW(gMain.ipInput, gMain.devices[change->iItem].ip.c_str());
                    }
                }
            } else if (notification->code == NM_DBLCLK) {
                HandleConnectRequest(window);
            }
        }
        return 0;
    }
    case WM_DRAWITEM: {
        const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (draw->CtlID == ID_CONNECT) {
            RECT rect = draw->rcItem;
            HDC dc = draw->hDC;
            const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
            SetBkMode(dc, TRANSPARENT);
            UITheme::DrawRoundedPanel(dc, rect, pressed ? RGB(11, 94, 215) : RGB(13, 110, 253), RGB(13, 110, 253));
            SetTextColor(dc, RGB(255, 255, 255));
            SelectObject(dc, UITheme::GetBoldFont());
            DrawTextW(dc, L"Yêu cầu kết nối", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        return FALSE;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, RGB(255, 255, 255));
        SetTextColor(dc, RGB(30, 41, 59));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }
    case WM_ERASEBKGND: {
        RECT rect{};
        GetClientRect(window, &rect);
        UITheme::FillSolid(reinterpret_cast<HDC>(wParam), rect, RGB(240, 242, 245));
        return 1;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool Register(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClassName;
    return RegisterClassExW(&wc) != 0;
}

HWND Create(HINSTANCE hInstance, int nCmdShow) {
    HWND window = CreateWindowExW(0, kWindowClassName, L"Manager App - Connection Panel",
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 980, 620,
                                  nullptr, nullptr, hInstance, nullptr);
    if (window) {
        ShowWindow(window, nCmdShow == SW_HIDE ? SW_SHOWNORMAL : nCmdShow);
        UpdateWindow(window);
    }
    return window;
}

} // namespace MainWindow

