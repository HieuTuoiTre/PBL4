#include "TaskManagerWindow.h"
#include <commctrl.h>
#include <string>
#include <cstdio>

namespace {
    HWND g_hTaskWnd = NULL;
    HWND g_hTaskList = NULL;
    TcpClient* g_client = nullptr;

    LRESULT CALLBACK TaskWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_CREATE: {
                g_hTaskList = CreateWindowExA(0, "SysListView32", "",
                    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                    0, 0, 0, 0, hwnd, (HMENU)1, GetModuleHandle(NULL), NULL);

                SendMessageA(g_hTaskList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

                LVCOLUMNA lvc = { LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM, LVCFMT_LEFT, 200, (LPSTR)"Ten Tien Trinh", 0, 0 };
                SendMessageA(g_hTaskList, LVM_INSERTCOLUMNA, 0, (LPARAM)&lvc);
                lvc.cx = 70; lvc.pszText = (LPSTR)"PID"; SendMessageA(g_hTaskList, LVM_INSERTCOLUMNA, 1, (LPARAM)&lvc);
                lvc.cx = 90; lvc.pszText = (LPSTR)"RAM (MB)"; SendMessageA(g_hTaskList, LVM_INSERTCOLUMNA, 2, (LPARAM)&lvc);

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
            case WM_NOTIFY: {
                LPNMHDR nmh = (LPNMHDR)lp;
                if (nmh->idFrom == 1 && nmh->code == NM_DBLCLK && g_client) {
                    int sel = SendMessageA(g_hTaskList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
                    if (sel != -1) {
                        char pidStr[32];
                        LVITEMA lvi = {0}; lvi.iSubItem = 1; lvi.pszText = pidStr; lvi.cchTextMax = 32;
                        SendMessageA(g_hTaskList, LVM_GETITEMTEXTA, sel, (LPARAM)&lvi);

                        uint32_t pid = std::stoul(pidStr);
                        if (MessageBoxA(hwnd, "Ban chac chan muon tat (Kill) tien trinh nay?", "Canh Bao", MB_YESNO | MB_ICONWARNING) == IDYES) {
                            g_client->SendPacket(Protocol::MSG_KILL_PROCESS, (char*)&pid, sizeof(pid));
                            g_client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
                        }
                    }
                }
                return 0;
            }
            case WM_CLOSE:
                KillTimer(hwnd, 1);
                DestroyWindow(hwnd);
                g_hTaskWnd = NULL;
                return 0;
        }
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

namespace TaskManagerWindow {
    void Show(HWND hParent, TcpClient* client) {
        g_client = client;
        if (!g_hTaskWnd) {
            WNDCLASSA wc = {};
            wc.lpfnWndProc = TaskWndProc;
            wc.lpszClassName = "TaskClass";
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            RegisterClassA(&wc);

            g_hTaskWnd = CreateWindowExA(WS_EX_TOPMOST, "TaskClass", "Task Manager - Dang tai...",
                WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 450, 500, hParent, NULL, GetModuleHandle(NULL), NULL);
        } else {
            SetForegroundWindow(g_hTaskWnd);
        }
    }

    void UpdateSysInfo(const Protocol::SysInfoPayload& sys) {
        if (!g_hTaskWnd) return;
        char title[128];
        sprintf(title, "Task Manager - CPU: %d%% | RAM: %d%% | Disk: %d%% (Nhan dup de Kill)", sys.cpuUsage, sys.ramUsage, sys.diskUsage);
        SetWindowTextA(g_hTaskWnd, title);
    }

    void UpdateProcessList(const Protocol::ProcessInfoPayload* pList, int count) {
        if (!g_hTaskWnd || !g_hTaskList) return;
        SendMessageA(g_hTaskList, LVM_DELETEALLITEMS, 0, 0);
        for (int i = 0; i < count; i++) {
            LVITEMA lvi = { LVIF_TEXT, i, 0, 0, 0, (LPSTR)pList[i].name };
            SendMessageA(g_hTaskList, LVM_INSERTITEMA, 0, (LPARAM)&lvi);

            std::string pidStr = std::to_string(pList[i].pid);
            std::string ramStr = std::to_string(pList[i].ramUsageMB);

            lvi.iSubItem = 1; lvi.pszText = (LPSTR)pidStr.c_str();
            SendMessageA(g_hTaskList, LVM_SETITEMTEXTA, i, (LPARAM)&lvi);
            lvi.iSubItem = 2; lvi.pszText = (LPSTR)ramStr.c_str();
            SendMessageA(g_hTaskList, LVM_SETITEMTEXTA, i, (LPARAM)&lvi);
        }
    }
}