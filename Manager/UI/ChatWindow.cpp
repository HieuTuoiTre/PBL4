#include "ChatWindow.h"
#include "../../Shared/Utils.h"
#include <gdiplus.h>
#include <commctrl.h>
#include <vector>
#include <string>

#define WM_CHAT_NEW_MESSAGE (WM_APP + 201)
#define IDC_CHAT_HISTORY    2001
#define IDC_CHAT_INPUT      2002
#define IDC_CHAT_SEND       2003

namespace {
    HWND g_hChatWnd = NULL;
    HWND g_hParentWnd = NULL;
    HWND g_hHistoryEdit = NULL;
    HWND g_hInputEdit = NULL;
    HWND g_hBtnSend = NULL;
    WNDPROC g_oldEditProc = NULL;
    HFONT g_hFontNormal = NULL;
    HFONT g_hFontBold = NULL;
    HBRUSH g_hBgBrush = NULL;

    TcpClient* g_client = nullptr;
    CRITICAL_SECTION g_cs;
    bool g_csInit = false;
    int g_unreadCount = 0;
    std::vector<std::wstring> g_pendingMessages;

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

    LRESULT CALLBACK InputEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if ((uMsg == WM_KEYDOWN || uMsg == WM_CHAR) && wParam == VK_RETURN) {
            if (uMsg == WM_KEYDOWN) {
                // Nhấn phím Enter trong ô nhập -> Tự động bấm gửi tin nhắn
                SendMessageW(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(IDC_CHAT_SEND, BN_CLICKED), (LPARAM)hWnd);
            }
            return 0; // Chặn cả KEYDOWN lẫn CHAR để không phát tiếng bíp của Windows
        }
        return CallWindowProcW(g_oldEditProc, hWnd, uMsg, wParam, lParam);
    }

    void SendCurrentMessage() {
        if (!g_hInputEdit || !g_client) return;

        wchar_t buf[1024] = {0};
        GetWindowTextW(g_hInputEdit, buf, 1024);
        std::wstring wmsg = buf;

        // Xóa khoảng trắng thừa đầu và cuối
        while (!wmsg.empty() && (wmsg.back() == L' ' || wmsg.back() == L'\r' || wmsg.back() == L'\n' || wmsg.back() == L'\t')) {
            wmsg.pop_back();
        }
        size_t first = wmsg.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos) return;
        wmsg = wmsg.substr(first);

        std::string utf8Msg = Utils::WideToUtf8(wmsg);
        if (utf8Msg.empty()) return;

        Protocol::ChatMessagePayload payload = {0};
        strncpy_s(payload.sender, "Quản lý", sizeof(payload.sender) - 1);
        strncpy_s(payload.message, utf8Msg.c_str(), sizeof(payload.message) - 1);
        payload.timestamp = GetTickCount64();

        if (g_client->SendPacket(Protocol::MSG_CHAT_MESSAGE, (const char*)&payload, sizeof(payload))) {
            std::wstring timeStr = GetCurrentTimeString();
            AppendMessageToHistory(timeStr + L" Bạn: " + wmsg);
            SetWindowTextW(g_hInputEdit, L"");
            SetFocus(g_hInputEdit);
        }
    }

    LRESULT CALLBACK ChatWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        float scale = GetDpiScale(hwnd);
        int headerH = (int)std::round(48.0f * scale);
        int inputH = (int)std::round(34.0f * scale);
        int pad = (int)std::round(12.0f * scale);
        int btnW = (int)std::round(76.0f * scale);

        switch (msg) {
            case WM_CREATE: {
                g_hHistoryEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_CHAT_HISTORY, GetModuleHandle(NULL), NULL);

                g_hInputEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_CHAT_INPUT, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hInputEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Nhập tin nhắn... (Nhấn Enter để gửi)");
                g_oldEditProc = (WNDPROC)SetWindowLongPtrW(g_hInputEdit, GWLP_WNDPROC, (LONG_PTR)InputEditSubclassProc);

                g_hBtnSend = CreateWindowExW(0, L"BUTTON", L"Gửi ➤",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_CHAT_SEND, GetModuleHandle(NULL), NULL);

                g_hFontNormal = CreateFontW((int)(-13 * scale), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
                g_hFontBold = CreateFontW((int)(-13 * scale), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
                SendMessageW(g_hHistoryEdit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SendMessageW(g_hInputEdit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

                // Nạp toàn bộ tin nhắn đang chờ vào lịch sử
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

                // 1. Vẽ Header phong cách Slate/Dark hiện đại
                Gdiplus::Graphics g(hdc);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

                Gdiplus::SolidBrush headerBg(Gdiplus::Color(255, 30, 41, 59));
                g.FillRectangle(&headerBg, 0, 0, rc.right, headerH);

                Gdiplus::FontFamily ff(L"Segoe UI");
                Gdiplus::Font fontTitle(&ff, 12.0f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                Gdiplus::Font fontSub(&ff, 9.5f * scale, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

                Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 255, 255, 255));
                Gdiplus::SolidBrush subBrush(Gdiplus::Color(255, 148, 163, 184));

                g.DrawString(L"💬 Trò chuyện nội bộ (In-Session Chat)", -1, &fontTitle, Gdiplus::PointF(14.0f * scale, 9.0f * scale), &titleBrush);
                g.DrawString(L"Kết nối trực tiếp hai chiều với máy khách qua LAN", -1, &fontSub, Gdiplus::PointF(14.0f * scale, 27.0f * scale), &subBrush);

                // 2. Nền phần dưới
                RECT bottomRc = { 0, headerH, rc.right, rc.bottom };
                if (!g_hBgBrush) g_hBgBrush = CreateSolidBrush(RGB(248, 250, 252));
                FillRect(hdc, &bottomRc, g_hBgBrush);

                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_DRAWITEM: {
                LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lp;
                if (!pDIS || pDIS->CtlID != IDC_CHAT_SEND) break;

                bool isPressed = (pDIS->itemState & ODS_SELECTED);
                Gdiplus::Graphics g(pDIS->hDC);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

                Gdiplus::Color fill = isPressed ? Gdiplus::Color(255, 29, 78, 216) : Gdiplus::Color(255, 37, 99, 235);
                Gdiplus::SolidBrush fillBrush(fill);
                Gdiplus::RectF r((float)pDIS->rcItem.left, (float)pDIS->rcItem.top, (float)(pDIS->rcItem.right - pDIS->rcItem.left), (float)(pDIS->rcItem.bottom - pDIS->rcItem.top));

                g.FillRectangle(&fillBrush, r);

                Gdiplus::FontFamily ff(L"Segoe UI");
                Gdiplus::Font font(&ff, 12.0f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
                Gdiplus::StringFormat sf;
                sf.SetAlignment(Gdiplus::StringAlignmentCenter);
                sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

                g.DrawString(L"Gửi ➤", -1, &font, r, &sf, &textBrush);
                return TRUE;
            }

            case WM_COMMAND: {
                int id = LOWORD(wp);
                int code = HIWORD(wp);
                if (id == IDC_CHAT_SEND && code == BN_CLICKED) {
                    SendCurrentMessage();
                    return 0;
                }
                break;
            }

            case WM_CHAT_NEW_MESSAGE: {
                EnterCriticalSection(&g_cs);
                for (const auto& line : g_pendingMessages) {
                    AppendMessageToHistory(line);
                }
                g_pendingMessages.clear();
                LeaveCriticalSection(&g_cs);
                return 0;
            }

            case WM_SETFOCUS:
                if (g_hInputEdit) SetFocus(g_hInputEdit);
                return 0;

            case WM_CLOSE:
                ShowWindow(hwnd, SW_HIDE);
                return 0;

            case WM_DESTROY:
                g_hChatWnd = NULL;
                g_hHistoryEdit = NULL;
                g_hInputEdit = NULL;
                g_hBtnSend = NULL;
                if (g_hFontNormal) { DeleteObject(g_hFontNormal); g_hFontNormal = NULL; }
                if (g_hFontBold) { DeleteObject(g_hFontBold); g_hFontBold = NULL; }
                if (g_hBgBrush) { DeleteObject(g_hBgBrush); g_hBgBrush = NULL; }
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

namespace ChatWindow {

    void Show(HWND hParent, TcpClient* client) {
        if (!g_csInit) {
            InitializeCriticalSection(&g_cs);
            g_csInit = true;
        }

        g_hParentWnd = hParent;
        g_client = client;
        g_unreadCount = 0;

        if (g_hParentWnd) InvalidateRect(g_hParentWnd, NULL, FALSE);

        if (!g_hChatWnd || !IsWindow(g_hChatWnd)) {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = ChatWndProc;
            wc.lpszClassName = L"ManagerChatClassW";
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassW(&wc);

            float scale = 1.0f;
            HDC hdc = GetDC(NULL);
            if (hdc) {
                scale = (float)GetDeviceCaps(hdc, LOGPIXELSY) / 96.0f;
                ReleaseDC(NULL, hdc);
            }

            int w = (int)std::round(390.0f * scale);
            int h = (int)std::round(480.0f * scale);

            int posX = 100, posY = 100;
            if (hParent && IsWindow(hParent)) {
                RECT pr;
                GetWindowRect(hParent, &pr);
                posX = pr.right - w - (int)(24 * scale);
                posY = pr.bottom - h - (int)(48 * scale);
            }
            if (posX < 20) posX = 20;
            if (posY < 20) posY = 20;

            g_hChatWnd = CreateWindowExW(WS_EX_TOPMOST, L"ManagerChatClassW", L"Trò chuyện nội bộ",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VISIBLE,
                posX, posY, w, h, hParent, NULL, GetModuleHandle(NULL), NULL);
        } else {
            ShowWindow(g_hChatWnd, SW_SHOW);
            SetForegroundWindow(g_hChatWnd);
            if (g_hInputEdit) SetFocus(g_hInputEdit);
        }
    }

    void Hide() {
        if (g_hChatWnd && IsWindow(g_hChatWnd)) {
            ShowWindow(g_hChatWnd, SW_HIDE);
        }
    }

    void Toggle(HWND hParent, TcpClient* client) {
        if (IsOpen()) {
            Hide();
        } else {
            Show(hParent, client);
        }
    }

    bool IsOpen() {
        return (g_hChatWnd && IsWindow(g_hChatWnd) && IsWindowVisible(g_hChatWnd));
    }

    int GetUnreadCount() {
        return g_unreadCount;
    }

    void ResetUnreadCount() {
        g_unreadCount = 0;
        if (g_hParentWnd) InvalidateRect(g_hParentWnd, NULL, FALSE);
    }

    void OnMessageReceived(const Protocol::ChatMessagePayload& msg) {
        if (!g_csInit) {
            InitializeCriticalSection(&g_cs);
            g_csInit = true;
        }

        std::wstring timeStr = GetCurrentTimeString();
        std::string senderStr = (strlen(msg.sender) > 0) ? msg.sender : "Máy khách";
        std::wstring senderW = Utils::Utf8ToWide(senderStr);
        std::wstring contentW = Utils::Utf8ToWide(msg.message);

        std::wstring formattedLine = timeStr + L" " + senderW + L": " + contentW;

        EnterCriticalSection(&g_cs);
        g_pendingMessages.push_back(formattedLine);
        LeaveCriticalSection(&g_cs);

        if (g_hChatWnd && IsWindow(g_hChatWnd)) {
            PostMessageW(g_hChatWnd, WM_CHAT_NEW_MESSAGE, 0, 0);
        }

        if (!IsOpen() || GetForegroundWindow() != g_hChatWnd) {
            g_unreadCount++;
            MessageBeep(MB_ICONASTERISK);
            if (g_hParentWnd) InvalidateRect(g_hParentWnd, NULL, FALSE);
        }
    }

    void Cleanup() {
        if (g_hChatWnd && IsWindow(g_hChatWnd)) {
            DestroyWindow(g_hChatWnd);
            g_hChatWnd = NULL;
        }
        g_pendingMessages.clear();
        g_unreadCount = 0;
    }
}

