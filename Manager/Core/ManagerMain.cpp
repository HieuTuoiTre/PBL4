#include "../../Shared/Network.h"
#include "../../Shared/Protocol.h"
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <thread>
#include <vector>
#include <string>

#pragma comment(lib, "gdiplus.lib")

TcpClient g_client;
Gdiplus::Image* g_frame = nullptr;
CRITICAL_SECTION g_cs;
HWND g_hwnd = NULL;
HWND g_hTaskWnd = NULL;
HWND g_hTaskList = NULL;

bool g_isControlling = true; 

// ==========================================
// CỬA SỔ TASK MANAGER (CTRL+TAB BẬT LÊN)
// ==========================================
LRESULT CALLBACK TaskWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
        case WM_CREATE: {
            g_hTaskList = CreateWindowExA(0, "SysListView32", "", 
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL, 
                0, 0, 0, 0, hwnd, (HMENU)1, GetModuleHandle(NULL), NULL);
            
            SendMessageA(g_hTaskList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            
            LVCOLUMNA lvc = { LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM, LVCFMT_LEFT, 200, (LPSTR)"Ten Tien Trinh", 0, 0 };
            SendMessageA(g_hTaskList, LVM_INSERTCOLUMNA, 0, (LPARAM)&lvc);
            lvc.cx = 70; lvc.pszText = (LPSTR)"PID"; SendMessageA(g_hTaskList, LVM_INSERTCOLUMNA, 1, (LPARAM)&lvc);
            lvc.cx = 90; lvc.pszText = (LPSTR)"RAM (MB)"; SendMessageA(g_hTaskList, LVM_INSERTCOLUMNA, 2, (LPARAM)&lvc);
            
            g_client.SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0); // Lấy list lần đầu
            SetTimer(hwnd, 1, 3000, NULL); // Auto-refresh mỗi 3 giây
            return 0;
        }
        case WM_TIMER:
            g_client.SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
            return 0;
        case WM_SIZE:
            MoveWindow(g_hTaskList, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
            return 0;
        case WM_NOTIFY: { // Xử lý Double Click để Kill Process
            LPNMHDR nmh = (LPNMHDR)lp;
            if (nmh->idFrom == 1 && nmh->code == NM_DBLCLK) {
                int sel = SendMessageA(g_hTaskList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
                if (sel != -1) {
                    char pidStr[32];
                    LVITEMA lvi = {0}; lvi.iSubItem = 1; lvi.pszText = pidStr; lvi.cchTextMax = 32;
                    SendMessageA(g_hTaskList, LVM_GETITEMTEXTA, sel, (LPARAM)&lvi);
                    
                    uint32_t pid = std::stoul(pidStr);
                    if (MessageBoxA(hwnd, "Ban chac chan muon tat (Kill) tien trinh nay?", "Canh Bao", MB_YESNO | MB_ICONWARNING) == IDYES) {
                        g_client.SendPacket(Protocol::MSG_KILL_PROCESS, (char*)&pid, sizeof(pid));
                        g_client.SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0); // Lấy lại list
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

// ==========================================
// LUỒNG NGẦM: NHẬN ẢNH & DATA TASK MANAGER
// ==========================================
void ReceiveLoop() {
    Protocol::PacketHeader hdr;
    while (g_client.ReceiveExact((char*)&hdr, sizeof(hdr))) {
        if (hdr.size <= 0) continue;
        std::vector<char> buf(hdr.size);
        if (!g_client.ReceiveExact(buf.data(), hdr.size)) break;
        
        if (hdr.type == Protocol::MSG_VIDEO_FRAME) {
            Protocol::VideoDeltaHeader delta;
            memcpy(&delta, buf.data(), sizeof(delta));
            
            int jpgSize = buf.size() - sizeof(delta);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, jpgSize);
            if (!hMem) continue;
            memcpy(GlobalLock(hMem), buf.data() + sizeof(delta), jpgSize);
            GlobalUnlock(hMem);

            IStream* pStream = nullptr;
            if (CreateStreamOnHGlobal(hMem, TRUE, &pStream) == S_OK) {
                Gdiplus::Image* img = Gdiplus::Image::FromStream(pStream);
                if (img && img->GetLastStatus() == Gdiplus::Ok) {
                    EnterCriticalSection(&g_cs);
                    if (!g_frame) g_frame = new Gdiplus::Bitmap(delta.width, delta.height, PixelFormat32bppARGB);
                    Gdiplus::Graphics g(g_frame);
                    g.DrawImage(img, delta.x, delta.y, delta.width, delta.height);
                    LeaveCriticalSection(&g_cs);
                    InvalidateRect(g_hwnd, NULL, FALSE);
                }
                if (img) delete img;
                pStream->Release();
            } else GlobalFree(hMem);
        }
        else if (hdr.type == Protocol::MSG_PROCESS_LIST_RESPONSE && g_hTaskWnd) {
            // Nạp Data vào bảng Task Manager
            int count = hdr.size / sizeof(Protocol::ProcessInfoPayload);
            Protocol::ProcessInfoPayload* pList = (Protocol::ProcessInfoPayload*)buf.data();
            
            SendMessageA(g_hTaskList, LVM_DELETEALLITEMS, 0, 0); // Xóa cũ
            for (int i = 0; i < count; i++) {
                LVITEMA lvi = { LVIF_TEXT, i, 0, 0, 0, pList[i].name };
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
}

// ==========================================
// GỬI CHUỘT / PHÍM
// ==========================================
void SendMouse(uint8_t action, uint8_t btn, LPARAM lp, int16_t wheel = 0) {
    if (!g_frame || !g_isControlling) return;
    RECT r; GetClientRect(g_hwnd, &r);
    Protocol::MousePayload mp = { action, btn, (int16_t)(GET_X_LPARAM(lp) * g_frame->GetWidth() / r.right), (int16_t)(GET_Y_LPARAM(lp) * g_frame->GetHeight() / r.bottom), wheel };
    g_client.SendPacket(Protocol::MSG_MOUSE_EVENT, (char*)&mp, sizeof(mp));
}

void SendKey(WPARAM wp, uint8_t isDown) {
    if (!g_isControlling) return;
    Protocol::KeyboardPayload kp = { (uint32_t)wp, isDown };
    g_client.SendPacket(Protocol::MSG_KEY_EVENT, (char*)&kp, sizeof(kp));
}

// ==========================================
// LUỒNG CHÍNH: VẼ VÀ BẮT SỰ KIỆN
// ==========================================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            EnterCriticalSection(&g_cs);
            if (g_frame) {
                Gdiplus::Graphics g(hdc);
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
                RECT r; GetClientRect(hwnd, &r);
                g.DrawImage(g_frame, 0, 0, r.right, r.bottom);
            }
            LeaveCriticalSection(&g_cs);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            // 1. Chuyển đổi trạng thái bằng Ctrl + Alt
            if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000)) {
                if ((lp & (1 << 30)) == 0) {
                    g_isControlling = !g_isControlling;
                    SetWindowTextW(hwnd, g_isControlling ? L"LanRemote - [ĐANG ĐIỀU KHIỂN] (Ctrl+Tab: TaskMgr | Ctrl+Alt: Thoát)" : L"LanRemote - [CHỈ XEM] (Ctrl+Tab: TaskMgr | Ctrl+Alt: Điều khiển)");
                }
                return 0; 
            }
            
            // 2. Ctrl + Tab Mở Task Manager
            if (wp == VK_TAB && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                if (!g_hTaskWnd) {
                    WNDCLASSA wc = {}; wc.lpfnWndProc = TaskWndProc; wc.lpszClassName = "TaskClass"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
                    RegisterClassA(&wc);
                    g_hTaskWnd = CreateWindowExA(WS_EX_TOPMOST, "TaskClass", "Task Manager - Ctrl+Tab (Nhan dup de Kill)", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 420, 500, hwnd, NULL, GetModuleHandle(NULL), NULL);
                }
                return 0; // Chặn không gửi tổ hợp phím này sang Client
            }

            // Gửi phím bình thường
            if (g_isControlling) { SendKey(wp, 1); return 0; }
            break;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: { if (g_isControlling) { SendKey(wp, 0); return 0; } break; }

        case WM_MOUSEMOVE:   SendMouse(Protocol::MOUSE_ACTION_MOVE, Protocol::BUTTON_NONE, lp); return 0;
        case WM_LBUTTONDOWN: SendMouse(Protocol::MOUSE_ACTION_PRESS, Protocol::BUTTON_LEFT, lp); return 0;
        case WM_LBUTTONUP:   SendMouse(Protocol::MOUSE_ACTION_RELEASE, Protocol::BUTTON_LEFT, lp); return 0;
        case WM_RBUTTONDOWN: SendMouse(Protocol::MOUSE_ACTION_PRESS, Protocol::BUTTON_RIGHT, lp); return 0;
        case WM_RBUTTONUP:   SendMouse(Protocol::MOUSE_ACTION_RELEASE, Protocol::BUTTON_RIGHT, lp); return 0;
        case WM_MOUSEWHEEL:  SendMouse(Protocol::MOUSE_ACTION_WHEEL, Protocol::BUTTON_NONE, lp, GET_WHEEL_DELTA_WPARAM(wp)); return 0;

        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    SetProcessDPIAware(); 
    
    // --- BẬT CONSOLE ĐỂ NHẬP IP ---
    AllocConsole();
    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stdout);
    printf("--- KHOI DONG MANAGER ---\n");
    printf("Nhap IP may Client (Nhan Enter de dung 127.0.0.1): ");
    
    char ipBuf[256] = {0};
    fgets(ipBuf, sizeof(ipBuf), stdin);
    if (ipBuf[strlen(ipBuf) - 1] == '\n') ipBuf[strlen(ipBuf) - 1] = '\0';
    std::string targetIP = (strlen(ipBuf) > 0) ? ipBuf : "172.20.10.2";
    FreeConsole(); 
    // ------------------------------

    HMODULE hComCtl = LoadLibraryA("comctl32.dll");
    if (hComCtl) { typedef void (WINAPI *INITPROC)(void); INITPROC pInit = (INITPROC)GetProcAddress(hComCtl, "InitCommonControls"); if (pInit) pInit(); }

    Network::Initialize();
    InitializeCriticalSection(&g_cs);
    ULONG_PTR gdiToken; Gdiplus::GdiplusStartupInput gdiInput; Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, NULL);

    WNDCLASSW wc = {}; wc.lpfnWndProc = WndProc; wc.lpszClassName = L"MgrWindow"; RegisterClassW(&wc);
    g_hwnd = CreateWindowExW(0, L"MgrWindow", L"LanRemote - [ĐANG ĐIỀU KHIỂN] (Ctrl+Tab: TaskMgr | Ctrl+Alt: Thoát)", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, 0, 0, hInst, 0);

    if (g_client.Connect(targetIP, 8080)) { 
        std::thread(ReceiveLoop).detach(); 
    } else {
        MessageBoxA(NULL, "Khong the ket noi! Hay kiem tra lai IP, Firewall va Client.", "Loi", MB_OK);
    }

    MSG msg; while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }

    g_client.Close(); if (g_frame) delete g_frame; Gdiplus::GdiplusShutdown(gdiToken); DeleteCriticalSection(&g_cs); Network::Cleanup();
    return 0;
}