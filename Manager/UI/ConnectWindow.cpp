#include "ConnectWindow.h"
#include "../Core/DiscoveryScanner.h"
#include "../../Shared/Utils.h"
#include <commctrl.h>
#include <thread>
#include <vector>
#include <string>
#include <algorithm>

#define WM_CONNECT_RESULT (WM_APP + 201)
#define WM_CLIENT_UPDATE  (WM_APP + 202)
#define TIMER_SCAN_ID     1001

namespace {
    HWND g_hConnectWnd = NULL;
    HWND g_hListClients = NULL;
    HWND g_hEditIp = NULL;
    HWND g_hBtnConnect = NULL;
    HWND g_hBtnLocalhost = NULL;
    HWND g_hBtnScan = NULL;
    HWND g_hBtnExit = NULL;
    HWND g_hStatusLabel = NULL;

    HFONT g_hFontTitle = NULL;
    HFONT g_hFontSubtitle = NULL;
    HFONT g_hFontLabel = NULL;
    HFONT g_hFontEdit = NULL;
    HFONT g_hFontStatus = NULL;
    HFONT g_hFontBtn = NULL;

    HBRUSH g_hBodyBrush = NULL;
    HBRUSH g_hHeaderBrush = NULL;

    WNDPROC g_oldEditProc = NULL;
    ConnectWindow::ConnectCallback g_callback = nullptr;

    bool g_isOpen = false;
    bool g_success = false;
    bool g_isConnecting = false;
    std::string g_targetIP = "";
    std::string g_connectedIP = "";
    std::wstring g_statusText = L"Đang tự động quét mạng LAN...";
    int g_statusColorType = 1; // 0: Normal (Gray), 1: Info (Blue), 2: Error (Red)

    std::vector<DiscoveredClient> g_discoveredList;

    enum ControlId {
        IDC_LIST_CLIENTS = 100,
        IDC_EDIT_IP = 101,
        IDC_BTN_CONNECT = 102,
        IDC_BTN_LOCALHOST = 103,
        IDC_BTN_SCAN = 104,
        IDC_BTN_EXIT = 105,
        IDC_STATUS_LABEL = 106
    };

    // Đánh giá mức độ ưu tiên IP để chọn card mạng LAN thật thay vì card ảo hay loopback
    int GetIpPriority(const std::string& ip) {
        if (ip.rfind("192.168.", 0) == 0 && ip.rfind("192.168.56.", 0) != 0) return 3; // IP LAN thật (Wi-Fi/Ethernet)
        if (ip.rfind("10.", 0) == 0) return 3;                                          // IP LAN doanh nghiệp
        if (ip.rfind("172.", 0) == 0) return 2;                                         // IP card ảo (WSL, Docker)
        if (ip.rfind("192.168.56.", 0) == 0) return 2;                                 // IP VirtualBox
        if (ip == "127.0.0.1") return 1;                                               // Localhost
        return 0;
    }

    // Subclass riêng cho Edit box để nhận phím Enter và Esc
    LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == WM_KEYDOWN) {
            if (wParam == VK_RETURN) {
                // Nhấn Enter trong ô nhập -> Bấm nút Kết nối
                SendMessageW(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(IDC_BTN_CONNECT, BN_CLICKED), 0);
                return 0;
            } else if (wParam == VK_ESCAPE) {
                // Nhấn Esc -> Đóng cửa sổ
                SendMessageW(GetParent(hWnd), WM_CLOSE, 0, 0);
                return 0;
            }
        }
        return CallWindowProcW(g_oldEditProc, hWnd, uMsg, wParam, lParam);
    }

    void SetStatus(const std::wstring& text, int colorType) {
        g_statusText = text;
        g_statusColorType = colorType;
        if (g_hStatusLabel) {
            SetWindowTextW(g_hStatusLabel, text.c_str());
            InvalidateRect(g_hStatusLabel, NULL, TRUE);
        }
    }

    void RefreshListViewUI() {
        if (!g_hListClients) return;

        // Lưu lại tên máy đang được chọn trước khi cập nhật
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

            LVITEMW lvi = {};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = i;
            lvi.iSubItem = 0;
            lvi.pszText = (LPWSTR)client.computerName.c_str();
            ListView_InsertItem(g_hListClients, &lvi);

            std::wstring ipW = Utils::Utf8ToWide(client.ip);
            ListView_SetItemText(g_hListClients, i, 1, (LPWSTR)ipW.c_str());

            ListView_SetItemText(g_hListClients, i, 2, (LPWSTR)client.userName.c_str());

            std::wstring statusStr = client.isBusy ? L"Đang bận" : L"Sẵn sàng";
            ListView_SetItemText(g_hListClients, i, 3, (LPWSTR)statusStr.c_str());

            if (!selectedComputer.empty() && _wcsicmp(client.computerName.c_str(), selectedComputer.c_str()) == 0) {
                newSelIdx = i;
            }
        }

        // Khôi phục lại trạng thái chọn dòng
        if (newSelIdx != -1) {
            ListView_SetItemState(g_hListClients, newSelIdx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }

        SendMessageW(g_hListClients, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(g_hListClients, NULL, TRUE);

        if (!g_isConnecting) {
            if (g_discoveredList.empty()) {
                SetStatus(L"Chưa phát hiện máy Client nào trong mạng LAN. Bạn có thể nhấn 'Quét lại' hoặc nhập IP thủ công.", 0);
            } else {
                std::wstring msg = L"Tìm thấy " + std::to_wstring(g_discoveredList.size()) + L" máy Client trong LAN. Click đúp vào máy để kết nối!";
                SetStatus(msg, 0);
            }
        }
    }

    void StartConnectionProcess(HWND hwnd) {
        if (g_isConnecting) return;

        wchar_t buf[256] = {0};
        GetWindowTextW(g_hEditIp, buf, 255);

        // Trim khoảng trắng
        std::wstring ws = buf;
        size_t start = ws.find_first_not_of(L" \t\r\n");
        size_t end = ws.find_last_not_of(L" \t\r\n");
        if (start == std::wstring::npos) {
            SetStatus(L"Vui lòng chọn 1 máy trong danh sách hoặc nhập địa chỉ IP!", 2);
            SetFocus(g_hEditIp);
            return;
        }
        std::wstring trimmed = ws.substr(start, end - start + 1);
        std::string ipStr = Utils::WideToUtf8(trimmed);

        g_targetIP = ipStr;
        g_isConnecting = true;

        // Vô hiệu hóa các nút điều khiển trong khi đang kết nối
        EnableWindow(g_hBtnConnect, FALSE);
        EnableWindow(g_hBtnLocalhost, FALSE);
        EnableWindow(g_hBtnScan, FALSE);
        EnableWindow(g_hEditIp, FALSE);
        EnableWindow(g_hListClients, FALSE);

        SetStatus(L"Đang kết nối tới " + trimmed + L"... Vui lòng đợi.", 1);

        // Thực hiện kết nối trên luồng riêng để giao diện không bị treo
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

    void HandleSelectedRow(int index, bool triggerConnect) {
        if (index < 0 || index >= (int)g_discoveredList.size()) return;

        std::wstring ipW = Utils::Utf8ToWide(g_discoveredList[index].ip);
        SetWindowTextW(g_hEditIp, ipW.c_str());
        SendMessageW(g_hEditIp, EM_SETSEL, 0, -1);

        if (triggerConnect) {
            StartConnectionProcess(g_hConnectWnd);
        }
    }

    LRESULT CALLBACK ConnectWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_CREATE: {
                g_hConnectWnd = hwnd;

                // 1. Tạo các font chữ giao diện
                g_hFontTitle = CreateFontW(19, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

                g_hFontSubtitle = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

                g_hFontLabel = CreateFontW(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

                g_hFontEdit = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

                g_hFontStatus = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

                g_hFontBtn = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

                g_hBodyBrush = CreateSolidBrush(RGB(245, 246, 248));
                g_hHeaderBrush = CreateSolidBrush(RGB(33, 37, 41));

                // 2. Tạo Label và Nút "Quét lại" phía trên ListView
                HWND hLblList = CreateWindowExW(0, L"STATIC", L"Danh sách máy Client đang bật trong mạng LAN:",
                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                    25, 82, 360, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
                SendMessageW(hLblList, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);

                g_hBtnScan = CreateWindowExW(0, L"BUTTON", L"🔍 Quét mạng",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    485, 76, 120, 28, hwnd, (HMENU)IDC_BTN_SCAN, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hBtnScan, WM_SETFONT, (WPARAM)g_hFontSubtitle, TRUE);

                // 3. Tạo ListView hiển thị bảng danh sách Client
                g_hListClients = CreateWindowExW(WS_EX_CLIENTEDGE, L"SysListView32", L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                    25, 108, 580, 205, hwnd, (HMENU)IDC_LIST_CLIENTS, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hListClients, WM_SETFONT, (WPARAM)g_hFontEdit, TRUE);

                ListView_SetExtendedListViewStyle(g_hListClients,
                    LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

                // Thêm các cột cho bảng
                LVCOLUMNW lvc = {};
                lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

                lvc.iSubItem = 0; lvc.cx = 175; lvc.pszText = (LPWSTR)L"Tên máy tính";
                ListView_InsertColumn(g_hListClients, 0, &lvc);

                lvc.iSubItem = 1; lvc.cx = 135; lvc.pszText = (LPWSTR)L"Địa chỉ IP";
                ListView_InsertColumn(g_hListClients, 1, &lvc);

                lvc.iSubItem = 2; lvc.cx = 125; lvc.pszText = (LPWSTR)L"Tài khoản User";
                ListView_InsertColumn(g_hListClients, 2, &lvc);

                lvc.iSubItem = 3; lvc.cx = 135; lvc.pszText = (LPWSTR)L"Trạng thái";
                ListView_InsertColumn(g_hListClients, 3, &lvc);

                // 4. Tạo phần nhập IP thủ công phía dưới
                HWND hLblManual = CreateWindowExW(0, L"STATIC", L"Hoặc nhập địa chỉ IP Client thủ công:",
                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                    25, 325, 300, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
                SendMessageW(hLblManual, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);

                g_hEditIp = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"127.0.0.1",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                    25, 348, 280, 28, hwnd, (HMENU)IDC_EDIT_IP, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hEditIp, WM_SETFONT, (WPARAM)g_hFontEdit, TRUE);

                g_oldEditProc = (WNDPROC)SetWindowLongPtrW(g_hEditIp, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);

                g_hBtnLocalhost = CreateWindowExW(0, L"BUTTON", L"Localhost",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    315, 348, 100, 28, hwnd, (HMENU)IDC_BTN_LOCALHOST, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hBtnLocalhost, WM_SETFONT, (WPARAM)g_hFontSubtitle, TRUE);

                // 5. Tạo Status Label
                g_hStatusLabel = CreateWindowExW(0, L"STATIC", g_statusText.c_str(),
                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                    25, 390, 580, 35, hwnd, (HMENU)IDC_STATUS_LABEL, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hStatusLabel, WM_SETFONT, (WPARAM)g_hFontStatus, TRUE);

                // 6. Tạo nút "Kết nối" (Default button)
                g_hBtnConnect = CreateWindowExW(0, L"BUTTON", L"Kết nối",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    190, 430, 130, 36, hwnd, (HMENU)IDC_BTN_CONNECT, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hBtnConnect, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

                // 7. Tạo nút "Thoát"
                g_hBtnExit = CreateWindowExW(0, L"BUTTON", L"Thoát",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    335, 430, 105, 36, hwnd, (HMENU)IDC_BTN_EXIT, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hBtnExit, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);

                // 8. Đặt Timer tự động quét lại ngầm mỗi 4 giây
                SetTimer(hwnd, TIMER_SCAN_ID, 4000, NULL);

                // 9. Bắt đầu lắng nghe phản hồi UDP và phát quét ngay lập tức
                DiscoveryScanner::Start([hwnd](const DiscoveredClient& client, bool isOnline) {
                    DiscoveredClient* pCopy = new DiscoveredClient(client);
                    PostMessageW(hwnd, WM_CLIENT_UPDATE, isOnline ? 1 : 0, (LPARAM)pCopy);
                });

                return 0;
            }

            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);

                RECT clientRect;
                GetClientRect(hwnd, &clientRect);

                // Header banner phía trên (Y: 0 -> 68px)
                RECT headerRect = { 0, 0, clientRect.right, 68 };
                FillRect(hdc, &headerRect, g_hHeaderBrush);

                SetBkMode(hdc, TRANSPARENT);

                // Tiêu đề lớn
                SetTextColor(hdc, RGB(255, 255, 255));
                HFONT oldFont = (HFONT)SelectObject(hdc, g_hFontTitle);
                RECT titleRect = { 25, 12, clientRect.right - 25, 38 };
                DrawTextW(hdc, L"LAN REMOTE DESKTOP - QUẢN LÝ KẾT NỐI", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                // Phụ đề nhỏ
                SetTextColor(hdc, RGB(173, 181, 189));
                SelectObject(hdc, g_hFontSubtitle);
                RECT subRect = { 25, 38, clientRect.right - 25, 60 };
                DrawTextW(hdc, L"Tự động phát hiện các máy Client trong mạng LAN & Kết nối điều khiển", -1, &subRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                // Đường viền ngăn cách
                HPEN hPen = CreatePen(PS_SOLID, 1, RGB(222, 226, 230));
                HPEN oldPen = (HPEN)SelectObject(hdc, hPen);
                MoveToEx(hdc, 0, 68, NULL);
                LineTo(hdc, clientRect.right, 68);
                SelectObject(hdc, oldPen);
                DeleteObject(hPen);

                SelectObject(hdc, oldFont);
                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_ERASEBKGND: {
                HDC hdc = (HDC)wp;
                RECT rc;
                GetClientRect(hwnd, &rc);
                FillRect(hdc, &rc, g_hBodyBrush);
                return 1;
            }

            case WM_CTLCOLORSTATIC: {
                HDC hdc = (HDC)wp;
                HWND hCtrl = (HWND)lp;

                SetBkMode(hdc, TRANSPARENT);

                if (hCtrl == g_hStatusLabel) {
                    if (g_statusColorType == 1) {
                        SetTextColor(hdc, RGB(13, 110, 253)); // Blue
                    } else if (g_statusColorType == 2) {
                        SetTextColor(hdc, RGB(220, 53, 69)); // Red
                    } else {
                        SetTextColor(hdc, RGB(108, 117, 125)); // Muted Gray
                    }
                } else {
                    SetTextColor(hdc, RGB(33, 37, 41)); // Dark Slate
                }
                return (LRESULT)g_hBodyBrush;
            }

            case WM_COMMAND: {
                int id = LOWORD(wp);
                if (id == IDC_BTN_CONNECT) {
                    StartConnectionProcess(hwnd);
                } else if (id == IDC_BTN_SCAN) {
                    SetStatus(L"Đang quét tìm kiếm Client trong mạng LAN...", 1);
                    DiscoveryScanner::TriggerScan();
                } else if (id == IDC_BTN_LOCALHOST) {
                    SetWindowTextW(g_hEditIp, L"127.0.0.1");
                    SendMessageW(g_hEditIp, EM_SETSEL, 0, -1);
                    SetFocus(g_hEditIp);
                } else if (id == IDC_BTN_EXIT) {
                    SendMessageW(hwnd, WM_CLOSE, 0, 0);
                }
                return 0;
            }

            case WM_NOTIFY: {
                LPNMHDR pnm = (LPNMHDR)lp;
                if (pnm->idFrom == IDC_LIST_CLIENTS) {
                    if (pnm->code == NM_CUSTOMDRAW) {
                        LPNMLVCUSTOMDRAW pLvcd = (LPNMLVCUSTOMDRAW)lp;
                        if (pLvcd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                            return CDRF_NOTIFYITEMDRAW;
                        }
                        if (pLvcd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                            return CDRF_NOTIFYSUBITEMDRAW;
                        }
                        if (pLvcd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                            if (pLvcd->iSubItem == 3) {
                                int itemIdx = (int)pLvcd->nmcd.dwItemSpec;
                                bool isSelected = (ListView_GetItemState(g_hListClients, itemIdx, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                                HDC hdc = pLvcd->nmcd.hdc;
                                RECT subRect;
                                ListView_GetSubItemRect(g_hListClients, itemIdx, 3, LVIR_BOUNDS, &subRect);

                                // Tô màu nền ô
                                HBRUSH bgBrush = isSelected ? GetSysColorBrush(COLOR_HIGHLIGHT) : GetSysColorBrush(COLOR_WINDOW);
                                FillRect(hdc, &subRect, bgBrush);

                                wchar_t textBuf[64] = {0};
                                ListView_GetItemText(g_hListClients, itemIdx, 3, textBuf, 64);
                                bool isBusy = (wcsstr(textBuf, L"bận") != nullptr || wcsstr(textBuf, L"Bận") != nullptr);

                                // Vẽ icon chấm tròn vector rõ nét (Xanh lá cây chuẩn cho Sẵn sàng, Vàng cho Đang bận)
                                int rowH = subRect.bottom - subRect.top;
                                int dotSize = (rowH > 22) ? 10 : 8;
                                int dotX = subRect.left + 8;
                                int dotY = subRect.top + (rowH - dotSize) / 2;

                                COLORREF dotFill = isBusy ? RGB(245, 158, 11) : RGB(34, 197, 94);
                                COLORREF dotBorder = isBusy ? RGB(217, 119, 6) : RGB(22, 163, 74);
                                HBRUSH dotBrush = CreateSolidBrush(dotFill);
                                HPEN dotPen = CreatePen(PS_SOLID, 1, dotBorder);
                                HGDIOBJ oldBrush = SelectObject(hdc, dotBrush);
                                HGDIOBJ oldPen = SelectObject(hdc, dotPen);
                                Ellipse(hdc, dotX, dotY, dotX + dotSize, dotY + dotSize);
                                SelectObject(hdc, oldBrush);
                                SelectObject(hdc, oldPen);
                                DeleteObject(dotBrush);
                                DeleteObject(dotPen);

                                // Vẽ chữ trạng thái
                                RECT textRect = subRect;
                                textRect.left = dotX + dotSize + 6;
                                SetBkMode(hdc, TRANSPARENT);
                                COLORREF textColor = isSelected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : (isBusy ? RGB(180, 83, 9) : RGB(21, 128, 61));
                                SetTextColor(hdc, textColor);
                                SelectObject(hdc, g_hFontEdit);
                                DrawTextW(hdc, isBusy ? L"Đang bận" : L"Sẵn sàng", -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

                                return CDRF_SKIPDEFAULT;
                            }
                            return CDRF_DODEFAULT;
                        }
                    } else if (pnm->code == NM_DBLCLK) {
                        // Click đúp vào một dòng -> Kết nối ngay!
                        int curSel = ListView_GetNextItem(g_hListClients, -1, LVNI_SELECTED);
                        if (curSel != -1) {
                            HandleSelectedRow(curSel, true);
                        }
                    } else if (pnm->code == NM_CLICK || pnm->code == LVN_ITEMCHANGED) {
                        // Click chọn một dòng -> Điền IP vào ô nhập thủ công
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
                    // Dọn dẹp máy Client quá 12 giây không còn phản hồi (đã tắt máy đột ngột/rút dây mạng)
                    DWORD now = GetTickCount();
                    bool listChanged = false;
                    for (auto it = g_discoveredList.begin(); it != g_discoveredList.end(); ) {
                        if (it->lastSeenTick > 0 && (now - it->lastSeenTick > 12000)) {
                            it = g_discoveredList.erase(it);
                            listChanged = true;
                        } else {
                            ++it;
                        }
                    }
                    if (listChanged) {
                        RefreshListViewUI();
                    }

                    // Tự động quét lại định kỳ mỗi 4 giây nếu không trong quá trình kết nối
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

                // Khử trùng lặp theo TÊN MÁY TÍNH (Computer Name) thay vì IP
                auto it = std::find_if(g_discoveredList.begin(), g_discoveredList.end(),
                    [&](const DiscoveredClient& c) {
                        return _wcsicmp(c.computerName.c_str(), pClient->computerName.c_str()) == 0;
                    });

                if (isOnline) {
                    if (it != g_discoveredList.end()) {
                        // Cập nhật thông tin máy đã có
                        it->userName = pClient->userName;
                        it->isBusy = pClient->isBusy;
                        it->lastSeenTick = GetTickCount();

                        // Ưu tiên IP card mạng LAN thật (192.168.x.x / 10.x.x.x) thay vì card ảo hay loopback
                        if (GetIpPriority(pClient->ip) >= GetIpPriority(it->ip)) {
                            it->ip = pClient->ip;
                        }
                    } else {
                        // Thêm máy mới vào danh sách ngay lập tức!
                        pClient->lastSeenTick = GetTickCount();
                        g_discoveredList.push_back(*pClient);
                    }
                } else {
                    // Máy phát tín hiệu tắt app (DISCOVERY_BYE) -> Xóa khỏi danh sách ngay!
                    if (it != g_discoveredList.end()) {
                        g_discoveredList.erase(it);
                    }
                }

                RefreshListViewUI();
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
                    SetStatus(errW, 2);
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
                if (g_hFontSubtitle) { DeleteObject(g_hFontSubtitle); g_hFontSubtitle = NULL; }
                if (g_hFontLabel) { DeleteObject(g_hFontLabel); g_hFontLabel = NULL; }
                if (g_hFontEdit) { DeleteObject(g_hFontEdit); g_hFontEdit = NULL; }
                if (g_hFontStatus) { DeleteObject(g_hFontStatus); g_hFontStatus = NULL; }
                if (g_hFontBtn) { DeleteObject(g_hFontBtn); g_hFontBtn = NULL; }
                if (g_hBodyBrush) { DeleteObject(g_hBodyBrush); g_hBodyBrush = NULL; }
                if (g_hHeaderBrush) { DeleteObject(g_hHeaderBrush); g_hHeaderBrush = NULL; }

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
        g_statusText = L"Đang tự động quét mạng LAN...";
        g_statusColorType = 1;
        g_discoveredList.clear();

        // Đăng ký lớp cửa sổ
        WNDCLASSW wc = {};
        wc.lpfnWndProc = ConnectWndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = L"LanRemoteConnectWnd";

        RegisterClassW(&wc);

        int winWidth = 645;
        int winHeight = 520;
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

        // Vòng lặp sự kiện của ConnectWindow
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
