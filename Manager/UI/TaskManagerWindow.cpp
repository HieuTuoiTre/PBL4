#include "TaskManagerWindow.h"
#include "../../Shared/Utils.h"
#include <commctrl.h>
#include <vector>
#include <string>
#include <cwchar>

#define WM_TASK_UPDATE_SYS  (WM_APP + 101)
#define WM_TASK_UPDATE_LIST (WM_APP + 102)

namespace {
    HWND g_hTaskWnd = NULL;
    HWND g_hTaskList = NULL;
    TcpClient* g_client = nullptr;

    CRITICAL_SECTION g_dataCs;
    bool g_csInit = false;
    Protocol::SysInfoPayload g_latestSys = {};
    std::vector<Protocol::ProcessInfoPayload> g_latestProcesses;

    void RenderProcessListOnUI() {
        if (!g_hTaskWnd || !g_hTaskList) return;

        EnterCriticalSection(&g_dataCs);
        std::vector<Protocol::ProcessInfoPayload> pList = g_latestProcesses;
        LeaveCriticalSection(&g_dataCs);

        int count = (int)pList.size();

        // 1. Lưu lại PID đang được chọn
        std::wstring selectedPid = L"";
        int curSel = (int)SendMessageW(g_hTaskList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
        if (curSel != -1) {
            wchar_t buf[32] = {0};
            LVITEMW lvi = {};
            lvi.iSubItem = 1;
            lvi.pszText = buf;
            lvi.cchTextMax = 32;
            SendMessageW(g_hTaskList, LVM_GETITEMTEXTW, curSel, (LPARAM)&lvi);
            selectedPid = buf;
        }

        // 2. Khóa vẽ tạm thời để cập nhật tại chỗ (In-place) không làm nhảy thanh cuộn
        SendMessageW(g_hTaskList, WM_SETREDRAW, FALSE, 0);

        int existingCount = (int)SendMessageW(g_hTaskList, LVM_GETITEMCOUNT, 0, 0);

        for (int i = 0; i < count; i++) {
            std::wstring nameW = Utils::Utf8ToWide(pList[i].name);
            std::wstring pidW = std::to_wstring(pList[i].pid);
            std::wstring ramW = std::to_wstring(pList[i].ramUsageMB);

            LVITEMW lvi = {};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = i;
            lvi.iSubItem = 0;
            lvi.pszText = (LPWSTR)nameW.c_str();

            if (i >= existingCount) {
                SendMessageW(g_hTaskList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
            } else {
                SendMessageW(g_hTaskList, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);
            }

            lvi.iSubItem = 1;
            lvi.pszText = (LPWSTR)pidW.c_str();
            SendMessageW(g_hTaskList, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);

            lvi.iSubItem = 2;
            lvi.pszText = (LPWSTR)ramW.c_str();
            SendMessageW(g_hTaskList, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);

            if (!selectedPid.empty() && pidW == selectedPid) {
                LVITEMW stateItem = {};
                stateItem.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
                stateItem.state = LVIS_SELECTED | LVIS_FOCUSED;
                SendMessageW(g_hTaskList, LVM_SETITEMSTATE, i, (LPARAM)&stateItem);
            }
        }

        for (int i = existingCount - 1; i >= count; i--) {
            SendMessageW(g_hTaskList, LVM_DELETEITEM, i, 0);
        }

        SendMessageW(g_hTaskList, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(g_hTaskList, NULL, FALSE);
    }

    LRESULT CALLBACK TaskWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_CREATE: {
                g_hTaskWnd = hwnd;
                g_hTaskList = CreateWindowExW(0, L"SysListView32", L"",
                    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                    0, 0, 0, 0, hwnd, (HMENU)1, GetModuleHandle(NULL), NULL);

                SendMessageW(g_hTaskList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                    LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

                LVCOLUMNW lvc = { LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM, LVCFMT_LEFT, 220, (LPWSTR)L"Tên Tiến Trình", 0, 0 };
                SendMessageW(g_hTaskList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);
                lvc.cx = 80; lvc.pszText = (LPWSTR)L"PID"; SendMessageW(g_hTaskList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);
                lvc.cx = 100; lvc.pszText = (LPWSTR)L"RAM (MB)"; SendMessageW(g_hTaskList, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);

                if (g_client) {
                    g_client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
                    g_client->SendPacket(Protocol::MSG_SYS_INFO_REQUEST, nullptr, 0);
                }
                SetTimer(hwnd, 1, 3000, NULL);
                return 0;
            }
            case WM_TIMER:
                if (g_client) {
                    g_client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
                    g_client->SendPacket(Protocol::MSG_SYS_INFO_REQUEST, nullptr, 0);
                }
                return 0;
            case WM_SIZE:
                MoveWindow(g_hTaskList, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
                return 0;
            case WM_TASK_UPDATE_SYS: {
                EnterCriticalSection(&g_dataCs);
                Protocol::SysInfoPayload sys = g_latestSys;
                LeaveCriticalSection(&g_dataCs);

                wchar_t title[128];
                swprintf(title, 128, L"Task Manager - CPU: %d%% | RAM: %d%% | Disk: %d%% (Nhấp đúp để Kill)",
                         sys.cpuUsage, sys.ramUsage, sys.diskUsage);
                SetWindowTextW(hwnd, title);
                return 0;
            }
            case WM_TASK_UPDATE_LIST: {
                RenderProcessListOnUI();
                return 0;
            }
            case WM_NOTIFY: {
                LPNMHDR nmh = (LPNMHDR)lp;
                if (nmh->idFrom == 1 && nmh->code == NM_DBLCLK && g_client) {
                    int sel = (int)SendMessageW(g_hTaskList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
                    if (sel != -1) {
                        wchar_t pidStr[32] = {0};
                        LVITEMW lvi = {};
                        lvi.iSubItem = 1;
                        lvi.pszText = pidStr;
                        lvi.cchTextMax = 32;
                        SendMessageW(g_hTaskList, LVM_GETITEMTEXTW, sel, (LPARAM)&lvi);

                        if (wcslen(pidStr) > 0) {
                            wchar_t* endPtr = nullptr;
                            uint32_t pid = (uint32_t)wcstoul(pidStr, &endPtr, 10);
                            if (endPtr != pidStr && pid > 0) {
                                if (MessageBoxW(hwnd, L"Bạn chắc chắn muốn tắt (Kill) tiến trình này?", L"Cảnh Báo", MB_YESNO | MB_ICONWARNING) == IDYES) {
                                    g_client->SendPacket(Protocol::MSG_KILL_PROCESS, (char*)&pid, sizeof(pid));
                                    g_client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
                                }
                            }
                        }
                    }
                }
                return 0;
            }
            case WM_CLOSE:
                KillTimer(hwnd, 1);
                DestroyWindow(hwnd);
                g_hTaskWnd = NULL;
                g_hTaskList = NULL;
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

namespace TaskManagerWindow {
    void Show(HWND hParent, TcpClient* client) {
        if (!g_csInit) {
            InitializeCriticalSection(&g_dataCs);
            g_csInit = true;
        }
        g_client = client;
        if (!g_hTaskWnd) {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = TaskWndProc;
            wc.lpszClassName = L"TaskClassW";
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            RegisterClassW(&wc);

            CreateWindowExW(WS_EX_TOPMOST, L"TaskClassW", L"Task Manager - Đang tải...",
                WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 460, 500, hParent, NULL, GetModuleHandle(NULL), NULL);
        } else {
            SetForegroundWindow(g_hTaskWnd);
        }
    }

    void UpdateSysInfo(const Protocol::SysInfoPayload& sys) {
        if (!g_csInit || !g_hTaskWnd) return;
        EnterCriticalSection(&g_dataCs);
        g_latestSys = sys;
        LeaveCriticalSection(&g_dataCs);
        PostMessageW(g_hTaskWnd, WM_TASK_UPDATE_SYS, 0, 0);
    }

    void UpdateProcessList(const Protocol::ProcessInfoPayload* pList, int count) {
        if (!g_csInit || !g_hTaskWnd || count <= 0 || !pList) return;
        EnterCriticalSection(&g_dataCs);
        g_latestProcesses.assign(pList, pList + count);
        LeaveCriticalSection(&g_dataCs);
        PostMessageW(g_hTaskWnd, WM_TASK_UPDATE_LIST, 0, 0);
    }
}