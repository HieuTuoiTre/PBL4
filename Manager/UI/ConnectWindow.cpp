// ============================================================================
// File: ConnectWindow.cpp
// Mô tả: Hiện thực chi tiết giao diện bảng điều khiển kết nối mạng LAN (ConnectWindow).
// Bao gồm:
//   - Tự động quét và phát hiện các Client đang bật trong mạng LAN qua UDP Broadcast.
//   - Bảng danh sách SysListView32 với vẽ tùy biến vector (NM_CUSTOMDRAW) chấm trạng thái trực quan.
//   - Phân cấp ưu tiên địa chỉ IP (ưu tiên IP LAN thật 192.168.x.x hơn IP card ảo Docker/WSL).
//   - Vòng lặp tin nhắn cục bộ quản lý vòng đời cửa sổ kết nối độc lập.
// ============================================================================

// Nạp file header khai báo giao diện của ConnectWindow
#include "ConnectWindow.h"

// Nạp module quét tự động máy Client qua giao thức UDP
#include "../Core/DiscoveryScanner.h"

// Nạp các hàm tiện ích chuyển đổi bảng mã UTF-8 và UTF-16
#include "../../Shared/Utils.h"

// Nạp thư viện điều khiển giao diện phổ thông Windows Common Controls (SysListView32, ...)
#include <commctrl.h>

// Nạp các thư viện chuẩn C++ cho đa luồng, mảng động, chuỗi ký tự và thuật toán
#include <thread>
#include <vector>
#include <string>
#include <algorithm>

// Định nghĩa mã thông điệp tùy biến của ứng dụng để trao đổi giữa các luồng ngầm và UI
#define WM_CONNECT_RESULT (WM_APP + 201) // Thông điệp báo kết quả kết nối (thành công hoặc thất bại kèm chuỗi lỗi)
#define WM_CLIENT_UPDATE  (WM_APP + 202) // Thông điệp báo có máy Client mới trực tuyến hoặc vừa tắt máy
#define TIMER_SCAN_ID     1001           // Định danh bộ đếm thời gian tự động quét lại mạng LAN định kỳ

namespace {
    // ========================================================================
    // CÁC HANDLE ĐIỀU KHIỂN GIAO DIỆN CỤC BỘ (GUI HANDLES)
    // ========================================================================
    HWND g_hConnectWnd = NULL;    // Handle của cửa sổ chính ConnectWindow
    HWND g_hListClients = NULL;   // Handle của bảng danh sách máy Client (SysListView32)
    HWND g_hEditIp = NULL;        // Handle của ô nhập địa chỉ IP thủ công
    HWND g_hBtnConnect = NULL;    // Handle của nút bấm "Kết nối"
    HWND g_hBtnLocalhost = NULL;  // Handle của nút bấm tiện ích "Localhost" (127.0.0.1)
    HWND g_hBtnScan = NULL;       // Handle của nút bấm "Quét mạng" thủ công
    HWND g_hBtnExit = NULL;       // Handle của nút bấm "Thoát"
    HWND g_hStatusLabel = NULL;   // Handle của nhãn văn bản hiển thị trạng thái và thông báo lỗi

    // ========================================================================
    // TÀI NGUYÊN ĐỒ HỌA GDI (FONTS & BRUSHES)
    // ========================================================================
    HFONT g_hFontTitle = NULL;    // Font tiêu đề chính (Segoe UI 19pt, Bold)
    HFONT g_hFontSubtitle = NULL; // Font phụ đề hướng dẫn (Segoe UI 13pt)
    HFONT g_hFontLabel = NULL;    // Font nhãn phân mục (Segoe UI 14pt, Semi-bold)
    HFONT g_hFontEdit = NULL;     // Font ô nhập văn bản và nội dung bảng (Segoe UI 15pt)
    HFONT g_hFontStatus = NULL;   // Font thông báo trạng thái (Segoe UI 13pt)
    HFONT g_hFontBtn = NULL;      // Font nút bấm hành động chính (Segoe UI 14pt, Bold)

    HBRUSH g_hBodyBrush = NULL;   // Cọ vẽ màu nền thân cửa sổ (màu xám nhạt hiện đại RGB(245, 246, 248))
    HBRUSH g_hHeaderBrush = NULL; // Cọ vẽ màu nền banner tiêu đề phía trên (màu tối Dark Slate RGB(33, 37, 41))

    // ========================================================================
    // CÁC BIẾN QUẢN LÝ TRẠNG THÁI VÀ DỮ LIỆU
    // ========================================================================
    WNDPROC g_oldEditProc = NULL;                      // Con trỏ lưu hàm WndProc gốc của ô Edit trước khi subclass
    ConnectWindow::ConnectCallback g_callback = nullptr; // Callback thực hiện kết nối được truyền từ ManagerMain

    bool g_isOpen = false;        // Cờ theo dõi cửa sổ đang mở hay đã đóng
    bool g_success = false;       // Đánh dấu kết nối thành công để báo cho ManagerMain mở RemoteWindow
    bool g_isConnecting = false;  // Đang trong tiến trình thử kết nối TCP ngầm (vô hiệu hóa các nút tránh click lặp)
    std::string g_targetIP = "";  // Địa chỉ IP đang được yêu cầu kết nối
    std::string g_connectedIP = ""; // Địa chỉ IP đã kết nối thành công
    std::wstring g_statusText = L"Đang tự động quét mạng LAN..."; // Nội dung nhãn trạng thái hiện tại
    int g_statusColorType = 1;    // Phân loại màu trạng thái: 0: Xám thông thường, 1: Xanh dương thông tin, 2: Đỏ cảnh báo

    std::vector<DiscoveredClient> g_discoveredList; // Danh sách các máy Client tìm thấy trong mạng LAN

    // Định danh ID của các control trên giao diện
    enum ControlId {
        IDC_LIST_CLIENTS = 100, // ID của ListView danh sách máy
        IDC_EDIT_IP = 101,      // ID của ô nhập IP
        IDC_BTN_CONNECT = 102,  // ID của nút Kết nối
        IDC_BTN_LOCALHOST = 103,// ID của nút Localhost
        IDC_BTN_SCAN = 104,     // ID của nút Quét mạng
        IDC_BTN_EXIT = 105,     // ID của nút Thoát
        IDC_STATUS_LABEL = 106  // ID của nhãn trạng thái
    };

    // ========================================================================
    // HÀM ĐÁNH GIÁ MỨC ĐỘ ƯU TIÊN CỦA ĐỊA CHỈ IP
    // Giúp chọn card mạng LAN vật lý thật (Wi-Fi, dây LAN) thay vì card mạng ảo
    // ========================================================================
    int GetIpPriority(const std::string& ip) {
        // Dải 192.168.x.x (trừ card máy ảo VirtualBox 192.168.56.x) là mạng gia đình/văn phòng -> Ưu tiên cao nhất (3)
        if (ip.rfind("192.168.", 0) == 0 && ip.rfind("192.168.56.", 0) != 0) return 3;
        // Dải 10.x.x.x là mạng nội bộ doanh nghiệp -> Ưu tiên cao nhất (3)
        if (ip.rfind("10.", 0) == 0) return 3;
        // Dải 172.x.x.x thường là card ảo Docker/WSL -> Ưu tiên trung bình (2)
        if (ip.rfind("172.", 0) == 0) return 2;
        // Dải 192.168.56.x của VirtualBox Host-Only Adapter -> Ưu tiên trung bình (2)
        if (ip.rfind("192.168.56.", 0) == 0) return 2;
        // Dải loopback cục bộ 127.0.0.1 -> Ưu tiên thấp (1)
        if (ip == "127.0.0.1") return 1;
        // Các dải IP không xác định khác -> Ưu tiên thấp nhất (0)
        return 0;
    }

    // ========================================================================
    // HÀM SUBCLASS CHO Ô EDIT NHẬP ĐỊA CHỈ IP
    // Bắt sự kiện phím Enter để kích hoạt kết nối nhanh và phím Esc để thoát
    // ========================================================================
    LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == WM_KEYDOWN) {
            if (wParam == VK_RETURN) {
                // Nhấn Enter trong ô nhập -> Tự động bấm nút "Kết nối"
                SendMessageW(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(IDC_BTN_CONNECT, BN_CLICKED), 0);
                return 0;
            } else if (wParam == VK_ESCAPE) {
                // Nhấn phím Escape -> Gửi lệnh đóng cửa sổ
                SendMessageW(GetParent(hWnd), WM_CLOSE, 0, 0);
                return 0;
            }
        }
        // Các thông điệp bàn phím khác chuyển về thủ tục xử lý mặc định của Edit Box
        return CallWindowProcW(g_oldEditProc, hWnd, uMsg, wParam, lParam);
    }

    // Cập nhật nội dung và màu sắc của nhãn trạng thái giao diện
    void SetStatus(const std::wstring& text, int colorType) {
        g_statusText = text;
        g_statusColorType = colorType;
        if (g_hStatusLabel) {
            SetWindowTextW(g_hStatusLabel, text.c_str());
            InvalidateRect(g_hStatusLabel, NULL, TRUE); // Yêu cầu vẽ lại nhãn để cập nhật màu chữ
        }
    }

    // ========================================================================
    // CẬP NHẬT BẢNG DANH SÁCH MÁY CLIENT (REFRESH LISTVIEW UI)
    // Tự động bảo lưu vị trí dòng đang chọn của người dùng trong khi làm mới dữ liệu
    // ========================================================================
    void RefreshListViewUI() {
        if (!g_hListClients) return;

        // Lưu lại tên máy tính đang được người dùng bôi chọn trước khi xóa danh sách cũ
        std::wstring selectedComputer = L"";
        int curSel = ListView_GetNextItem(g_hListClients, -1, LVNI_SELECTED);
        if (curSel >= 0 && curSel < (int)g_discoveredList.size()) {
            selectedComputer = g_discoveredList[curSel].computerName;
        }

        // Tắt cờ vẽ lại của bảng để tránh hiện tượng nhấp nháy màn hình khi xóa và nạp lại dòng
        SendMessageW(g_hListClients, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(g_hListClients);

        int newSelIdx = -1; // Vị trí dòng mới của máy tính đã chọn
        for (int i = 0; i < (int)g_discoveredList.size(); i++) {
            const auto& client = g_discoveredList[i];

            // Cột 0: Tên máy tính
            LVITEMW lvi = {};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = i;
            lvi.iSubItem = 0;
            lvi.pszText = (LPWSTR)client.computerName.c_str();
            ListView_InsertItem(g_hListClients, &lvi);

            // Cột 1: Địa chỉ IP máy Client
            std::wstring ipW = Utils::Utf8ToWide(client.ip);
            ListView_SetItemText(g_hListClients, i, 1, (LPWSTR)ipW.c_str());

            // Cột 2: Tên tài khoản Windows đang đăng nhập trên Client
            ListView_SetItemText(g_hListClients, i, 2, (LPWSTR)client.userName.c_str());

            // Cột 3: Trạng thái bận hay sẵn sàng tiếp nhận điều khiển
            std::wstring statusStr = client.isBusy ? L"Đang bận" : L"Sẵn sàng";
            ListView_SetItemText(g_hListClients, i, 3, (LPWSTR)statusStr.c_str());

            // Nếu máy này trùng tên với máy đang được chọn trước đó, ghi nhận lại chỉ số dòng
            if (!selectedComputer.empty() && _wcsicmp(client.computerName.c_str(), selectedComputer.c_str()) == 0) {
                newSelIdx = i;
            }
        }

        // Khôi phục lại trạng thái bôi xanh dòng đã chọn
        if (newSelIdx != -1) {
            ListView_SetItemState(g_hListClients, newSelIdx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }

        // Bật lại cơ chế vẽ cho bảng và yêu cầu render cập nhật lên màn hình
        SendMessageW(g_hListClients, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(g_hListClients, NULL, TRUE);

        // Hiển thị thông báo hướng dẫn dưới thanh trạng thái
        if (!g_isConnecting) {
            if (g_discoveredList.empty()) {
                SetStatus(L"Chưa phát hiện máy Client nào trong mạng LAN. Bạn có thể nhấn 'Quét mạng' hoặc nhập IP thủ công.", 0);
            } else {
                std::wstring msg = L"Tìm thấy " + std::to_wstring(g_discoveredList.size()) + L" máy Client trong LAN. Click đúp vào máy để kết nối!";
                SetStatus(msg, 0);
            }
        }
    }

    // ========================================================================
    // TIẾN TRÌNH KHỞI TẠO KẾT NỐI (START CONNECTION PROCESS)
    // Đọc IP từ ô nhập và chạy luồng ngầm thực hiện bắt tay mạng TCP
    // ========================================================================
    void StartConnectionProcess(HWND hwnd) {
        if (g_isConnecting) return; // Nếu đang trong tiến trình kết nối thì không cho bấm thêm

        // Đọc chuỗi ký tự trong ô Edit IP
        wchar_t buf[256] = {0};
        GetWindowTextW(g_hEditIp, buf, 255);

        // Cắt bỏ các ký tự khoảng trắng thừa ở hai đầu chuỗi
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

        // Vô hiệu hóa tạm thời các nút bấm và ô nhập để ngăn người dùng thao tác loạn xạ
        EnableWindow(g_hBtnConnect, FALSE);
        EnableWindow(g_hBtnLocalhost, FALSE);
        EnableWindow(g_hBtnScan, FALSE);
        EnableWindow(g_hEditIp, FALSE);
        EnableWindow(g_hListClients, FALSE);

        SetStatus(L"Đang kết nối tới " + trimmed + L"... Vui lòng đợi.", 1);

        // Khởi chạy một luồng độc lập riêng biệt để thực hiện kết nối TCP mạng
        // Điều này đảm bảo giao diện đồ họa không bao giờ bị đơ (Not Responding)
        std::thread([hwnd, ipStr]() {
            std::string err;
            bool ok = false;
            if (g_callback) {
                // Gọi callback kết nối mạng (thực hiện kết nối 3 socket tới Client)
                ok = g_callback(ipStr, err);
            }
            // Cấp phát heap chuỗi lỗi để chuyển thông điệp an toàn qua luồng UI
            std::string* pErr = new std::string(err);
            PostMessageW(hwnd, WM_CONNECT_RESULT, ok ? 1 : 0, (LPARAM)pErr);
        }).detach();
    }

    // Xử lý khi người dùng click chuột vào một dòng máy trong bảng danh sách
    void HandleSelectedRow(int index, bool triggerConnect) {
        if (index < 0 || index >= (int)g_discoveredList.size()) return;

        // Tự động điền địa chỉ IP của máy được chọn vào ô nhập thủ công
        std::wstring ipW = Utils::Utf8ToWide(g_discoveredList[index].ip);
        SetWindowTextW(g_hEditIp, ipW.c_str());
        SendMessageW(g_hEditIp, EM_SETSEL, 0, -1); // Bôi đen toàn bộ chuỗi IP vừa điền

        // Nếu người dùng click đúp (Double-Click), lập tức tiến hành kết nối ngay
        if (triggerConnect) {
            StartConnectionProcess(g_hConnectWnd);
        }
    }

    // ========================================================================
    // HÀM XỬ LÝ THÔNG ĐIỆP CHÍNH CỦA CONNECTWINDOW (CONNECTWNDPROC)
    // ========================================================================
    LRESULT CALLBACK ConnectWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            // Khi cửa sổ vừa được tạo ra
            case WM_CREATE: {
                g_hConnectWnd = hwnd;

                // 1. Khởi tạo các phông chữ chuẩn Segoe UI theo từng kích thước
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

                // Tạo các cọ vẽ màu nền hiện đại
                g_hBodyBrush = CreateSolidBrush(RGB(245, 246, 248));
                g_hHeaderBrush = CreateSolidBrush(RGB(33, 37, 41));

                // 2. Tạo nhãn mô tả danh sách Client
                HWND hLblList = CreateWindowExW(0, L"STATIC", L"Danh sách máy Client đang bật trong mạng LAN:",
                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                    25, 82, 360, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
                SendMessageW(hLblList, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);

                // Nút "Quét mạng" thủ công
                g_hBtnScan = CreateWindowExW(0, L"BUTTON", L"🔍 Quét mạng",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    485, 76, 120, 28, hwnd, (HMENU)IDC_BTN_SCAN, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hBtnScan, WM_SETFONT, (WPARAM)g_hFontSubtitle, TRUE);

                // 3. Tạo bảng SysListView32 hiển thị danh sách máy
                g_hListClients = CreateWindowExW(WS_EX_CLIENTEDGE, L"SysListView32", L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                    25, 108, 580, 205, hwnd, (HMENU)IDC_LIST_CLIENTS, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hListClients, WM_SETFONT, (WPARAM)g_hFontEdit, TRUE);

                // Thiết lập kiểu hiển thị nâng cao cho bảng (chọn cả dòng, kẻ lưới, đệm đôi chống nhấp nháy)
                ListView_SetExtendedListViewStyle(g_hListClients,
                    LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

                // Thêm 4 cột thông tin cho bảng
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

                // 4. Tạo khu vực nhập IP thủ công
                HWND hLblManual = CreateWindowExW(0, L"STATIC", L"Hoặc nhập địa chỉ IP Client thủ công:",
                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                    25, 325, 300, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
                SendMessageW(hLblManual, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);

                g_hEditIp = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"127.0.0.1",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                    25, 348, 280, 28, hwnd, (HMENU)IDC_EDIT_IP, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hEditIp, WM_SETFONT, (WPARAM)g_hFontEdit, TRUE);

                // Gán hàm subclass cho ô Edit để nhận phím Enter và Esc
                g_oldEditProc = (WNDPROC)SetWindowLongPtrW(g_hEditIp, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);

                // Nút "Localhost" điền nhanh địa chỉ 127.0.0.1
                g_hBtnLocalhost = CreateWindowExW(0, L"BUTTON", L"Localhost",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    315, 348, 100, 28, hwnd, (HMENU)IDC_BTN_LOCALHOST, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hBtnLocalhost, WM_SETFONT, (WPARAM)g_hFontSubtitle, TRUE);

                // 5. Tạo nhãn hiển thị trạng thái và thông báo lỗi
                g_hStatusLabel = CreateWindowExW(0, L"STATIC", g_statusText.c_str(),
                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                    25, 390, 580, 35, hwnd, (HMENU)IDC_STATUS_LABEL, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hStatusLabel, WM_SETFONT, (WPARAM)g_hFontStatus, TRUE);

                // 6. Nút "Kết nối" (Nút mặc định nhận phím Enter: BS_DEFPUSHBUTTON)
                g_hBtnConnect = CreateWindowExW(0, L"BUTTON", L"Kết nối",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    190, 430, 130, 36, hwnd, (HMENU)IDC_BTN_CONNECT, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hBtnConnect, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

                // 7. Nút "Thoát"
                g_hBtnExit = CreateWindowExW(0, L"BUTTON", L"Thoát",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    335, 430, 105, 36, hwnd, (HMENU)IDC_BTN_EXIT, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hBtnExit, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);

                // 8. Đặt bộ đếm thời gian quét mạng tự động mỗi 4000ms (4 giây)
                SetTimer(hwnd, TIMER_SCAN_ID, 4000, NULL);

                // 9. Khởi chạy luồng DiscoveryScanner để nghe phản hồi UDP và gửi gói tin tìm kiếm ngay lập tức
                DiscoveryScanner::Start([hwnd](const DiscoveredClient& client, bool isOnline) {
                    DiscoveredClient* pCopy = new DiscoveredClient(client);
                    PostMessageW(hwnd, WM_CLIENT_UPDATE, isOnline ? 1 : 0, (LPARAM)pCopy);
                });

                return 0;
            }

            // Vẽ banner tiêu đề trên cùng với nền tối và chữ trắng sắc nét
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);

                RECT clientRect;
                GetClientRect(hwnd, &clientRect);

                // Vẽ dải banner phía trên (tọa độ Y: từ 0 đến 68 pixel)
                RECT headerRect = { 0, 0, clientRect.right, 68 };
                FillRect(hdc, &headerRect, g_hHeaderBrush);

                SetBkMode(hdc, TRANSPARENT);

                // Vẽ dòng tiêu đề lớn màu trắng
                SetTextColor(hdc, RGB(255, 255, 255));
                HFONT oldFont = (HFONT)SelectObject(hdc, g_hFontTitle);
                RECT titleRect = { 25, 12, clientRect.right - 25, 38 };
                DrawTextW(hdc, L"LAN REMOTE DESKTOP - QUẢN LÝ KẾT NỐI", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                // Vẽ dòng phụ đề nhỏ hướng dẫn màu xám sáng
                SetTextColor(hdc, RGB(173, 181, 189));
                SelectObject(hdc, g_hFontSubtitle);
                RECT subRect = { 25, 38, clientRect.right - 25, 60 };
                DrawTextW(hdc, L"Tự động phát hiện các máy Client trong mạng LAN & Kết nối điều khiển", -1, &subRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                // Vẽ đường kẻ viền phân cách giữa header và thân cửa sổ
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

            // Xóa màu nền cửa sổ bằng cọ màu xám nhạt hiện đại
            case WM_ERASEBKGND: {
                HDC hdc = (HDC)wp;
                RECT rc;
                GetClientRect(hwnd, &rc);
                FillRect(hdc, &rc, g_hBodyBrush);
                return 1;
            }

            // Tô màu chữ và nền cho các control tĩnh (STATIC controls)
            case WM_CTLCOLORSTATIC: {
                HDC hdc = (HDC)wp;
                HWND hCtrl = (HWND)lp;

                SetBkMode(hdc, TRANSPARENT); // Nền trong suốt hòa vào nền cửa sổ

                if (hCtrl == g_hStatusLabel) {
                    // Màu sắc tùy theo loại trạng thái
                    if (g_statusColorType == 1) {
                        SetTextColor(hdc, RGB(13, 110, 253)); // Xanh dương cho thông tin đang kết nối
                    } else if (g_statusColorType == 2) {
                        SetTextColor(hdc, RGB(220, 53, 69));  // Đỏ cho thông báo lỗi
                    } else {
                        SetTextColor(hdc, RGB(108, 117, 125)); // Xám cho hướng dẫn thông thường
                    }
                } else {
                    SetTextColor(hdc, RGB(33, 37, 41)); // Màu chữ xám đen cho các nhãn khác
                }
                return (LRESULT)g_hBodyBrush;
            }

            // Xử lý các sự kiện click nút bấm
            case WM_COMMAND: {
                int id = LOWORD(wp);
                if (id == IDC_BTN_CONNECT) {
                    StartConnectionProcess(hwnd); // Bấm nút "Kết nối"
                } else if (id == IDC_BTN_SCAN) {
                    SetStatus(L"Đang quét tìm kiếm Client trong mạng LAN...", 1);
                    DiscoveryScanner::TriggerScan(); // Bấm nút "Quét mạng"
                } else if (id == IDC_BTN_LOCALHOST) {
                    SetWindowTextW(g_hEditIp, L"127.0.0.1"); // Điền 127.0.0.1 vào ô nhập
                    SendMessageW(g_hEditIp, EM_SETSEL, 0, -1);
                    SetFocus(g_hEditIp);
                } else if (id == IDC_BTN_EXIT) {
                    SendMessageW(hwnd, WM_CLOSE, 0, 0); // Bấm nút "Thoát"
                }
                return 0;
            }

            // Xử lý các thông báo từ Common Controls (bảng danh sách ListView)
            case WM_NOTIFY: {
                LPNMHDR pnm = (LPNMHDR)lp;
                if (pnm->idFrom == IDC_LIST_CLIENTS) {
                    // Vẽ tùy biến (Custom Draw) cột Trạng thái với icon chấm tròn vector
                    if (pnm->code == NM_CUSTOMDRAW) {
                        LPNMLVCUSTOMDRAW pLvcd = (LPNMLVCUSTOMDRAW)lp;
                        if (pLvcd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                            return CDRF_NOTIFYITEMDRAW;
                        }
                        if (pLvcd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                            return CDRF_NOTIFYSUBITEMDRAW;
                        }
                        if (pLvcd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                            // Cột số 3 là cột "Trạng thái"
                            if (pLvcd->iSubItem == 3) {
                                int itemIdx = (int)pLvcd->nmcd.dwItemSpec;
                                bool isSelected = (ListView_GetItemState(g_hListClients, itemIdx, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                                HDC hdc = pLvcd->nmcd.hdc;
                                RECT subRect;
                                ListView_GetSubItemRect(g_hListClients, itemIdx, 3, LVIR_BOUNDS, &subRect);

                                // Tô màu nền của ô (màu xanh bôi chọn nếu dòng đang được chọn)
                                HBRUSH bgBrush = isSelected ? GetSysColorBrush(COLOR_HIGHLIGHT) : GetSysColorBrush(COLOR_WINDOW);
                                FillRect(hdc, &subRect, bgBrush);

                                wchar_t textBuf[64] = {0};
                                ListView_GetItemText(g_hListClients, itemIdx, 3, textBuf, 64);
                                bool isBusy = (wcsstr(textBuf, L"bận") != nullptr || wcsstr(textBuf, L"Bận") != nullptr);

                                // Vẽ icon chấm tròn vector rõ nét (Xanh lá cây chuẩn #22C55E cho Sẵn sàng, Vàng hổ phách cho Đang bận)
                                int rowH = subRect.bottom - subRect.top;
                                int dotSize = (rowH > 22) ? 10 : 8;
                                int dotX = subRect.left + 10;
                                int dotY = subRect.top + (rowH - dotSize) / 2;

                                COLORREF dotFill = isBusy ? RGB(245, 158, 11) : RGB(34, 197, 94);     // #22C55E Emerald green / Amber
                                COLORREF dotBorder = isBusy ? RGB(217, 119, 6) : RGB(22, 163, 74);   // #16A34A Darker border
                                HBRUSH dotBrush = CreateSolidBrush(dotFill);
                                HPEN dotPen = CreatePen(PS_SOLID, 1, dotBorder);
                                HGDIOBJ oldBrush = SelectObject(hdc, dotBrush);
                                HGDIOBJ oldPen = SelectObject(hdc, dotPen);
                                Ellipse(hdc, dotX, dotY, dotX + dotSize, dotY + dotSize);
                                SelectObject(hdc, oldBrush);
                                SelectObject(hdc, oldPen);
                                DeleteObject(dotBrush);
                                DeleteObject(dotPen);

                                // Vẽ chuỗi văn bản trạng thái ngay cạnh chấm tròn
                                RECT textRect = subRect;
                                textRect.left = dotX + dotSize + 8;
                                SetBkMode(hdc, TRANSPARENT);
                                COLORREF textColor = isSelected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : (isBusy ? RGB(180, 83, 9) : RGB(21, 128, 61));
                                SetTextColor(hdc, textColor);
                                HGDIOBJ oldF = SelectObject(hdc, g_hFontEdit);
                                DrawTextW(hdc, isBusy ? L"Đang bận" : L"Sẵn sàng", -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                                SelectObject(hdc, oldF);

                                return CDRF_SKIPDEFAULT; // Báo cho Windows biết ta đã tự vẽ xong ô này
                            }
                            return CDRF_DODEFAULT;
                        }
                    } else if (pnm->code == NM_DBLCLK) {
                        // Người dùng nhấp đúp (Double-Click) vào dòng -> Kết nối ngay lập tức!
                        int curSel = ListView_GetNextItem(g_hListClients, -1, LVNI_SELECTED);
                        if (curSel != -1) {
                            HandleSelectedRow(curSel, true);
                        }
                    } else if (pnm->code == NM_CLICK || pnm->code == LVN_ITEMCHANGED) {
                        // Người dùng nhấp chọn 1 dòng -> Tự động điền IP vào ô nhập thủ công
                        int curSel = ListView_GetNextItem(g_hListClients, -1, LVNI_SELECTED);
                        if (curSel != -1) {
                            HandleSelectedRow(curSel, false);
                        }
                    }
                }
                break;
            }

            // Bộ đếm thời gian kiểm tra và dọn dẹp các Client đã offline
            case WM_TIMER: {
                if (wp == TIMER_SCAN_ID) {
                    // Dọn dẹp máy Client quá 12 giây không còn phản hồi tín hiệu (đã rút mạng hoặc tắt máy đột ngột)
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

                    // Tự động phát lại gói tin quét mạng mỗi 4 giây nếu không trong quá trình kết nối
                    if (!g_isConnecting) {
                        DiscoveryScanner::TriggerScan();
                    }
                }
                return 0;
            }

            // Nhận tín hiệu cập nhật máy Client từ luồng ngầm UDP
            case WM_CLIENT_UPDATE: {
                bool isOnline = (wp == 1);
                DiscoveredClient* pClient = (DiscoveredClient*)lp;
                if (!pClient) return 0;

                // Khử trùng lặp theo TÊN MÁY TÍNH (Computer Name) thay vì IP để tránh lặp máy có nhiều card mạng
                auto it = std::find_if(g_discoveredList.begin(), g_discoveredList.end(),
                    [&](const DiscoveredClient& c) {
                        return _wcsicmp(c.computerName.c_str(), pClient->computerName.c_str()) == 0;
                    });

                if (isOnline) {
                    if (it != g_discoveredList.end()) {
                        // Cập nhật thông tin của máy đã có trong danh sách
                        it->userName = pClient->userName;
                        it->isBusy = pClient->isBusy;
                        it->lastSeenTick = GetTickCount();

                        // Ưu tiên IP card mạng LAN thật (192.168.x.x / 10.x.x.x) thay vì card ảo hay loopback
                        if (GetIpPriority(pClient->ip) >= GetIpPriority(it->ip)) {
                            it->ip = pClient->ip;
                        }
                    } else {
                        // Thêm máy Client mới phát hiện vào danh sách ngay lập tức
                        pClient->lastSeenTick = GetTickCount();
                        g_discoveredList.push_back(*pClient);
                    }
                } else {
                    // Máy Client phát tín hiệu tắt ứng dụng (DISCOVERY_BYE) -> Xóa khỏi danh sách ngay!
                    if (it != g_discoveredList.end()) {
                        g_discoveredList.erase(it);
                    }
                }

                // Cập nhật lại giao diện danh sách
                RefreshListViewUI();
                delete pClient; // Giải phóng con trỏ bộ nhớ động đã cấp phát từ luồng phụ
                return 0;
            }

            // Nhận kết quả từ luồng thử nghiệm kết nối TCP
            case WM_CONNECT_RESULT: {
                g_isConnecting = false;
                bool ok = (wp == 1);
                std::string* pErr = (std::string*)lp;

                if (ok) {
                    // Kết nối thành công -> Ghi nhận IP và đóng cửa sổ ConnectWindow để mở RemoteWindow
                    g_connectedIP = g_targetIP;
                    g_success = true;
                    g_isOpen = false;
                    DestroyWindow(hwnd);
                } else {
                    // Kết nối thất bại -> Bật lại các nút bấm và hiển thị thông báo lỗi chi tiết
                    EnableWindow(g_hBtnConnect, TRUE);
                    EnableWindow(g_hBtnLocalhost, TRUE);
                    EnableWindow(g_hBtnScan, TRUE);
                    EnableWindow(g_hEditIp, TRUE);
                    EnableWindow(g_hListClients, TRUE);

                    std::wstring errW = L"Không thể kết nối tới Client!";
                    if (pErr && !pErr->empty()) {
                        errW = Utils::Utf8ToWide(*pErr);
                    }
                    SetStatus(errW, 2); // Hiển thị màu đỏ
                    SetFocus(g_hEditIp);
                }

                if (pErr) delete pErr; // Giải phóng con trỏ chuỗi lỗi
                return 0;
            }

            // Người dùng bấm nút X đóng cửa sổ
            case WM_CLOSE: {
                g_isOpen = false;
                g_success = false;
                DestroyWindow(hwnd);
                return 0;
            }

            // Hủy cửa sổ và giải phóng toàn bộ tài nguyên hệ điều hành
            case WM_DESTROY: {
                KillTimer(hwnd, TIMER_SCAN_ID); // Hủy Timer quét mạng
                DiscoveryScanner::Stop();       // Dừng lắng nghe UDP socket

                // Giải phóng các phông chữ GDI
                if (g_hFontTitle) { DeleteObject(g_hFontTitle); g_hFontTitle = NULL; }
                if (g_hFontSubtitle) { DeleteObject(g_hFontSubtitle); g_hFontSubtitle = NULL; }
                if (g_hFontLabel) { DeleteObject(g_hFontLabel); g_hFontLabel = NULL; }
                if (g_hFontEdit) { DeleteObject(g_hFontEdit); g_hFontEdit = NULL; }
                if (g_hFontStatus) { DeleteObject(g_hFontStatus); g_hFontStatus = NULL; }
                if (g_hFontBtn) { DeleteObject(g_hFontBtn); g_hFontBtn = NULL; }

                // Giải phóng các cọ vẽ GDI
                if (g_hBodyBrush) { DeleteObject(g_hBodyBrush); g_hBodyBrush = NULL; }
                if (g_hHeaderBrush) { DeleteObject(g_hHeaderBrush); g_hHeaderBrush = NULL; }

                g_hConnectWnd = NULL;
                g_hListClients = NULL;
                PostMessageW(NULL, WM_NULL, 0, 0); // Đánh thức vòng lặp tin nhắn nếu đang chờ
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

namespace ConnectWindow {
    // Hiện thực hàm Show hiển thị cửa sổ kết nối
    bool Show(HINSTANCE hInstance, std::string& outSelectedIP, ConnectCallback onConnect) {
        g_callback = onConnect;
        g_success = false;
        g_isOpen = true;
        g_isConnecting = false;
        g_statusText = L"Đang tự động quét mạng LAN...";
        g_statusColorType = 1;
        g_discoveredList.clear();

        // Đăng ký lớp cửa sổ tùy biến LanRemoteConnectWnd
        WNDCLASSW wc = {};
        wc.lpfnWndProc = ConnectWndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = L"LanRemoteConnectWnd";

        RegisterClassW(&wc);

        // Tính toán kích thước và đặt cửa sổ xuất hiện chính giữa màn hình desktop
        int winWidth = 645;
        int winHeight = 520;
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        int posX = (screenW - winWidth) / 2;
        int posY = (screenH - winHeight) / 2;

        // Tạo cửa sổ với đầy đủ thanh tiêu đề và nút thu nhỏ
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

        // Vòng lặp lấy và điều phối thông điệp riêng cho ConnectWindow
        MSG msg;
        while (g_isOpen && GetMessageW(&msg, NULL, 0, 0)) {
            // IsDialogMessageW hỗ trợ phím Tab chuyển đổi giữa các nút và ô nhập
            if (!IsDialogMessageW(hwnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        // Nếu kết nối thành công, trả về IP đã kết nối
        if (g_success) {
            outSelectedIP = g_connectedIP;
            return true;
        }
        return false;
    }
}
