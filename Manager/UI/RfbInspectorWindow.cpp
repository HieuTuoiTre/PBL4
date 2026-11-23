#include "RfbInspectorWindow.h"
#include <commctrl.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <mutex>

namespace {
    HWND g_hwnd = NULL;
    HWND g_hListView = NULL;
    HWND g_hBtnClear = NULL;
    HWND g_hBtnPause = NULL;
    HWND g_hStatusText = NULL;

    bool g_autoScroll = true;
    std::mutex g_logMutex;
    uint32_t g_packetCounter = 0;

    std::wstring Utf8ToWide(const std::string& str) {
        if (str.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
        std::wstring wstr(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], len);
        return wstr;
    }

    std::wstring GetCurrentTimeStr() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        auto timer = std::chrono::system_clock::to_time_t(now);
        std::tm bt;
        localtime_s(&bt, &timer);
        std::wstringstream wss;
        wss << std::setfill(L'0')
            << std::setw(2) << bt.tm_hour << L":"
            << std::setw(2) << bt.tm_min << L":"
            << std::setw(2) << bt.tm_sec << L"."
            << std::setw(3) << ms.count();
        return wss.str();
    }

    LRESULT CALLBACK InspectorWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_COMMAND: {
                int id = LOWORD(wp);
                if (id == 1001) { // Xóa nhật ký
                    std::lock_guard<std::mutex> lock(g_logMutex);
                    ListView_DeleteAllItems(g_hListView);
                    g_packetCounter = 0;
                    SetWindowTextW(g_hStatusText, L"Tổng số gói: 0");
                } else if (id == 1002) { // Tạm dừng / Tiếp tục
                    g_autoScroll = !g_autoScroll;
                    SetWindowTextW(g_hBtnPause, g_autoScroll ? L"Tạm dừng cuộn" : L"Bật tự cuộn");
                }
                return 0;
            }

            case WM_SIZE: {
                RECT r;
                GetClientRect(hwnd, &r);
                int btnBarHeight = 40;
                if (g_hListView) {
                    MoveWindow(g_hListView, 5, 5, r.right - 10, r.bottom - btnBarHeight - 10, TRUE);
                }
                if (g_hBtnClear) {
                    MoveWindow(g_hBtnClear, 10, r.bottom - 35, 110, 26, TRUE);
                }
                if (g_hBtnPause) {
                    MoveWindow(g_hBtnPause, 130, r.bottom - 35, 120, 26, TRUE);
                }
                if (g_hStatusText) {
                    MoveWindow(g_hStatusText, 270, r.bottom - 30, r.right - 280, 20, TRUE);
                }
                return 0;
            }

            case WM_CLOSE:
                ShowWindow(hwnd, SW_HIDE);
                return 0;

            case WM_DESTROY:
                g_hwnd = NULL;
                g_hListView = NULL;
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
} // namespace

namespace RfbInspector {

    void Show(HWND hParent) {
        if (g_hwnd) {
            ShowWindow(g_hwnd, SW_SHOW);
            SetForegroundWindow(g_hwnd);
            return;
        }

        HINSTANCE hInst = GetModuleHandle(NULL);
        WNDCLASSW wc = {};
        wc.lpfnWndProc = InspectorWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"PBL4_RfbInspectorWnd";
        RegisterClassW(&wc);

        g_hwnd = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            L"PBL4_RfbInspectorWnd",
            L"RFB Protocol Inspector (Giao thức VNC - RFC 6143)",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            100, 100, 850, 480,
            hParent, NULL, hInst, NULL
        );

        if (!g_hwnd) return;

        RECT r;
        GetClientRect(g_hwnd, &r);
        int btnBarHeight = 40;

        g_hListView = CreateWindowExW(
            0, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            5, 5, r.right - 10, r.bottom - btnBarHeight - 10,
            g_hwnd, NULL, hInst, NULL
        );

        ListView_SetExtendedListViewStyle(g_hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        // Thêm 5 cột chuẩn
        LVCOLUMNW lvc = {};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

        lvc.iSubItem = 0; lvc.cx = 105; lvc.pszText = (LPWSTR)L"Thời gian";
        ListView_InsertColumn(g_hListView, 0, &lvc);

        lvc.iSubItem = 1; lvc.cx = 65;  lvc.pszText = (LPWSTR)L"Hướng";
        ListView_InsertColumn(g_hListView, 1, &lvc);

        lvc.iSubItem = 2; lvc.cx = 160; lvc.pszText = (LPWSTR)L"Thông điệp RFB";
        ListView_InsertColumn(g_hListView, 2, &lvc);

        lvc.iSubItem = 3; lvc.cx = 95;  lvc.pszText = (LPWSTR)L"Kích thước";
        ListView_InsertColumn(g_hListView, 3, &lvc);

        lvc.iSubItem = 4; lvc.cx = 400; lvc.pszText = (LPWSTR)L"Chi tiết giải mã gói tin";
        ListView_InsertColumn(g_hListView, 4, &lvc);

        g_hBtnClear = CreateWindowExW(0, L"BUTTON", L"Xóa nhật ký", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, r.bottom - 35, 110, 26, g_hwnd, (HMENU)1001, hInst, NULL);

        g_hBtnPause = CreateWindowExW(0, L"BUTTON", L"Tạm dừng cuộn", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            130, r.bottom - 35, 120, 26, g_hwnd, (HMENU)1002, hInst, NULL);

        g_hStatusText = CreateWindowExW(0, L"STATIC", L"Tổng số gói: 0", WS_CHILD | WS_VISIBLE,
            270, r.bottom - 30, r.right - 280, 20, g_hwnd, NULL, hInst, NULL);
    }

    void Toggle(HWND hParent) {
        if (g_hwnd && IsWindowVisible(g_hwnd)) {
            ShowWindow(g_hwnd, SW_HIDE);
        } else {
            Show(hParent);
        }
    }

    void LogPacket(const std::string& dir, const std::string& type, size_t size, const std::string& desc) {
        if (!g_hwnd || !g_hListView) return;

        std::lock_guard<std::mutex> lock(g_logMutex);
        g_packetCounter++;

        std::wstring timeStr = GetCurrentTimeStr();
        std::wstring dirStr = Utf8ToWide(dir);
        std::wstring typeStr = Utf8ToWide(type);
        std::wstring sizeStr;
        if (size >= 1024 * 1024) {
            sizeStr = std::to_wstring(size / (1024 * 1024)) + L" MB";
        } else if (size >= 1024) {
            sizeStr = std::to_wstring(size / 1024) + L" KB";
        } else {
            sizeStr = std::to_wstring(size) + L" B";
        }
        std::wstring descStr = Utf8ToWide(desc);

        int count = ListView_GetItemCount(g_hListView);
        if (count > 2000) {
            ListView_DeleteItem(g_hListView, 0);
            count--;
        }

        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = count;
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)timeStr.c_str();

        int index = ListView_InsertItem(g_hListView, &lvi);
        ListView_SetItemText(g_hListView, index, 1, (LPWSTR)dirStr.c_str());
        ListView_SetItemText(g_hListView, index, 2, (LPWSTR)typeStr.c_str());
        ListView_SetItemText(g_hListView, index, 3, (LPWSTR)sizeStr.c_str());
        ListView_SetItemText(g_hListView, index, 4, (LPWSTR)descStr.c_str());

        if (g_autoScroll) {
            ListView_EnsureVisible(g_hListView, index, FALSE);
        }

        if (g_hStatusText && (g_packetCounter % 10 == 0)) {
            std::wstring st = L"Tổng số gói RFB ghi nhận: " + std::to_wstring(g_packetCounter);
            SetWindowTextW(g_hStatusText, st.c_str());
        }
    }

    void Close() {
        if (g_hwnd) {
            DestroyWindow(g_hwnd);
            g_hwnd = NULL;
            g_hListView = NULL;
        }
    }

    bool IsOpen() {
        return (g_hwnd != NULL && IsWindowVisible(g_hwnd));
    }

} // namespace RfbInspector
