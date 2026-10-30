#include "ClientChatWindow.h"
#include "../../Shared/Utils.h"
#include <commctrl.h>
#include <cmath>
#include <vector>
#include <string>
#include <thread>
#include <atomic>

#define WM_CHAT_CLIENT_SHOW       (WM_APP + 301)
#define WM_CHAT_CLIENT_HIDE       (WM_APP + 302)
#define WM_CHAT_CLIENT_NEW_MSG    (WM_APP + 303)
#define WM_CHAT_CLIENT_CLOSE      (WM_APP + 304)

#define IDC_CLIENT_CHAT_HISTORY   3001
#define IDC_CLIENT_CHAT_INPUT     3002
#define IDC_CLIENT_CHAT_SEND      3003

namespace {
    HWND g_hClientChatWnd = NULL;
    HWND g_hHistoryEdit = NULL;
    HWND g_hInputEdit = NULL;
    HWND g_hBtnSend = NULL;
    WNDPROC g_oldEditProc = NULL;
    HFONT g_hFontNormal = NULL;
    HFONT g_hFontBold = NULL;
    HBRUSH g_hBgBrush = NULL;

    TcpServer* g_controlServer = nullptr;
    CRITICAL_SECTION g_cs;
    bool g_csInit = false;

    std::vector<std::wstring> g_pendingMessages;
    std::thread g_uiThread;
    DWORD g_uiThreadId = 0;
    std::atomic<bool> g_isThreadRunning(false);
    std::atomic<bool> g_isWindowReady(false);

    float GetDpiScale(HWND hwnd) {
        typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
        static PFN_GetDpiForWindow pGetDpiForWindow = (PFN_GetDpiForWindow)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        UINT dpi = 96;
        if (pGetDpiForWindow && hwnd) {
            dpi = pGetDpiForWindow(hwnd);
        } else {
            HDC hdc = GetDC(hwnd);
            if (hdc) {
                dpi = GetDeviceCaps(hdc, LOGPIXELSY);
                ReleaseDC(hwnd, hdc);
            }
        }
        if (dpi == 0) dpi = 96;
        return (float)dpi / 96.0f;
    }

    std::wstring GetCurrentTimeString() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buf[16];
        swprintf_s(buf, L"[%02d:%02d]", st.wHour, st.wMinute);
        return buf;
    }

    void AppendMessageToHistory(const std::wstring& line) {
        if (!g_hHistoryEdit || !IsWindow(g_hHistoryEdit)) return;
        int len = GetWindowTextLengthW(g_hHistoryEdit);
        SendMessageW(g_hHistoryEdit, EM_SETSEL, len, len);
        SendMessageW(g_hHistoryEdit, EM_REPLACESEL, FALSE, (LPARAM)(line + L"\r\n").c_str());
        SendMessageW(g_hHistoryEdit, EM_SCROLLCARET, 0, 0);
    }

    LRESULT CALLBACK ClientInputEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if ((uMsg == WM_KEYDOWN || uMsg == WM_CHAR) && wParam == VK_RETURN) {
            if (uMsg == WM_KEYDOWN) {
                SendMessageW(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(IDC_CLIENT_CHAT_SEND, BN_CLICKED), (LPARAM)hWnd);
            }
            return 0; // Chặn cả KEYDOWN lẫn CHAR để không phát tiếng bíp của Windows
        }
        return CallWindowProcW(g_oldEditProc, hWnd, uMsg, wParam, lParam);
    }

    void SendCurrentMessage() {
        if (!g_hInputEdit || !g_controlServer) return;

        wchar_t buf[1024] = {0};
        GetWindowTextW(g_hInputEdit, buf, 1024);
        std::wstring wmsg = buf;

        while (!wmsg.empty() && (wmsg.back() == L' ' || wmsg.back() == L'\r' || wmsg.back() == L'\n' || wmsg.back() == L'\t')) {
            wmsg.pop_back();
        }
        size_t first = wmsg.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos) return;
        wmsg = wmsg.substr(first);

        std::string utf8Msg = Utils::WideToUtf8(wmsg);
        if (utf8Msg.empty()) return;

        Protocol::ChatMessagePayload payload = {0};
        strncpy_s(payload.sender, "Máy khách", sizeof(payload.sender) - 1);
        strncpy_s(payload.message, utf8Msg.c_str(), sizeof(payload.message) - 1);
        payload.timestamp = GetTickCount64();

        if (g_controlServer->SendPacket(Protocol::MSG_CHAT_MESSAGE, (const char*)&payload, sizeof(payload))) {
            std::wstring timeStr = GetCurrentTimeString();
            AppendMessageToHistory(timeStr + L" Bạn: " + wmsg);
            SetWindowTextW(g_hInputEdit, L"");
            SetFocus(g_hInputEdit);
        }
    }

    LRESULT CALLBACK ClientChatWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        float scale = GetDpiScale(hwnd);
        int headerH = (int)std::round(48.0f * scale);
        int inputH = (int)std::round(34.0f * scale);
        int pad = (int)std::round(12.0f * scale);
        int btnW = (int)std::round(76.0f * scale);

        switch (msg) {
            case WM_CREATE: {
                g_hHistoryEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_CLIENT_CHAT_HISTORY, GetModuleHandle(NULL), NULL);

                g_hInputEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_CLIENT_CHAT_INPUT, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hInputEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Nhập tin nhắn... (Nhấn Enter để gửi)");
                g_oldEditProc = (WNDPROC)SetWindowLongPtrW(g_hInputEdit, GWLP_WNDPROC, (LONG_PTR)ClientInputEditSubclassProc);

                g_hBtnSend = CreateWindowExW(0, L"BUTTON", L"Gửi ➤",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_CLIENT_CHAT_SEND, GetModuleHandle(NULL), NULL);

                g_hFontNormal = CreateFontW((int)(-13 * scale), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
                g_hFontBold = CreateFontW((int)(-13 * scale), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
                SendMessageW(g_hHistoryEdit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SendMessageW(g_hInputEdit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

                EnterCriticalSection(&g_cs);
                for (const auto& line : g_pendingMessages) {
                    AppendMessageToHistory(line);
                }
                g_pendingMessages.clear();
                LeaveCriticalSection(&g_cs);

                SetFocus(g_hInputEdit);
                return 0;
            }

            case WM_SIZE: {
                int w = LOWORD(lp);
                int h = HIWORD(lp);

                int historyY = headerH + pad;
                int historyH = h - historyY - inputH - pad * 2;
                if (historyH < 50) historyH = 50;

                MoveWindow(g_hHistoryEdit, pad, historyY, w - pad * 2, historyH, TRUE);

                int inputY = h - inputH - pad;
                int inputW = w - pad * 3 - btnW;
                if (inputW < 80) inputW = 80;

                MoveWindow(g_hInputEdit, pad, inputY, inputW, inputH, TRUE);
                MoveWindow(g_hBtnSend, pad + inputW + pad, inputY, btnW, inputH, TRUE);

                RECT headerRc = { 0, 0, w, headerH };
                InvalidateRect(hwnd, &headerRc, FALSE);
                return 0;
            }

            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT rc;
                GetClientRect(hwnd, &rc);

                // Dùng GDI chuẩn kèm double-buffering: cực kỳ ổn định, siêu nhẹ và 0 phụ thuộc
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
                HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

                // 1. Vẽ Header phong cách Slate Dark (#1E293B)
                RECT headerRc = { 0, 0, rc.right, headerH };
                HBRUSH hHeaderBrush = CreateSolidBrush(RGB(30, 41, 59));
                FillRect(memDC, &headerRc, hHeaderBrush);
                DeleteObject(hHeaderBrush);

                SetBkMode(memDC, TRANSPARENT);
                SetTextColor(memDC, RGB(255, 255, 255));
                SelectObject(memDC, g_hFontBold);
                RECT rTitle = { (int)(14 * scale), (int)(8 * scale), rc.right - 10, (int)(28 * scale) };
                DrawTextW(memDC, L"💬 Trò chuyện với Quản lý", -1, &rTitle, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

                SetTextColor(memDC, RGB(148, 163, 184));
                SelectObject(memDC, g_hFontNormal);
                RECT rSub = { (int)(14 * scale), (int)(27 * scale), rc.right - 10, (int)(44 * scale) };
                DrawTextW(memDC, L"Phiên hỗ trợ trực tiếp từ xa (Hỏi đáp nhanh)", -1, &rSub, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

                // 2. Nền phần dưới
                RECT bottomRc = { 0, headerH, rc.right, rc.bottom };
                if (!g_hBgBrush) g_hBgBrush = CreateSolidBrush(RGB(248, 250, 252));
                FillRect(memDC, &bottomRc, g_hBgBrush);

                BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);

                SelectObject(memDC, oldBmp);
                DeleteObject(memBmp);
                DeleteDC(memDC);

                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_DRAWITEM: {
                LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lp;
                if (!pDIS || pDIS->CtlID != IDC_CLIENT_CHAT_SEND) break;

                bool isPressed = (pDIS->itemState & ODS_SELECTED);
                COLORREF clrBtn = isPressed ? RGB(14, 116, 144) : RGB(2, 132, 199);
                HBRUSH hBtnBrush = CreateSolidBrush(clrBtn);
                FillRect(pDIS->hDC, &pDIS->rcItem, hBtnBrush);
                DeleteObject(hBtnBrush);

                SetBkMode(pDIS->hDC, TRANSPARENT);
                SetTextColor(pDIS->hDC, RGB(255, 255, 255));
                SelectObject(pDIS->hDC, g_hFontBold);
                DrawTextW(pDIS->hDC, L"Gửi ➤", -1, &pDIS->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                return TRUE;
            }

            case WM_COMMAND: {
                int id = LOWORD(wp);
                int code = HIWORD(wp);
                if (id == IDC_CLIENT_CHAT_SEND && code == BN_CLICKED) {
                    SendCurrentMessage();
                    return 0;
                }
                break;
            }

            case WM_CHAT_CLIENT_SHOW: {
                ShowWindow(hwnd, SW_SHOW);
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
                if (g_hInputEdit) SetFocus(g_hInputEdit);
                return 0;
            }

            case WM_CHAT_CLIENT_HIDE: {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }

            case WM_CHAT_CLIENT_NEW_MSG: {
                EnterCriticalSection(&g_cs);
                for (const auto& line : g_pendingMessages) {
                    AppendMessageToHistory(line);
                }
                g_pendingMessages.clear();
                LeaveCriticalSection(&g_cs);

                ShowWindow(hwnd, SW_SHOW);
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
                FlashWindow(hwnd, TRUE);
                MessageBeep(MB_ICONASTERISK);
                return 0;
            }

            case WM_CHAT_CLIENT_CLOSE: {
                DestroyWindow(hwnd);
                return 0;
            }

            case WM_SETFOCUS:
                if (g_hInputEdit) SetFocus(g_hInputEdit);
                return 0;

            case WM_CLOSE:
                ShowWindow(hwnd, SW_HIDE);
                return 0;

            case WM_DESTROY:
                g_hClientChatWnd = NULL;
                g_hHistoryEdit = NULL;
                g_hInputEdit = NULL;
                g_hBtnSend = NULL;
                if (g_hFontNormal) { DeleteObject(g_hFontNormal); g_hFontNormal = NULL; }
                if (g_hFontBold) { DeleteObject(g_hFontBold); g_hFontBold = NULL; }
                if (g_hBgBrush) { DeleteObject(g_hBgBrush); g_hBgBrush = NULL; }
                PostQuitMessage(0);
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    void ChatUIThreadProc() {
        g_uiThreadId = GetCurrentThreadId();

        WNDCLASSW wc = {};
        wc.lpfnWndProc = ClientChatWndProc;
        wc.lpszClassName = L"ClientChatClassW";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassW(&wc);

        float scale = 1.0f;
        HDC hdc = GetDC(NULL);
        if (hdc) {
            scale = (float)GetDeviceCaps(hdc, LOGPIXELSY) / 96.0f;
            ReleaseDC(NULL, hdc);
        }

        int w = (int)std::round(380.0f * scale);
        int h = (int)std::round(460.0f * scale);

        RECT workArea = {};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
        int posX = workArea.right - w - (int)(16 * scale);
        int posY = workArea.bottom - h - (int)(16 * scale);
        if (posX < 20) posX = 20;
        if (posY < 20) posY = 20;

        g_hClientChatWnd = CreateWindowExW(WS_EX_TOPMOST, L"ClientChatClassW", L"Trò chuyện với Quản lý",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
            posX, posY, w, h, NULL, NULL, GetModuleHandle(NULL), NULL);

        g_isWindowReady = true;

        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        g_isWindowReady = false;
        g_hClientChatWnd = NULL;
        g_uiThreadId = 0;
    }
}

namespace ClientChatWindow {

    void Initialize(TcpServer* controlServer) {
        if (!g_csInit) {
            InitializeCriticalSection(&g_cs);
            g_csInit = true;
        }

        g_controlServer = controlServer;

        if (!g_isThreadRunning.load()) {
            g_isThreadRunning = true;
            g_isWindowReady = false;
            g_uiThread = std::thread(ChatUIThreadProc);

            // Chờ cửa sổ khởi tạo xong (tối đa 500ms)
            for (int i = 0; i < 50 && !g_isWindowReady.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    void Show() {
        if (g_hClientChatWnd && IsWindow(g_hClientChatWnd)) {
            PostMessageW(g_hClientChatWnd, WM_CHAT_CLIENT_SHOW, 0, 0);
        }
    }

    void Hide() {
        if (g_hClientChatWnd && IsWindow(g_hClientChatWnd)) {
            PostMessageW(g_hClientChatWnd, WM_CHAT_CLIENT_HIDE, 0, 0);
        }
    }

    void Toggle() {
        if (IsOpen()) {
            Hide();
        } else {
            Show();
        }
    }

    bool IsOpen() {
        return (g_hClientChatWnd && IsWindow(g_hClientChatWnd) && IsWindowVisible(g_hClientChatWnd));
    }

    void OnMessageReceived(const Protocol::ChatMessagePayload& msg) {
        if (!g_csInit) {
            InitializeCriticalSection(&g_cs);
            g_csInit = true;
        }

        std::wstring timeStr = GetCurrentTimeString();
        std::string senderStr = (strlen(msg.sender) > 0) ? msg.sender : "Quản lý";
        std::wstring senderW = Utils::Utf8ToWide(senderStr);
        std::wstring contentW = Utils::Utf8ToWide(msg.message);

        std::wstring formattedLine = timeStr + L" " + senderW + L": " + contentW;

        EnterCriticalSection(&g_cs);
        g_pendingMessages.push_back(formattedLine);
        LeaveCriticalSection(&g_cs);

        if (g_hClientChatWnd && IsWindow(g_hClientChatWnd)) {
            PostMessageW(g_hClientChatWnd, WM_CHAT_CLIENT_NEW_MSG, 0, 0);
        }
    }

    void Close() {
        if (g_hClientChatWnd && IsWindow(g_hClientChatWnd)) {
            PostMessageW(g_hClientChatWnd, WM_CHAT_CLIENT_CLOSE, 0, 0);
        }
        if (g_isThreadRunning.load()) {
            g_isThreadRunning = false;
            if (g_uiThreadId != 0) {
                PostThreadMessageW(g_uiThreadId, WM_QUIT, 0, 0);
            }
            if (g_uiThread.joinable()) {
                g_uiThread.join();
            }
        }
        g_uiThreadId = 0;
        EnterCriticalSection(&g_cs);
        g_pendingMessages.clear();
        LeaveCriticalSection(&g_cs);
        g_controlServer = nullptr;
    }
}
