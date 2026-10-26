// ============================================================================
// File: ConnectWindow.cpp
// Mô tả: Hiện thực chi tiết giao diện bảng điều khiển kết nối mạng LAN (ConnectWindow).
// Phong cách: Windows 11 Fluent Light Theme với Card container bo góc,
//            Hàng ListView thoáng đạt có icon máy tính vector, Badge trạng thái dạng pill,
//            Nút bấm bo góc hiện đại và hiển thị trạng thái mượt mà.
// ============================================================================

#include "ConnectWindow.h"
#include "../Core/DiscoveryScanner.h"
#include "../../Shared/Utils.h"

#include <commctrl.h>
#include <objidl.h> // Bắt buộc nạp trước gdiplus.h khi dùng MinGW/GCC
#include <gdiplus.h>

#include <thread>
#include <vector>
#include <string>
#include <algorithm>
#include <cwchar>

// Định nghĩa mã thông điệp tùy biến
#define WM_CONNECT_RESULT (WM_APP + 201)
#define WM_CLIENT_UPDATE  (WM_APP + 202)
#define TIMER_SCAN_ID     1001

namespace {
    // ========================================================================
    // CẤU TRÚC CLIENT LƯU TRỮ TRONG UI
    // ========================================================================
    struct ClientDisplayItem {
        std::string ip;
        std::wstring computerName;
        std::wstring userName;
        bool isBusy = false;
        bool isOnline = true;
        DWORD lastSeenTick = 0;
    };

    // ========================================================================
    // CÁC HANDLE ĐIỀU KHIỂN GIAO DIỆN CỤC BỘ (GUI HANDLES)
    // ========================================================================
    HWND g_hConnectWnd = NULL;
    HWND g_hListClients = NULL;
    HWND g_hEditIp = NULL;
    HWND g_hBtnConnect = NULL;
    HWND g_hBtnLocalhost = NULL;
    HWND g_hBtnScan = NULL;
    HWND g_hBtnExit = NULL;

    HWND g_hHeader = NULL;         // Handle thanh Header của ListView
    WNDPROC g_oldHeaderProc = NULL;// Con trỏ lưu hàm WndProc gốc của Header
    HWND g_hHoveredBtn = NULL;     // Handle nút bấm đang được chuột rê qua (Hover)
    bool g_editHasFocus = false;   // Trạng thái focus của ô nhập IP để vẽ viền xanh sáng
    bool g_hasAutoFilledIp = false;       // Đã tự động điền IP của Client lần đầu tiên hay chưa
    bool g_userManuallyEditedIp = false;  // Người dùng đã tự gõ hoặc sửa IP trong ô Edit hay chưa
    HIMAGELIST g_hImageList = NULL;// ImageList giả lập để tăng chiều cao dòng ListView

    // ========================================================================
    // TÀI NGUYÊN ĐỒ HỌA GDI & GDI+
    // ========================================================================
    HFONT g_hFontTitle = NULL;     // Segoe UI 20px, Bold
    HFONT g_hFontSub = NULL;       // Segoe UI 11px, Bold
    HFONT g_hFontNormal = NULL;    // Segoe UI 12px, Regular
    HFONT g_hFontHeader = NULL;    // Segoe UI 12px, Semi-bold (Header ListView)
    HFONT g_hFontCardTitle = NULL; // Segoe UI 13px, Bold
    HFONT g_hFontEdit = NULL;      // Segoe UI 13px, Regular

    HBRUSH g_hBgBrush = NULL;      // Nền chính #F1F5F9 (RGB 241, 245, 249)
    HBRUSH g_hWhiteBrush = NULL;   // Nền Card trắng #FFFFFF

    // ========================================================================
    // BIẾN QUẢN LÝ DỮ LIỆU VÀ TRẠNG THÁI
    // ========================================================================
    WNDPROC g_oldEditProc = NULL;
    ConnectWindow::ConnectCallback g_callback = nullptr;

    bool g_isOpen = false;
    bool g_success = false;
    bool g_isConnecting = false;
    std::string g_targetIP = "";
    std::string g_connectedIP = "";
    std::wstring g_statusText = L"Đang tự động quét mạng LAN...";
    int g_statusColorType = 0; // 0: Bình thường (Slate), 1: Thông tin (Blue), 2: Lỗi (Red)

    std::vector<ClientDisplayItem> g_discoveredList;

    // Định danh ID của các control
    enum ControlId {
        IDC_LIST_CLIENTS  = 100,
        IDC_EDIT_IP       = 101,
        IDC_BTN_CONNECT   = 102,
        IDC_BTN_LOCALHOST = 103,
        IDC_BTN_SCAN      = 104,
        IDC_BTN_EXIT      = 105
    };

    // ========================================================================
    // HÀM TIỆN ÍCH ĐỒ HỌA GDI+
    // ========================================================================
    void AddRoundRectPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
        float d = radius * 2.0f;
        if (d > rect.Width) d = rect.Width;
        if (d > rect.Height) d = rect.Height;
        path.Reset();
        path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
        path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
        path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
        path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
        path.CloseFigure();
    }

    // ========================================================================
    // HÀM ĐÁNH GIÁ MỨC ĐỘ ƯU TIÊN CỦA ĐỊA CHỈ IP
    // ========================================================================
    int GetIpPriority(const std::string& ip) {
        if (ip.rfind("192.168.", 0) == 0 && ip.rfind("192.168.56.", 0) != 0) return 4;
        if (ip.rfind("10.", 0) == 0) return 4;
        if (ip.rfind("172.", 0) == 0) return 3;
        if (ip != "127.0.0.1" && !ip.empty()) return 2;
        if (ip == "127.0.0.1") return 1;
        return 0;
    }

    // ========================================================================
    // CẬP NHẬT TRẠNG THÁI VÀ VẼ LẠI DẢI THÔNG BÁO
    // ========================================================================
    void SetStatus(const std::wstring& text, int colorType) {
        g_statusText = text;
        g_statusColorType = colorType;
        if (g_hConnectWnd) {
            // Vùng thông báo nằm ở góc trên bên phải phía dưới nút Quét mạng
            RECT rc = { 460, 60, 740, 86 };
            InvalidateRect(g_hConnectWnd, &rc, TRUE);
        }
    }

    // ========================================================================
    // SUBCLASS TÙY BIẾN THANH TIÊU ĐỀ CỘT LISTVIEW (HEADER CONTROL)
    // Loại bỏ viền 3D màu xám cổ điển, vẽ nền trắng phẳng và font Segoe UI hiện đại
    // ========================================================================
    LRESULT CALLBACK HeaderSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == WM_ERASEBKGND) {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(hdc, &rc, g_hWhiteBrush);
            return 1;
        }

        if (uMsg == WM_PAINT) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT rc;
            GetClientRect(hWnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

            {
                Gdiplus::Graphics g(memDC);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

                // Nền trắng tinh khiết ăn khớp với Card 1
                Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
                g.FillRectangle(&whiteBrush, 0, 0, w, h);

                // Đường viền dưới tinh tế #E2E8F0 ngăn cách header với hàng nội dung
                Gdiplus::Pen botPen(Gdiplus::Color(255, 226, 232, 240), 1.0f);
                g.DrawLine(&botPen, 0.0f, (float)h - 1.0f, (float)w, (float)h - 1.0f);

                // Đường phân cách mờ giữa các cột
                Gdiplus::Pen sepPen(Gdiplus::Color(255, 241, 245, 249), 1.0f);

                // Font Segoe UI cho Header: thanh mảnh, hiện đại, màu Slate 700 (#334155)
                Gdiplus::FontFamily ff(L"Segoe UI");
                Gdiplus::Font fontHeader(&ff, 12.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 51, 65, 85));

                Gdiplus::StringFormat sf;
                sf.SetAlignment(Gdiplus::StringAlignmentNear);
                sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoClip);

                int count = Header_GetItemCount(hWnd);
                for (int i = 0; i < count; i++) {
                    RECT itemRc;
                    Header_GetItemRect(hWnd, i, &itemRc);

                    wchar_t text[128] = {0};
                    HDITEMW hdi = {};
                    hdi.mask = HDI_TEXT;
                    hdi.pszText = text;
                    hdi.cchTextMax = 127;
                    Header_GetItem(hWnd, i, &hdi);

                    // Vẽ tiêu đề cột canh lề trái với khoảng đệm 8px
                    Gdiplus::RectF textRc((float)itemRc.left + 8.0f, (float)itemRc.top,
                                         (float)(itemRc.right - itemRc.left - 12), (float)(itemRc.bottom - itemRc.top));
                    g.DrawString(text, -1, &fontHeader, textRc, &sf, &textBrush);

                    // Vẽ đường phân cách dọc mỏng giữa các cột
                    if (i < count - 1) {
                        g.DrawLine(&sepPen, (float)itemRc.right - 1.0f, (float)itemRc.top + 4.0f,
                                            (float)itemRc.right - 1.0f, (float)itemRc.bottom - 5.0f);
                    }
                }
            }

            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);

            EndPaint(hWnd, &ps);
            return 0;
        }

        return CallWindowProcW(g_oldHeaderProc, hWnd, uMsg, wParam, lParam);
    }

    // ========================================================================
    // SUBCLASS CHO Ô EDIT NHẬP ĐỊA CHỈ IP
    // ========================================================================
    LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == WM_CHAR || uMsg == WM_KEYDOWN) {
            if (wParam != VK_RETURN && wParam != VK_ESCAPE && wParam != VK_TAB) {
                g_userManuallyEditedIp = true; // Người dùng trực tiếp gõ vào ô Edit
            }
        }
        if (uMsg == WM_KEYDOWN) {
            if (wParam == VK_RETURN) {
                SendMessageW(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(IDC_BTN_CONNECT, BN_CLICKED), 0);
                return 0;
            } else if (wParam == VK_ESCAPE) {
                SendMessageW(GetParent(hWnd), WM_CLOSE, 0, 0);
                return 0;
            }
        } else if (uMsg == WM_SETFOCUS) {
            g_editHasFocus = true;
            RECT rc = { 40, 392, 260, 434 };
            InvalidateRect(GetParent(hWnd), &rc, FALSE);
        } else if (uMsg == WM_KILLFOCUS) {
            g_editHasFocus = false;
            RECT rc = { 40, 392, 260, 434 };
            InvalidateRect(GetParent(hWnd), &rc, FALSE);
        }
        return CallWindowProcW(g_oldEditProc, hWnd, uMsg, wParam, lParam);
    }

    // ========================================================================
    // CẬP NHẬT BẢNG DANH SÁCH MÁY CLIENT (REFRESH LISTVIEW UI)
    // ========================================================================
    void RefreshListViewUI() {
        if (!g_hListClients) return;

        std::wstring selectedComputer = L"";
        int curSel = ListView_GetNextItem(g_hListClients, -1, LVNI_SELECTED);
        if (curSel >= 0 && curSel < (int)g_discoveredList.size()) {
            selectedComputer = g_discoveredList[curSel].computerName;
        }

        SendMessageW(g_hListClients, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(g_hListClients);

        int newSelIdx = -1;
        for (int i = 0; i < (int)g_discoveredList.size(); i++) {
            const auto& client = g_discoveredList[i];

            // Cột 0: Tên máy tính
            LVITEMW lvi = {};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = i;
            lvi.iSubItem = 0;
            lvi.pszText = (LPWSTR)client.computerName.c_str();
            ListView_InsertItem(g_hListClients, &lvi);

            // Cột 1: Địa chỉ IP
            std::wstring ipW = Utils::Utf8ToWide(client.ip);
            ListView_SetItemText(g_hListClients, i, 1, (LPWSTR)ipW.c_str());

            // Cột 2: Tên tài khoản Windows
            ListView_SetItemText(g_hListClients, i, 2, (LPWSTR)client.userName.c_str());

            // Cột 3: Trạng thái (Đang hoạt động / Đang bận / Ngoại tuyến)
            std::wstring statusStr = L"Ngoại tuyến";
            if (client.isOnline) {
                statusStr = client.isBusy ? L"Đang bận" : L"Đang hoạt động";
            }
            ListView_SetItemText(g_hListClients, i, 3, (LPWSTR)statusStr.c_str());

            if (!selectedComputer.empty() && _wcsicmp(client.computerName.c_str(), selectedComputer.c_str()) == 0) {
                newSelIdx = i;
            }
        }

        if (newSelIdx != -1) {
            ListView_SetItemState(g_hListClients, newSelIdx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        } else if (!g_discoveredList.empty() && curSel == -1) {
            // Mặc định chọn dòng đầu tiên nếu chưa chọn dòng nào
            ListView_SetItemState(g_hListClients, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }

        SendMessageW(g_hListClients, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(g_hListClients, NULL, TRUE);

        if (!g_isConnecting) {
            if (g_discoveredList.empty()) {
                SetStatus(L"🔄 Đang tự động quét mạng LAN...", 0);
            } else {
                std::wstring msg = L"✓ Đã phát hiện " + std::to_wstring(g_discoveredList.size()) + L" máy trong mạng LAN";
                SetStatus(msg, 0);
            }
        }
    }

    // ========================================================================
    // TIẾN TRÌNH KHỞI TẠO KẾT NỐI (START CONNECTION PROCESS)
    // ========================================================================
    void StartConnectionProcess(HWND hwnd) {
        if (g_isConnecting) return;

        wchar_t buf[256] = {0};
        GetWindowTextW(g_hEditIp, buf, 255);

        std::wstring ws = buf;
        size_t start = ws.find_first_not_of(L" \t\r\n");
        size_t end = ws.find_last_not_of(L" \t\r\n");
        if (start == std::wstring::npos) {
            SetStatus(L"⚠ Vui lòng chọn 1 máy hoặc nhập địa chỉ IP!", 2);
            SetFocus(g_hEditIp);
            return;
        }
        std::wstring trimmed = ws.substr(start, end - start + 1);
        std::string ipStr = Utils::WideToUtf8(trimmed);

        g_targetIP = ipStr;
        g_isConnecting = true;

        EnableWindow(g_hBtnConnect, FALSE);
        EnableWindow(g_hBtnLocalhost, FALSE);
        EnableWindow(g_hBtnScan, FALSE);
        EnableWindow(g_hEditIp, FALSE);
        EnableWindow(g_hListClients, FALSE);

        SetStatus(L"⏳ Đang kết nối tới " + trimmed + L"...", 1);

        std::thread([hwnd, ipStr]() {
            std::string err;
            bool ok = false;
            if (g_callback) {
                ok = g_callback(ipStr, err);
            }
            std::string* pErr = new std::string(err);
            PostMessageW(hwnd, WM_CONNECT_RESULT, ok ? 1 : 0, (LPARAM)pErr);
        }).detach();
    }

    // Xử lý khi người dùng chọn dòng trong ListView
    void HandleSelectedRow(int index, bool triggerConnect) {
        if (index < 0 || index >= (int)g_discoveredList.size()) return;

        std::wstring ipW = Utils::Utf8ToWide(g_discoveredList[index].ip);
        SetWindowTextW(g_hEditIp, ipW.c_str());
        SendMessageW(g_hEditIp, EM_SETSEL, 0, -1);
        g_userManuallyEditedIp = false; // Người dùng vừa chủ động chọn máy trong danh sách

        if (triggerConnect) {
            StartConnectionProcess(g_hConnectWnd);
        }
    }

    // ========================================================================
    // VẼ VECTOR BIỂU TƯỢNG SƠ ĐỒ MẠNG LAN TRÊN BANNER
    // ========================================================================
    void DrawNetworkBannerIcon(Gdiplus::Graphics& g, float x, float y) {
        // Màn hình chính ở đỉnh sơ đồ
        Gdiplus::RectF hostScr(x + 12.0f, y, 22.0f, 15.0f);
        Gdiplus::GraphicsPath hostPath;
        AddRoundRectPath(hostPath, hostScr, 2.0f);
        Gdiplus::SolidBrush blueFill(Gdiplus::Color(255, 37, 99, 235)); // #2563EB
        Gdiplus::Pen bluePen(Gdiplus::Color(255, 29, 78, 216), 1.2f);
        g.FillPath(&blueFill, &hostPath);
        g.DrawPath(&bluePen, &hostPath);

        // Chi tiết màn hình bên trong
        Gdiplus::SolidBrush glowFill(Gdiplus::Color(255, 147, 197, 253)); // #93C5FD
        Gdiplus::RectF innerScr(x + 15.0f, y + 3.0f, 16.0f, 7.0f);
        g.FillRectangle(&glowFill, innerScr);

        // Chân đế màn hình
        Gdiplus::Pen slatePen(Gdiplus::Color(255, 71, 85, 105), 1.5f); // #475569
        g.DrawLine(&slatePen, x + 23.0f, y + 15.0f, x + 23.0f, y + 19.0f);
        g.DrawLine(&slatePen, x + 18.0f, y + 19.0f, x + 28.0f, y + 19.0f);

        // Dây mạng nối xuống đường trục Bus
        Gdiplus::Pen busPen(Gdiplus::Color(255, 148, 163, 184), 1.5f); // #94A3B8
        g.DrawLine(&busPen, x + 23.0f, y + 19.0f, x + 23.0f, y + 25.0f);

        // Trục cáp ngang Bus
        g.DrawLine(&busPen, x + 6.0f, y + 25.0f, x + 40.0f, y + 25.0f);

        // 3 nhánh rẽ xuống 3 máy trạm Client
        float drops[3] = { x + 8.0f, x + 23.0f, x + 38.0f };
        for (int i = 0; i < 3; i++) {
            g.DrawLine(&busPen, drops[i], y + 25.0f, drops[i], y + 30.0f);
            Gdiplus::RectF nodeRect(drops[i] - 5.0f, y + 30.0f, 10.0f, 8.0f);
            Gdiplus::SolidBrush nodeFill(Gdiplus::Color(255, 59, 130, 246));
            Gdiplus::Pen nodePen(Gdiplus::Color(255, 71, 85, 105), 1.0f);
            g.FillRectangle(&nodeFill, nodeRect);
            g.DrawRectangle(&nodePen, nodeRect);
            g.DrawLine(&nodePen, drops[i] - 3.0f, y + 38.0f, drops[i] + 3.0f, y + 38.0f);
        }
    }

    // ========================================================================
    // VẼ VECTOR BIỂU TƯỢNG MÁY TÍNH DESKTOP TRONG DÒNG LISTVIEW
    // ========================================================================
    void DrawMonitorRowIcon(Gdiplus::Graphics& g, float x, float y) {
        Gdiplus::RectF scr(x, y, 18.0f, 13.0f);
        Gdiplus::GraphicsPath scrPath;
        AddRoundRectPath(scrPath, scr, 1.5f);
        Gdiplus::Pen pen(Gdiplus::Color(255, 51, 65, 85), 1.4f); // #334155
        Gdiplus::SolidBrush scrBg(Gdiplus::Color(255, 255, 255, 255));
        g.FillPath(&scrBg, &scrPath);
        g.DrawPath(&pen, &scrPath);

        // Đường chân đế
        g.DrawLine(&pen, x + 9.0f, y + 13.0f, x + 9.0f, y + 16.0f);
        g.DrawLine(&pen, x + 4.5f, y + 16.0f, x + 13.5f, y + 16.0f);
    }

    // ========================================================================
    // VẼ BADGE TRẠNG THÁI DẠNG PILL (ĐANG HOẠT ĐỘNG, ĐANG BẬN, NGOẠI TUYẾN)
    // ========================================================================
    void DrawStatusBadgePill(Gdiplus::Graphics& g, const Gdiplus::RectF& cellRect, int statusType, const std::wstring& text) {
        float pillH = 26.0f;
        float pillY = cellRect.Y + (cellRect.Height - pillH) / 2.0f;
        float pillX = cellRect.X + 6.0f;

        Gdiplus::Color bgColor, borderColor, dotColor, textColor;
        float pillW = 126.0f;

        if (statusType == 0) { // Sẵn sàng / Đang hoạt động
            bgColor = Gdiplus::Color(255, 220, 252, 231);     // #DCFCE7
            borderColor = Gdiplus::Color(255, 187, 247, 208); // #BBF7D0
            dotColor = Gdiplus::Color(255, 34, 197, 94);      // #22C55E
            textColor = Gdiplus::Color(255, 21, 128, 61);     // #15803D
            pillW = 126.0f;
        } else if (statusType == 1) { // Đang bận
            bgColor = Gdiplus::Color(255, 254, 243, 199);     // #FEF3C7
            borderColor = Gdiplus::Color(255, 253, 230, 138); // #FDE68A
            dotColor = Gdiplus::Color(255, 245, 158, 11);     // #F59E0B
            textColor = Gdiplus::Color(255, 180, 83, 9);      // #B45309
            pillW = 98.0f;
        } else { // Ngoại tuyến
            bgColor = Gdiplus::Color(255, 241, 245, 249);     // #F1F5F9
            borderColor = Gdiplus::Color(255, 226, 232, 240); // #E2E8F0
            dotColor = Gdiplus::Color(255, 100, 116, 139);    // #64748B
            textColor = Gdiplus::Color(255, 71, 85, 105);     // #475569
            pillW = 108.0f;
        }

        Gdiplus::RectF pillRect(pillX, pillY, pillW, pillH);
        Gdiplus::GraphicsPath pillPath;
        AddRoundRectPath(pillPath, pillRect, 13.0f);

        Gdiplus::SolidBrush bgBrush(bgColor);
        Gdiplus::Pen borderPen(borderColor, 1.0f);
        g.FillPath(&bgBrush, &pillPath);
        g.DrawPath(&borderPen, &pillPath);

        // Chấm tròn trạng thái
        float dotSize = 7.0f;
        float dotX = pillX + 11.0f;
        float dotY = pillY + (pillH - dotSize) / 2.0f;
        Gdiplus::SolidBrush dotBrush(dotColor);
        g.FillEllipse(&dotBrush, dotX, dotY, dotSize, dotSize);

        // Chữ trạng thái
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font font(&ff, 12.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush textBrush(textColor);
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentNear);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

        Gdiplus::RectF textRect(dotX + dotSize + 6.0f, pillY, pillW - dotSize - 16.0f, pillH);
        g.DrawString(text.c_str(), -1, &font, textRect, &sf, &textBrush);
    }

    // ========================================================================
    // VẼ NÚT BẤM TÙY BIẾN (OWNER-DRAW BUTTONS)
    // ========================================================================
    void DrawModernButton(LPDRAWITEMSTRUCT dis) {
        Gdiplus::Graphics g(dis->hDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        RECT r = dis->rcItem;
        Gdiplus::RectF rf((float)r.left, (float)r.top, (float)(r.right - r.left), (float)(r.bottom - r.top));

        bool isSelected = (dis->itemState & ODS_SELECTED) != 0;
        bool isDisabled = (dis->itemState & ODS_DISABLED) != 0;
        bool isHovered = (dis->hwndItem == g_hHoveredBtn) && !isDisabled;

        Gdiplus::GraphicsPath path;
        float radius = 8.0f;
        if (dis->CtlID == IDC_BTN_LOCALHOST) radius = 6.0f;
        AddRoundRectPath(path, rf, radius);

        Gdiplus::Color bgColor, borderColor, textColor;

        if (dis->CtlID == IDC_BTN_CONNECT) {
            // Nút "Kết nối" (Royal Blue #2563EB)
            if (isDisabled) {
                bgColor = Gdiplus::Color(255, 147, 197, 253);
                borderColor = Gdiplus::Color(255, 191, 219, 254);
                textColor = Gdiplus::Color(255, 255, 255, 255);
            } else if (isSelected) {
                bgColor = Gdiplus::Color(255, 30, 58, 138); // #1E3A8A
                borderColor = Gdiplus::Color(255, 30, 58, 138);
                textColor = Gdiplus::Color(255, 255, 255, 255);
            } else if (isHovered) {
                bgColor = Gdiplus::Color(255, 29, 78, 216); // #1D4ED8
                borderColor = Gdiplus::Color(255, 29, 78, 216);
                textColor = Gdiplus::Color(255, 255, 255, 255);
            } else {
                bgColor = Gdiplus::Color(255, 37, 99, 235); // #2563EB
                borderColor = Gdiplus::Color(255, 37, 99, 235);
                textColor = Gdiplus::Color(255, 255, 255, 255);
            }
        } else if (dis->CtlID == IDC_BTN_SCAN) {
            // Nút "Quét mạng" (Deep Royal Blue / Navy #1E3A8A)
            if (isDisabled) {
                bgColor = Gdiplus::Color(255, 148, 163, 184);
                borderColor = Gdiplus::Color(255, 148, 163, 184);
                textColor = Gdiplus::Color(255, 255, 255, 255);
            } else if (isSelected) {
                bgColor = Gdiplus::Color(255, 23, 37, 84); // #172554
                borderColor = Gdiplus::Color(255, 23, 37, 84);
                textColor = Gdiplus::Color(255, 255, 255, 255);
            } else if (isHovered) {
                bgColor = Gdiplus::Color(255, 29, 78, 216); // #1D4ED8
                borderColor = Gdiplus::Color(255, 29, 78, 216);
                textColor = Gdiplus::Color(255, 255, 255, 255);
            } else {
                bgColor = Gdiplus::Color(255, 30, 58, 138); // #1E3A8A
                borderColor = Gdiplus::Color(255, 30, 58, 138);
                textColor = Gdiplus::Color(255, 255, 255, 255);
            }
        } else if (dis->CtlID == IDC_BTN_LOCALHOST) {
            // Nút "Localhost" (#E2E8F0)
            if (isSelected) {
                bgColor = Gdiplus::Color(255, 203, 213, 225); // #CBD5E1
                borderColor = Gdiplus::Color(255, 148, 163, 184);
                textColor = Gdiplus::Color(255, 15, 23, 42);
            } else if (isHovered) {
                bgColor = Gdiplus::Color(255, 226, 232, 240); // #E2E8F0
                borderColor = Gdiplus::Color(255, 203, 213, 225);
                textColor = Gdiplus::Color(255, 15, 23, 42);
            } else {
                bgColor = Gdiplus::Color(255, 241, 245, 249); // #F1F5F9
                borderColor = Gdiplus::Color(255, 203, 213, 225);
                textColor = Gdiplus::Color(255, 51, 65, 85);
            }
        } else {
            // Nút "Thoát" (#FFFFFF)
            if (isSelected) {
                bgColor = Gdiplus::Color(255, 226, 232, 240);
                borderColor = Gdiplus::Color(255, 148, 163, 184);
                textColor = Gdiplus::Color(255, 15, 23, 42);
            } else if (isHovered) {
                bgColor = Gdiplus::Color(255, 241, 245, 249);
                borderColor = Gdiplus::Color(255, 203, 213, 225);
                textColor = Gdiplus::Color(255, 15, 23, 42);
            } else {
                bgColor = Gdiplus::Color(255, 255, 255, 255);
                borderColor = Gdiplus::Color(255, 203, 213, 225);
                textColor = Gdiplus::Color(255, 51, 65, 85);
            }
        }

        // Đổ nền bo góc và viền
        Gdiplus::SolidBrush bgBrush(bgColor);
        Gdiplus::Pen borderPen(borderColor, 1.0f);
        g.FillPath(&bgBrush, &path);
        g.DrawPath(&borderPen, &path);

        // Lấy chuỗi văn bản của nút
        wchar_t text[128] = {0};
        GetWindowTextW(dis->hwndItem, text, 127);

        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font font(&ff, (dis->CtlID == IDC_BTN_CONNECT) ? 13.0f : 12.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush textBrush(textColor);
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

        // Nếu là nút "Quét mạng", vẽ icon làm mới / radar hai chiều xoay vòng đối xứng
        if (dis->CtlID == IDC_BTN_SCAN) {
            float icX = rf.X + 18.0f;
            float icY = rf.Y + rf.Height / 2.0f;
            float r = 5.5f;

            Gdiplus::Pen arcPen(textColor, 1.6f);
            arcPen.SetStartCap(Gdiplus::LineCapRound);
            arcPen.SetEndCap(Gdiplus::LineCapRound);

            // Cung tròn phía trên (quay theo chiều kim đồng hồ)
            g.DrawArc(&arcPen, icX - r, icY - r, r * 2.0f, r * 2.0f, 215.0f, 170.0f);
            // Đầu mũi tên của cung trên
            float ax1 = icX + r * 0.82f;
            float ay1 = icY - r * 0.57f;
            g.DrawLine(&arcPen, ax1, ay1, ax1 - 3.2f, ay1 - 2.5f);
            g.DrawLine(&arcPen, ax1, ay1, ax1 - 3.2f, ay1 + 2.5f);

            // Cung tròn phía dưới (quay theo chiều kim đồng hồ)
            g.DrawArc(&arcPen, icX - r, icY - r, r * 2.0f, r * 2.0f, 35.0f, 170.0f);
            // Đầu mũi tên của cung dưới
            float ax2 = icX - r * 0.82f;
            float ay2 = icY + r * 0.57f;
            g.DrawLine(&arcPen, ax2, ay2, ax2 + 3.2f, ay2 + 2.5f);
            g.DrawLine(&arcPen, ax2, ay2, ax2 + 3.2f, ay2 - 2.5f);

            Gdiplus::RectF textRect(rf.X + 22.0f, rf.Y, rf.Width - 22.0f, rf.Height);
            g.DrawString(text, -1, &font, textRect, &sf, &textBrush);
        } else {
            g.DrawString(text, -1, &font, rf, &sf, &textBrush);
        }
    }

    // ========================================================================
    // HÀM XỬ LÝ THÔNG ĐIỆP CHÍNH (CONNECTWNDPROC)
    // ========================================================================
    LRESULT CALLBACK ConnectWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_CREATE: {
                g_hConnectWnd = hwnd;

                // 1. Khởi tạo Font chữ chuẩn Segoe UI
                g_hFontTitle = CreateFontW(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

                g_hFontSub = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

                g_hFontNormal = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

                g_hFontHeader = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

                g_hFontCardTitle = CreateFontW(17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

                g_hFontEdit = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

                // Tạo các cọ vẽ GDI
                g_hBgBrush = CreateSolidBrush(RGB(241, 245, 249));   // Slate 100
                g_hWhiteBrush = CreateSolidBrush(RGB(255, 255, 255)); // Pure White

                // 2. Nút "Quét mạng" ở góc trên bên phải
                g_hBtnScan = CreateWindowExW(0, L"BUTTON", L"Quét mạng",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    608, 22, 124, 36, hwnd, (HMENU)IDC_BTN_SCAN, GetModuleHandle(NULL), NULL);

                // 3. Tạo bảng SysListView32 bên trong Card 1 (không viền thô)
                g_hListClients = CreateWindowExW(0, L"SysListView32", L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                    36, 132, 688, 202, hwnd, (HMENU)IDC_LIST_CLIENTS, GetModuleHandle(NULL), NULL);

                // Thiết lập phong cách bảng: Full row select và đệm đôi chống giật
                ListView_SetExtendedListViewStyle(g_hListClients, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
                ListView_SetBkColor(g_hListClients, RGB(255, 255, 255));
                ListView_SetTextBkColor(g_hListClients, RGB(255, 255, 255));

                // Gán ImageList giả lập để tăng chiều cao mỗi dòng lên 34px thoáng đãng
                g_hImageList = ImageList_Create(1, 34, ILC_COLOR32, 1, 1);
                ListView_SetImageList(g_hListClients, g_hImageList, LVSIL_SMALL);

                // Gán Subclass cho thanh Header của ListView để vẽ giao diện phẳng hiện đại, xóa viền xám 3D
                g_hHeader = ListView_GetHeader(g_hListClients);
                if (g_hHeader) {
                    g_oldHeaderProc = (WNDPROC)SetWindowLongPtrW(g_hHeader, GWLP_WNDPROC, (LONG_PTR)HeaderSubclassProc);
                }

                // Cấu hình 4 cột
                LVCOLUMNW lvc = {};
                lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

                lvc.iSubItem = 0; lvc.cx = 220; lvc.pszText = (LPWSTR)L"Tên máy tính";
                ListView_InsertColumn(g_hListClients, 0, &lvc);

                lvc.iSubItem = 1; lvc.cx = 140; lvc.pszText = (LPWSTR)L"Địa chỉ IP";
                ListView_InsertColumn(g_hListClients, 1, &lvc);

                lvc.iSubItem = 2; lvc.cx = 140; lvc.pszText = (LPWSTR)L"Tài khoản User";
                ListView_InsertColumn(g_hListClients, 2, &lvc);

                lvc.iSubItem = 3; lvc.cx = 170; lvc.pszText = (LPWSTR)L"Trạng thái";
                ListView_InsertColumn(g_hListClients, 3, &lvc);

                // 4. Ô nhập IP không viền nằm chính xác giữa tâm khung Card 2 (Canh giữa quang học hoàn hảo)
                g_hEditIp = CreateWindowExW(0, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                    76, 405, 168, 20, hwnd, (HMENU)IDC_EDIT_IP, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hEditIp, WM_SETFONT, (WPARAM)g_hFontEdit, TRUE);

                g_oldEditProc = (WNDPROC)SetWindowLongPtrW(g_hEditIp, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);

                // Nút "Localhost"
                g_hBtnLocalhost = CreateWindowExW(0, L"BUTTON", L"Localhost",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    262, 394, 88, 36, hwnd, (HMENU)IDC_BTN_LOCALHOST, GetModuleHandle(NULL), NULL);

                // 5. Nút "Kết nối" (Royal Blue)
                g_hBtnConnect = CreateWindowExW(0, L"BUTTON", L"↗  Kết nối",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    552, 402, 102, 38, hwnd, (HMENU)IDC_BTN_CONNECT, GetModuleHandle(NULL), NULL);

                // 6. Nút "Thoát"
                g_hBtnExit = CreateWindowExW(0, L"BUTTON", L"Thoát",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    662, 402, 70, 38, hwnd, (HMENU)IDC_BTN_EXIT, GetModuleHandle(NULL), NULL);

                // 7. Khởi động Timer quét mạng định kỳ mỗi 3500ms
                SetTimer(hwnd, TIMER_SCAN_ID, 3500, NULL);

                // 8. Bắt đầu lắng nghe phản hồi UDP từ Client
                DiscoveryScanner::Start([hwnd](const DiscoveredClient& client, bool isOnline) {
                    DiscoveredClient* pCopy = new DiscoveredClient(client);
                    PostMessageW(hwnd, WM_CLIENT_UPDATE, isOnline ? 1 : 0, (LPARAM)pCopy);
                });

                return 0;
            }

            // Vẽ toàn bộ nền, Header, Card containers và biểu tượng bằng GDI+
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);

                RECT rc;
                GetClientRect(hwnd, &rc);
                int clientW = rc.right;
                int clientH = rc.bottom;

                // Tạo Memory DC và Bitmap để đệm đôi chống chớp giật
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBmp = CreateCompatibleBitmap(hdc, clientW, clientH);
                HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

                {
                    Gdiplus::Graphics g(memDC);
                    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

                    // 1. Tô nền toàn cửa sổ #F1F5F9
                    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 241, 245, 249));
                    g.FillRectangle(&bgBrush, 0, 0, clientW, clientH);

                    // 2. Vẽ Banner trên cùng: Biểu tượng sơ đồ LAN mạng + Tiêu đề
                    DrawNetworkBannerIcon(g, 26.0f, 20.0f);

                    Gdiplus::FontFamily ff(L"Segoe UI");
                    Gdiplus::StringFormat sfLeft;
                    sfLeft.SetAlignment(Gdiplus::StringAlignmentNear);
                    sfLeft.SetLineAlignment(Gdiplus::StringAlignmentNear);
                    sfLeft.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

                    // Dòng phụ đề nhỏ phía trên: "LAN REMOTE DESKTOP"
                    Gdiplus::Font fontPre(&ff, 10.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush preBrush(Gdiplus::Color(255, 71, 85, 105)); // #475569
                    g.DrawString(L"LAN REMOTE DESKTOP", -1, &fontPre, Gdiplus::PointF(80.0f, 18.0f), &sfLeft, &preBrush);

                    // Tiêu đề lớn: "QUẢN LÝ KẾT NỐI"
                    Gdiplus::Font fontMain(&ff, 18.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush mainBrush(Gdiplus::Color(255, 15, 23, 42)); // #0F172A
                    g.DrawString(L"QUẢN LÝ KẾT NỐI", -1, &fontMain, Gdiplus::PointF(80.0f, 32.0f), &sfLeft, &mainBrush);

                    // Dòng mô tả: "Tự động phát hiện các máy Client trong mạng LAN • Kết nối điều khiển"
                    Gdiplus::Font fontDesc(&ff, 11.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush descBrush(Gdiplus::Color(255, 100, 116, 139)); // #64748B
                    g.DrawString(L"Tự động phát hiện các máy Client trong mạng LAN • Kết nối điều khiển", -1, &fontDesc, Gdiplus::PointF(80.0f, 57.0f), &sfLeft, &descBrush);

                    // Trạng thái quét mạng LAN góc trên bên phải
                    Gdiplus::StringFormat sfRight;
                    sfRight.SetAlignment(Gdiplus::StringAlignmentFar);
                    sfRight.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    sfRight.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

                    Gdiplus::Color statColor = Gdiplus::Color(255, 71, 85, 105);
                    if (g_statusColorType == 1) statColor = Gdiplus::Color(255, 37, 99, 235);
                    else if (g_statusColorType == 2) statColor = Gdiplus::Color(255, 220, 38, 38);

                    Gdiplus::SolidBrush statBrush(statColor);
                    Gdiplus::Font fontStat(&ff, 11.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                    Gdiplus::RectF statRect(460.0f, 62.0f, 272.0f, 20.0f);
                    g.DrawString(g_statusText.c_str(), -1, &fontStat, statRect, &sfRight, &statBrush);

                    // 3. Card 1: Khung danh sách máy Client (Bo góc 10px, Nền trắng, Viền #E2E8F0)
                    Gdiplus::RectF card1Rect(24.0f, 88.0f, (float)(clientW - 48), 252.0f);
                    Gdiplus::GraphicsPath card1Path;
                    AddRoundRectPath(card1Path, card1Rect, 10.0f);

                    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
                    Gdiplus::Pen cardBorder(Gdiplus::Color(255, 226, 232, 240), 1.0f); // #E2E8F0
                    g.FillPath(&whiteBrush, &card1Path);
                    g.DrawPath(&cardBorder, &card1Path);

                    // Tiêu đề trong Card 1: "Danh sách máy Client đang bật trong mạng LAN"
                    Gdiplus::Font fontCard1(&ff, 13.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush card1TitleBrush(Gdiplus::Color(255, 15, 23, 42));
                    g.DrawString(L"Danh sách máy Client đang bật trong mạng LAN", -1, &fontCard1, Gdiplus::PointF(40.0f, 102.0f), &sfLeft, &card1TitleBrush);

                    // 4. Card 2: Khung nhập địa chỉ IP thủ công
                    Gdiplus::RectF card2Rect(24.0f, 356.0f, 340.0f, 84.0f);
                    Gdiplus::GraphicsPath card2Path;
                    AddRoundRectPath(card2Path, card2Rect, 10.0f);
                    g.FillPath(&whiteBrush, &card2Path);
                    g.DrawPath(&cardBorder, &card2Path);

                    // Tiêu đề trong Card 2: "Hoặc nhập địa chỉ IP Client thủ công:"
                    Gdiplus::Font fontCard2(&ff, 12.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    g.DrawString(L"Hoặc nhập địa chỉ IP Client thủ công:", -1, &fontCard2, Gdiplus::PointF(38.0f, 368.0f), &sfLeft, &card1TitleBrush);

                    // Hộp viền bao quanh ô nhập IP và icon máy tính IP
                    Gdiplus::RectF ipBoxRect(38.0f, 394.0f, 214.0f, 36.0f);
                    Gdiplus::GraphicsPath ipBoxPath;
                    AddRoundRectPath(ipBoxPath, ipBoxRect, 6.0f);

                    Gdiplus::Pen ipBorderPen(g_editHasFocus ? Gdiplus::Color(255, 37, 99, 235) : Gdiplus::Color(255, 203, 213, 225),
                                            g_editHasFocus ? 1.6f : 1.0f);
                    g.DrawPath(&ipBorderPen, &ipBoxPath);

                    // Icon máy tính thu nhỏ có chữ IP bên trong canh giữa thẳng hàng
                    Gdiplus::RectF miniScr(46.0f, 404.0f, 22.0f, 15.0f);
                    Gdiplus::GraphicsPath miniScrPath;
                    AddRoundRectPath(miniScrPath, miniScr, 1.5f);
                    Gdiplus::Pen miniPen(Gdiplus::Color(255, 71, 85, 105), 1.2f);
                    g.DrawPath(&miniPen, &miniScrPath);
                    g.DrawLine(&miniPen, 57.0f, 419.0f, 57.0f, 422.0f);
                    g.DrawLine(&miniPen, 52.0f, 422.0f, 62.0f, 422.0f);

                    Gdiplus::Font fontMiniIP(&ff, 8.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush ipTextBrush(Gdiplus::Color(255, 71, 85, 105));
                    Gdiplus::StringFormat sfCenter;
                    sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
                    sfCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    g.DrawString(L"IP", -1, &fontMiniIP, miniScr, &sfCenter, &ipTextBrush);
                }

                // Chép từ Memory DC lên màn hình HDC chính
                BitBlt(hdc, 0, 0, clientW, clientH, memDC, 0, 0, SRCCOPY);

                SelectObject(memDC, oldBmp);
                DeleteObject(memBmp);
                DeleteDC(memDC);

                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_ERASEBKGND:
                return 1; // Ngăn chặn nhấp nháy màn hình

            // Tùy biến màu nền ô Edit để hòa vào Card trắng
            case WM_CTLCOLOREDIT: {
                HDC hdcEdit = (HDC)wp;
                SetTextColor(hdcEdit, RGB(15, 23, 42)); // #0F172A
                SetBkColor(hdcEdit, RGB(255, 255, 255));
                return (LRESULT)g_hWhiteBrush;
            }

            case WM_DRAWITEM: {
                LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
                if (dis->CtlID == IDC_BTN_CONNECT || dis->CtlID == IDC_BTN_SCAN ||
                    dis->CtlID == IDC_BTN_LOCALHOST || dis->CtlID == IDC_BTN_EXIT) {
                    DrawModernButton(dis);
                    return TRUE;
                }
                break;
            }

            // Theo dõi chuột để tạo hiệu ứng hover mượt mà cho 4 nút bấm
            case WM_SETCURSOR: {
                HWND hCursorWnd = (HWND)wp;
                if (hCursorWnd == g_hBtnScan || hCursorWnd == g_hBtnLocalhost ||
                    hCursorWnd == g_hBtnConnect || hCursorWnd == g_hBtnExit) {
                    if (g_hHoveredBtn != hCursorWnd) {
                        if (g_hHoveredBtn) InvalidateRect(g_hHoveredBtn, NULL, FALSE);
                        g_hHoveredBtn = hCursorWnd;
                        InvalidateRect(g_hHoveredBtn, NULL, FALSE);
                    }
                    TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                    TrackMouseEvent(&tme);
                } else {
                    if (g_hHoveredBtn) {
                        HWND old = g_hHoveredBtn;
                        g_hHoveredBtn = NULL;
                        InvalidateRect(old, NULL, FALSE);
                    }
                }
                break;
            }

            case WM_MOUSELEAVE: {
                if (g_hHoveredBtn) {
                    HWND old = g_hHoveredBtn;
                    g_hHoveredBtn = NULL;
                    InvalidateRect(old, NULL, FALSE);
                }
                return 0;
            }

            case WM_LBUTTONDOWN: {
                // Nhấp vào vùng hộp IP -> Tự động focus ô Edit
                int x = LOWORD(lp);
                int y = HIWORD(lp);
                if (x >= 38 && x <= 252 && y >= 394 && y <= 430) {
                    SetFocus(g_hEditIp);
                    SendMessageW(g_hEditIp, EM_SETSEL, 0, -1);
                }
                break;
            }

            case WM_COMMAND: {
                int id = LOWORD(wp);
                if (id == IDC_EDIT_IP && HIWORD(wp) == EN_CHANGE) {
                    if (GetFocus() == g_hEditIp) {
                        g_userManuallyEditedIp = true; // Người dùng tự gõ/chỉnh sửa nội dung ô IP
                    }
                } else if (id == IDC_BTN_CONNECT) {
                    StartConnectionProcess(hwnd);
                } else if (id == IDC_BTN_SCAN) {
                    SetStatus(L"🔄 Đang quét tìm kiếm Client trong LAN...", 1);
                    DiscoveryScanner::TriggerScan();
                } else if (id == IDC_BTN_LOCALHOST) {
                    SetWindowTextW(g_hEditIp, L"127.0.0.1");
                    SendMessageW(g_hEditIp, EM_SETSEL, 0, -1);
                    SetFocus(g_hEditIp);
                    g_userManuallyEditedIp = true; // Đánh dấu người dùng chủ động chọn Localhost
                } else if (id == IDC_BTN_EXIT) {
                    SendMessageW(hwnd, WM_CLOSE, 0, 0);
                }
                return 0;
            }

            // ================================================================
            // TÙY BIẾN VẼ BẢNG LISTVIEW VỚI ICON VECTOR VÀ PILL BADGE
            // ================================================================
            case WM_NOTIFY: {
                LPNMHDR pnm = (LPNMHDR)lp;
                if (pnm->idFrom == IDC_LIST_CLIENTS) {
                    if (pnm->code == NM_CUSTOMDRAW) {
                        LPNMLVCUSTOMDRAW pLvcd = (LPNMLVCUSTOMDRAW)lp;

                        if (pLvcd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                            return CDRF_NOTIFYITEMDRAW;
                        }

                        if (pLvcd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                            int itemIdx = (int)pLvcd->nmcd.dwItemSpec;
                            bool isSelected = (ListView_GetItemState(g_hListClients, itemIdx, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                            HDC hdc = pLvcd->nmcd.hdc;

                            RECT rcItem;
                            ListView_GetItemRect(g_hListClients, itemIdx, &rcItem, LVIR_BOUNDS);

                            Gdiplus::Graphics g(hdc);
                            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

                            if (isSelected) {
                                // Hàng được chọn: Nền xanh nhạt #EFF6FF bo góc mềm mại, viền #BFDBFE
                                Gdiplus::RectF rf((float)rcItem.left + 2.0f, (float)rcItem.top + 2.0f,
                                                 (float)(rcItem.right - rcItem.left) - 4.0f, (float)(rcItem.bottom - rcItem.top) - 4.0f);
                                Gdiplus::GraphicsPath selPath;
                                AddRoundRectPath(selPath, rf, 6.0f);

                                Gdiplus::SolidBrush selBg(Gdiplus::Color(255, 239, 246, 255)); // #EFF6FF
                                Gdiplus::Pen selBorder(Gdiplus::Color(255, 191, 219, 254), 1.0f); // #BFDBFE
                                g.FillPath(&selBg, &selPath);
                                g.DrawPath(&selBorder, &selPath);
                            } else {
                                // Hàng không chọn: Nền trắng và đường kẻ phân cách nhẹ dưới cùng
                                Gdiplus::SolidBrush whiteBg(Gdiplus::Color(255, 255, 255, 255));
                                g.FillRectangle(&whiteBg, (INT)rcItem.left, (INT)rcItem.top, (INT)(rcItem.right - rcItem.left), (INT)(rcItem.bottom - rcItem.top));

                                Gdiplus::Pen divPen(Gdiplus::Color(255, 241, 245, 249), 1.0f);
                                g.DrawLine(&divPen, (float)rcItem.left, (float)rcItem.bottom - 1.0f, (float)rcItem.right, (float)rcItem.bottom - 1.0f);
                            }

                            return CDRF_NOTIFYSUBITEMDRAW;
                        }

                        if (pLvcd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                            int itemIdx = (int)pLvcd->nmcd.dwItemSpec;
                            int subItem = pLvcd->iSubItem;
                            HDC hdc = pLvcd->nmcd.hdc;
                            bool isSelected = (ListView_GetItemState(g_hListClients, itemIdx, LVIS_SELECTED) & LVIS_SELECTED) != 0;

                            RECT subRect;
                            ListView_GetSubItemRect(g_hListClients, itemIdx, subItem, LVIR_BOUNDS, &subRect);

                            Gdiplus::Graphics g(hdc);
                            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

                            Gdiplus::FontFamily ff(L"Segoe UI");
                            Gdiplus::StringFormat sf;
                            sf.SetAlignment(Gdiplus::StringAlignmentNear);
                            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                            sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
                            sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

                            if (subItem == 0) {
                                // Cột 0: Icon máy tính vector + Tên máy tính
                                float rowH = (float)(subRect.bottom - subRect.top);
                                float iconY = (float)subRect.top + (rowH - 17.0f) / 2.0f;
                                DrawMonitorRowIcon(g, (float)subRect.left + 8.0f, iconY);

                                wchar_t nameBuf[128] = {0};
                                ListView_GetItemText(g_hListClients, itemIdx, 0, nameBuf, 127);

                                Gdiplus::Font fontName(&ff, 13.0f, isSelected ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                                Gdiplus::SolidBrush nameBrush(isSelected ? Gdiplus::Color(255, 30, 58, 138) : Gdiplus::Color(255, 15, 23, 42));
                                Gdiplus::RectF textRect((float)subRect.left + 34.0f, (float)subRect.top, (float)(subRect.right - subRect.left - 34), rowH);
                                g.DrawString(nameBuf, -1, &fontName, textRect, &sf, &nameBrush);

                                return CDRF_SKIPDEFAULT;
                            } else if (subItem == 1) {
                                // Cột 1: Địa chỉ IP
                                wchar_t ipBuf[64] = {0};
                                ListView_GetItemText(g_hListClients, itemIdx, 1, ipBuf, 63);

                                Gdiplus::Font fontIP(&ff, 13.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                                Gdiplus::SolidBrush ipBrush(Gdiplus::Color(255, 51, 65, 85));
                                Gdiplus::RectF textRect((float)subRect.left + 6.0f, (float)subRect.top, (float)(subRect.right - subRect.left - 6), (float)(subRect.bottom - subRect.top));
                                g.DrawString(ipBuf, -1, &fontIP, textRect, &sf, &ipBrush);

                                return CDRF_SKIPDEFAULT;
                            } else if (subItem == 2) {
                                // Cột 2: Tên tài khoản Windows
                                wchar_t userBuf[64] = {0};
                                ListView_GetItemText(g_hListClients, itemIdx, 2, userBuf, 63);

                                Gdiplus::Font fontUser(&ff, 13.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                                Gdiplus::SolidBrush userBrush(Gdiplus::Color(255, 51, 65, 85));
                                Gdiplus::RectF textRect((float)subRect.left + 6.0f, (float)subRect.top, (float)(subRect.right - subRect.left - 6), (float)(subRect.bottom - subRect.top));
                                g.DrawString(userBuf, -1, &fontUser, textRect, &sf, &userBrush);

                                return CDRF_SKIPDEFAULT;
                            } else if (subItem == 3) {
                                // Cột 3: Badge dạng Pill hiển thị trạng thái
                                wchar_t statusBuf[64] = {0};
                                ListView_GetItemText(g_hListClients, itemIdx, 3, statusBuf, 63);

                                int stType = 2; // Mặc định: Ngoại tuyến
                                if (wcsstr(statusBuf, L"hoạt động") != nullptr || wcsstr(statusBuf, L"Sẵn sàng") != nullptr) {
                                    stType = 0;
                                } else if (wcsstr(statusBuf, L"bận") != nullptr || wcsstr(statusBuf, L"Bận") != nullptr) {
                                    stType = 1;
                                }

                                Gdiplus::RectF cellRect((float)subRect.left, (float)subRect.top, (float)(subRect.right - subRect.left), (float)(subRect.bottom - subRect.top));
                                DrawStatusBadgePill(g, cellRect, stType, statusBuf);

                                return CDRF_SKIPDEFAULT;
                            }
                            return CDRF_DODEFAULT;
                        }
                    } else if (pnm->code == NM_DBLCLK) {
                        int curSel = ListView_GetNextItem(g_hListClients, -1, LVNI_SELECTED);
                        if (curSel != -1) {
                            HandleSelectedRow(curSel, true);
                        }
                    } else if (pnm->code == NM_CLICK) {
                        int curSel = ListView_GetNextItem(g_hListClients, -1, LVNI_SELECTED);
                        if (curSel != -1) {
                            HandleSelectedRow(curSel, false);
                        }
                    }
                }
                break;
            }

            case WM_TIMER: {
                if (wp == TIMER_SCAN_ID) {
                    DWORD now = GetTickCount();
                    bool listChanged = false;

                    for (auto it = g_discoveredList.begin(); it != g_discoveredList.end(); ) {
                        if (it->isOnline && (now - it->lastSeenTick > 8000)) {
                            // Quá 8 giây không nhận được gói tin UDP -> Chuyển sang Ngoại tuyến
                            it->isOnline = false;
                            it->isBusy = false;
                            listChanged = true;
                            ++it;
                        } else if (!it->isOnline && (now - it->lastSeenTick > 60000)) {
                            // Ngoại tuyến quá 60 giây -> Dọn dẹp khỏi bảng
                            it = g_discoveredList.erase(it);
                            listChanged = true;
                        } else {
                            ++it;
                        }
                    }

                    if (listChanged) {
                        RefreshListViewUI();
                    }

                    if (!g_isConnecting) {
                        DiscoveryScanner::TriggerScan();
                    }
                }
                return 0;
            }

            case WM_CLIENT_UPDATE: {
                bool isOnline = (wp == 1);
                DiscoveredClient* pClient = (DiscoveredClient*)lp;
                if (!pClient) return 0;

                auto it = std::find_if(g_discoveredList.begin(), g_discoveredList.end(),
                    [&](const ClientDisplayItem& c) {
                        return _wcsicmp(c.computerName.c_str(), pClient->computerName.c_str()) == 0;
                    });

                if (isOnline) {
                    if (it != g_discoveredList.end()) {
                        it->userName = pClient->userName;
                        it->isBusy = pClient->isBusy;
                        it->isOnline = true;
                        it->lastSeenTick = GetTickCount();

                        if (GetIpPriority(pClient->ip) >= GetIpPriority(it->ip)) {
                            it->ip = pClient->ip;
                        }
                    } else {
                        ClientDisplayItem item;
                        item.ip = pClient->ip;
                        item.computerName = pClient->computerName;
                        item.userName = pClient->userName;
                        item.isBusy = pClient->isBusy;
                        item.isOnline = true;
                        item.lastSeenTick = GetTickCount();
                        g_discoveredList.push_back(item);
                    }
                } else {
                    // Nhận tín hiệu DISCOVERY_BYE -> Đánh dấu Ngoại tuyến ngay
                    if (it != g_discoveredList.end()) {
                        it->isOnline = false;
                        it->isBusy = false;
                        it->lastSeenTick = GetTickCount();
                    }
                }

                RefreshListViewUI();

                // Tự động điền IP của Client lần đầu tiên DUY NHẤT nếu chưa từng điền và người dùng chưa tự gõ IP
                if (!g_hasAutoFilledIp && !g_userManuallyEditedIp) {
                    std::string autoIp = "";
                    // Ưu tiên chọn IP mạng LAN thật khác 127.0.0.1
                    for (const auto& c : g_discoveredList) {
                        if (c.isOnline && c.ip != "127.0.0.1") {
                            autoIp = c.ip;
                            break;
                        }
                    }
                    if (autoIp.empty() && !g_discoveredList.empty() && g_discoveredList[0].isOnline) {
                        autoIp = g_discoveredList[0].ip;
                    }
                    if (!autoIp.empty()) {
                        std::wstring ipW = Utils::Utf8ToWide(autoIp);
                        SetWindowTextW(g_hEditIp, ipW.c_str());
                        SendMessageW(g_hEditIp, EM_SETSEL, 0, -1);
                        g_hasAutoFilledIp = true; // Chỉ tự động điền ĐÚNG 1 LẦN DUY NHẤT!
                    }
                }

                delete pClient;
                return 0;
            }

            case WM_CONNECT_RESULT: {
                g_isConnecting = false;
                bool ok = (wp == 1);
                std::string* pErr = (std::string*)lp;

                if (ok) {
                    g_connectedIP = g_targetIP;
                    g_success = true;
                    g_isOpen = false;
                    DestroyWindow(hwnd);
                } else {
                    EnableWindow(g_hBtnConnect, TRUE);
                    EnableWindow(g_hBtnLocalhost, TRUE);
                    EnableWindow(g_hBtnScan, TRUE);
                    EnableWindow(g_hEditIp, TRUE);
                    EnableWindow(g_hListClients, TRUE);

                    std::wstring errW = L"Không thể kết nối tới Client!";
                    if (pErr && !pErr->empty()) {
                        errW = Utils::Utf8ToWide(*pErr);
                    }
                    SetStatus(L"⚠ Kết nối thất bại!", 2);
                    MessageBoxW(hwnd, errW.c_str(), L"Lỗi kết nối", MB_OK | MB_ICONERROR);
                    SetFocus(g_hEditIp);
                }

                if (pErr) delete pErr;
                return 0;
            }

            case WM_CLOSE: {
                g_isOpen = false;
                g_success = false;
                DestroyWindow(hwnd);
                return 0;
            }

            case WM_DESTROY: {
                KillTimer(hwnd, TIMER_SCAN_ID);
                DiscoveryScanner::Stop();

                if (g_hFontTitle) { DeleteObject(g_hFontTitle); g_hFontTitle = NULL; }
                if (g_hFontSub) { DeleteObject(g_hFontSub); g_hFontSub = NULL; }
                if (g_hFontNormal) { DeleteObject(g_hFontNormal); g_hFontNormal = NULL; }
                if (g_hFontHeader) { DeleteObject(g_hFontHeader); g_hFontHeader = NULL; }
                if (g_hFontCardTitle) { DeleteObject(g_hFontCardTitle); g_hFontCardTitle = NULL; }
                if (g_hFontEdit) { DeleteObject(g_hFontEdit); g_hFontEdit = NULL; }

                if (g_hBgBrush) { DeleteObject(g_hBgBrush); g_hBgBrush = NULL; }
                if (g_hWhiteBrush) { DeleteObject(g_hWhiteBrush); g_hWhiteBrush = NULL; }
                if (g_hImageList) { ImageList_Destroy(g_hImageList); g_hImageList = NULL; }

                if (g_hHeader && g_oldHeaderProc) {
                    SetWindowLongPtrW(g_hHeader, GWLP_WNDPROC, (LONG_PTR)g_oldHeaderProc);
                    g_oldHeaderProc = NULL;
                }
                g_hHeader = NULL;

                g_hConnectWnd = NULL;
                g_hListClients = NULL;
                PostMessageW(NULL, WM_NULL, 0, 0);
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

namespace ConnectWindow {
    bool Show(HINSTANCE hInstance, std::string& outSelectedIP, ConnectCallback onConnect) {
        g_callback = onConnect;
        g_success = false;
        g_isOpen = true;
        g_isConnecting = false;
        g_statusText = L"🔄 Đang tự động quét mạng LAN...";
        g_statusColorType = 0;
        g_discoveredList.clear();
        g_hasAutoFilledIp = false;
        g_userManuallyEditedIp = false;

        WNDCLASSW wc = {};
        wc.lpfnWndProc = ConnectWndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = L"LanRemoteConnectWnd";

        RegisterClassW(&wc);

        // Kích thước chuẩn xác cân đối tỷ lệ vàng theo thiết kế
        int targetClientW = 760;
        int targetClientH = 470;

        RECT wr = { 0, 0, targetClientW, targetClientH };
        AdjustWindowRectEx(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);

        int winWidth = wr.right - wr.left;
        int winHeight = wr.bottom - wr.top;
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        int posX = (screenW - winWidth) / 2;
        int posY = (screenH - winHeight) / 2;

        HWND hwnd = CreateWindowExW(
            0,
            L"LanRemoteConnectWnd",
            L"LanRemote - Dashboard Quản lý Kết nối LAN",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
            posX, posY, winWidth, winHeight,
            NULL, NULL, hInstance, NULL
        );

        if (!hwnd) {
            return false;
        }

        UpdateWindow(hwnd);

        MSG msg;
        while (g_isOpen && GetMessageW(&msg, NULL, 0, 0)) {
            if (!IsDialogMessageW(hwnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        if (g_success) {
            outSelectedIP = g_connectedIP;
            return true;
        }
        return false;
    }
}
