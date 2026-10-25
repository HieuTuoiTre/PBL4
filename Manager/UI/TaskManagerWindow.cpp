// ============================================================================
// File: TaskManagerWindow.cpp
// Mô tả: Hiện thực chi tiết giao diện cửa sổ Task Manager quản lý tác vụ từ xa.
// Bao gồm:
//   - Dashboard 4 thẻ chỉ số phần cứng (CPU, RAM, Đĩa, Tổng tiến trình) vẽ bằng GDI+ đệm đôi chống nhấp nháy.
//   - Thanh tìm kiếm tiến trình theo tên hoặc PID với bộ lọc không phân biệt hoa thường.
//   - Bảng SysListView32 cập nhật tại chỗ (In-Place Update) bảo lưu dòng chọn và vị trí thanh cuộn.
//   - Cơ chế xác nhận an toàn trước khi gửi lệnh kết thúc tác vụ (Kill Process) qua mạng.
// ============================================================================

// Nạp file header khai báo giao diện của TaskManagerWindow
#include "TaskManagerWindow.h"

// Nạp hàm tiện ích chuyển đổi bảng mã UTF-8 sang Unicode UTF-16
#include "../../Shared/Utils.h"

// Nạp thư viện điều khiển giao diện phổ thông Windows Common Controls (SysListView32)
#include <commctrl.h>

// Nạp thư viện đồ họa GDI+ cho Dashboard chỉ số
#include <gdiplus.h>

// Nạp các thư viện chuẩn C++ cho mảng động, chuỗi ký tự, thuật toán và xử lý chuỗi Unicode rộng
#include <vector>
#include <string>
#include <algorithm>
#include <cwchar>
#include <cmath>

// Định nghĩa các mã thông điệp nội bộ của ứng dụng để đồng bộ giữa luồng mạng và luồng giao diện
#define WM_TASK_UPDATE_SYS  (WM_APP + 101) // Thông điệp yêu cầu vẽ lại các thẻ chỉ số CPU/RAM/Đĩa
#define WM_TASK_UPDATE_LIST (WM_APP + 102) // Thông điệp yêu cầu làm mới bảng danh sách tiến trình

// Định danh các thành phần điều khiển (Control IDs)
#define IDC_TASK_LIST    1001 // ID của bảng SysListView32
#define IDC_TASK_REFRESH 1002 // ID của nút "Làm mới (F5)"
#define IDC_TASK_KILL    1003 // ID của nút "Kết thúc tác vụ"
#define IDC_TASK_SEARCH  1004 // ID của ô tìm kiếm tiến trình

namespace {
    // ========================================================================
    // CÁC HANDLE GIAO DIỆN VÀ BIẾN ĐỒNG BỘ DỮ LIỆU
    // ========================================================================
    HWND g_hTaskWnd = NULL;      // Handle cửa sổ TaskManagerWindow
    HWND g_hTaskList = NULL;     // Handle bảng danh sách tiến trình (SysListView32)
    HWND g_hSearchEdit = NULL;   // Handle ô nhập từ khóa tìm kiếm
    HWND g_hBtnRefresh = NULL;   // Handle nút "Làm mới"
    HWND g_hBtnKill = NULL;      // Handle nút "Kết thúc tác vụ"
    HWND g_hStatusLabel = NULL;  // Handle nhãn trạng thái tổng số tiến trình dưới chân cửa sổ
    TcpClient* g_client = nullptr; // Kênh TCP Điều khiển để gửi lệnh truy vấn và lệnh tắt tiến trình

    CRITICAL_SECTION g_dataCs;   // Khóa đồng bộ bảo vệ cấu trúc dữ liệu tiến trình giữa luồng mạng và luồng UI
    bool g_csInit = false;       // Cờ đánh dấu đã khởi tạo Critical Section hay chưa
    Protocol::SysInfoPayload g_latestSys = {};                         // Dữ liệu chỉ số phần cứng mới nhất nhận được
    std::vector<Protocol::ProcessInfoPayload> g_latestProcesses;        // Danh sách tiến trình đầy đủ mới nhất nhận được
    std::wstring g_searchQuery = L"";                                   // Từ khóa lọc tiến trình hiện tại

    // ========================================================================
    // HỖ TRỢ DYNAMIC DPI SCALING
    // ========================================================================
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

    // Tạo đường dẫn hình chữ nhật bo góc cho các thẻ Dashboard
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
    // XỬ LÝ LỆNH KẾT THÚC TIẾN TRÌNH (KILL PROCESS)
    // Hiển thị hộp thoại xác nhận rõ ràng trước khi gửi lệnh tắt tiến trình sang Client
    // ========================================================================
    void TriggerKillSelectedProcess(HWND hwnd) {
        if (!g_hTaskList || !g_client) return;

        // Lấy chỉ số dòng đang được người dùng bôi chọn trong bảng
        int sel = (int)SendMessageW(g_hTaskList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
        if (sel == -1) {
            MessageBoxW(hwnd, L"Vui lòng nhấp chọn một tiến trình trong bảng trước khi bấm kết thúc tác vụ!", L"Thông báo", MB_OK | MB_ICONINFORMATION);
            return;
        }

        wchar_t nameBuf[256] = {0};
        wchar_t pidBuf[32] = {0};
        LVITEMW lvi = {};
        lvi.iSubItem = 0; lvi.pszText = nameBuf; lvi.cchTextMax = 256;
        SendMessageW(g_hTaskList, LVM_GETITEMTEXTW, sel, (LPARAM)&lvi); // Lấy tên tiến trình
        lvi.iSubItem = 1; lvi.pszText = pidBuf; lvi.cchTextMax = 32;
        SendMessageW(g_hTaskList, LVM_GETITEMTEXTW, sel, (LPARAM)&lvi); // Lấy mã định danh PID

        if (wcslen(pidBuf) > 0) {
            wchar_t* endPtr = nullptr;
            uint32_t pid = (uint32_t)wcstoul(pidBuf, &endPtr, 10);
            if (endPtr != pidBuf && pid > 0) {
                // Hiển thị hộp thoại xác nhận để tránh bấm nhầm tắt ứng dụng quan trọng
                std::wstring confirmMsg = L"Bạn có chắc chắn muốn tắt (Kill) tiến trình '" + std::wstring(nameBuf) + L"' (PID: " + std::wstring(pidBuf) + L") không?";
                if (MessageBoxW(hwnd, confirmMsg.c_str(), L"Xác nhận kết thúc tác vụ", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
                    // Gửi gói tin MSG_KILL_PROCESS chứa mã PID sang Client
                    g_client->SendPacket(Protocol::MSG_KILL_PROCESS, (char*)&pid, sizeof(pid));

                    // Gửi yêu cầu cập nhật lại danh sách tiến trình và chỉ số hệ thống ngay lập tức
                    g_client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
                    g_client->SendPacket(Protocol::MSG_SYS_INFO_REQUEST, nullptr, 0);
                }
            }
        }
    }

    // ========================================================================
    // CẬP NHẬT DANH SÁCH TIẾN TRÌNH LÊN BẢNG (IN-PLACE LISTVIEW UPDATE)
    // Tự động lọc từ khóa tìm kiếm và giữ nguyên vị trí dòng chọn, không giật màn hình
    // ========================================================================
    void RenderProcessListOnUI() {
        if (!g_hTaskWnd || !g_hTaskList) return;

        // Sao chép danh sách tiến trình an toàn dưới bảo vệ của Critical Section
        EnterCriticalSection(&g_dataCs);
        std::vector<Protocol::ProcessInfoPayload> fullList = g_latestProcesses;
        LeaveCriticalSection(&g_dataCs);

        // Lọc danh sách tiến trình theo từ khóa tìm kiếm (so sánh chữ thường không phân biệt hoa/thường)
        std::vector<Protocol::ProcessInfoPayload> pList;
        std::wstring lowerQuery = g_searchQuery;
        std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::towlower);

        for (const auto& item : fullList) {
            if (lowerQuery.empty()) {
                pList.push_back(item);
            } else {
                std::wstring nameW = Utils::Utf8ToWide(item.name);
                std::wstring lowerName = nameW;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);
                if (lowerName.find(lowerQuery) != std::wstring::npos) {
                    pList.push_back(item);
                }
            }
        }

        int count = (int)pList.size();

        // 1. Lưu lại PID của tiến trình đang được chọn để khôi phục lại sau khi làm mới
        std::wstring selectedPid = L"";
        int curSel = (int)SendMessageW(g_hTaskList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
        if (curSel != -1) {
            wchar_t buf[32] = {0};
            LVITEMW lvi = {};
            lvi.iSubItem = 1; lvi.pszText = buf; lvi.cchTextMax = 32;
            SendMessageW(g_hTaskList, LVM_GETITEMTEXTW, curSel, (LPARAM)&lvi);
            selectedPid = buf;
        }

        // 2. Khóa cờ vẽ của ListView để cập nhật tại chỗ mà không làm nhảy thanh cuộn
        SendMessageW(g_hTaskList, WM_SETREDRAW, FALSE, 0);

        int existingCount = (int)SendMessageW(g_hTaskList, LVM_GETITEMCOUNT, 0, 0);

        for (int i = 0; i < count; i++) {
            std::wstring nameW = Utils::Utf8ToWide(pList[i].name);
            std::wstring pidW = std::to_wstring(pList[i].pid);
            std::wstring ramW = std::to_wstring(pList[i].ramUsageMB) + L" MB";
            std::wstring diskW = std::to_wstring(pList[i].diskUsageMB) + L" MB";

            LVITEMW lvi = {};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = i;
            lvi.iSubItem = 0;
            lvi.pszText = (LPWSTR)nameW.c_str();

            // Nếu số lượng tiến trình tăng thì chèn dòng mới, ngược lại thì ghi đè lên dòng đã có
            if (i >= existingCount) {
                SendMessageW(g_hTaskList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
            } else {
                SendMessageW(g_hTaskList, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);
            }

            // Ghi nhận PID (cột 1)
            lvi.iSubItem = 1;
            lvi.pszText = (LPWSTR)pidW.c_str();
            SendMessageW(g_hTaskList, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);

            // Ghi nhận RAM (cột 2)
            lvi.iSubItem = 2;
            lvi.pszText = (LPWSTR)ramW.c_str();
            SendMessageW(g_hTaskList, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);

            // Ghi nhận Đĩa (cột 3)
            lvi.iSubItem = 3;
            lvi.pszText = (LPWSTR)diskW.c_str();
            SendMessageW(g_hTaskList, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);

            // Khôi phục lại trạng thái bôi chọn nếu PID trùng khớp
            if (!selectedPid.empty() && pidW == selectedPid) {
                LVITEMW stateItem = {};
                stateItem.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
                stateItem.state = LVIS_SELECTED | LVIS_FOCUSED;
                SendMessageW(g_hTaskList, LVM_SETITEMSTATE, i, (LPARAM)&stateItem);
            }
        }

        // Xóa các dòng thừa nếu số lượng tiến trình giảm bớt
        for (int i = existingCount - 1; i >= count; i--) {
            SendMessageW(g_hTaskList, LVM_DELETEITEM, i, 0);
        }

        // Bật lại cơ chế vẽ cho ListView
        SendMessageW(g_hTaskList, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(g_hTaskList, NULL, FALSE);

        // Cập nhật nhãn trạng thái tổng số tiến trình dưới chân trang
        if (g_hStatusLabel) {
            std::wstring statusStr = L"Hiển thị: " + std::to_wstring(count) + L" / " + std::to_wstring(fullList.size()) + L" tiến trình";
            if (!g_searchQuery.empty()) {
                statusStr += L" (Lọc: \"" + g_searchQuery + L"\")";
            }
            statusStr += L" | F5: Làm mới | Phím Del hoặc Nhấp đúp: Kết thúc tác vụ";
            SetWindowTextW(g_hStatusLabel, statusStr.c_str());
        }
    }

    // ========================================================================
    // VẼ DASHBOARD CHỈ SỐ HỆ THỐNG (PAINT HEADER CARDS)
    // 4 thẻ tròn bo góc: CPU, RAM, ĐĨA (DISK), TIẾN TRÌNH với thanh trạng thái mini
    // ========================================================================
    void PaintHeaderCards(HWND hwnd, HDC hdc, int width, int headerH) {
        float scale = GetDpiScale(hwnd);
        if (width <= 0 || headerH <= 0) return;

        // Double-buffering toàn bộ Header bằng Bitmap trong bộ nhớ tránh chớp giật
        Gdiplus::Bitmap memBmp(width, headerH, PixelFormat32bppPARGB);
        Gdiplus::Graphics g(&memBmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        // Nền Header Dashboard: Slate Graphite tối RGB(24, 25, 30)
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 24, 25, 30));
        g.FillRectangle(&bgBrush, 0, 0, width, headerH);

        // Viền dưới 1px phân cách Header với khu vực bảng danh sách
        Gdiplus::Pen borderPen(Gdiplus::Color(255, 45, 47, 56), 1.0f);
        g.DrawLine(&borderPen, 0.0f, (float)(headerH - 1), (float)width, (float)(headerH - 1));

        // Lấy số liệu mới nhất
        EnterCriticalSection(&g_dataCs);
        Protocol::SysInfoPayload sys = g_latestSys;
        int procCount = (int)g_latestProcesses.size();
        LeaveCriticalSection(&g_dataCs);

        float padX = 10.0f * scale;
        float cardGap = 8.0f * scale;
        float cardY = 8.0f * scale;
        float cardH = (float)headerH - 16.0f * scale;
        float totalGaps = cardGap * 3.0f + padX * 2.0f;
        float cardW = (width - totalGaps) / 4.0f;
        if (cardW < 80.0f) cardW = 80.0f;

        // Sử dụng UnitPixel thay vì UnitPoint để không bao giờ bị nhân đôi DPI dẫn tới cắt chữ
        Gdiplus::FontFamily fontFamily(L"Segoe UI");
        Gdiplus::Font fontTitle(&fontFamily, 11.5f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font fontVal(&fontFamily, 19.0f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

        struct StatCard {
            std::wstring title;
            std::wstring value;
            int percent;
            Gdiplus::Color accentColor;
        };

        // Bảng màu trạng thái: Xanh lá (tốt), Vàng (cảnh báo > 65%), Đỏ (nguy hiểm > 85%)
        Gdiplus::Color green(255, 34, 197, 94);
        Gdiplus::Color yellow(255, 234, 179, 8);
        Gdiplus::Color red(255, 239, 68, 68);
        Gdiplus::Color blue(255, 59, 130, 246);

        auto GetUsageColor = [&](int p) {
            if (p >= 85) return red;
            if (p >= 65) return yellow;
            return green;
        };

        // 4 thẻ hiển thị thông số hệ thống
        StatCard cards[4] = {
            { L"CPU", std::to_wstring(sys.cpuUsage) + L"%", sys.cpuUsage, GetUsageColor(sys.cpuUsage) },
            { L"RAM", std::to_wstring(sys.ramUsage) + L"%", sys.ramUsage, GetUsageColor(sys.ramUsage) },
            { L"ĐĨA (DISK)", std::to_wstring(sys.diskUsage) + L"%", sys.diskUsage, GetUsageColor(sys.diskUsage) },
            { L"TIẾN TRÌNH", std::to_wstring(procCount), -1, blue }
        };

        Gdiplus::GraphicsPath path;
        float radius = 6.0f * scale;

        Gdiplus::StringFormat sf;
        sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentNear);
        sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

        for (int i = 0; i < 4; i++) {
            float cx = padX + i * (cardW + cardGap);
            Gdiplus::RectF cr(cx, cardY, cardW, cardH);

            AddRoundRectPath(path, cr, radius);
            Gdiplus::SolidBrush cardBg(Gdiplus::Color(255, 34, 36, 44));
            g.FillPath(&cardBg, &path);
            Gdiplus::Pen cardBorder(Gdiplus::Color(255, 52, 55, 68), 1.0f);
            g.DrawPath(&cardBorder, &path);

            // 1. Tiêu đề thẻ (rõ ràng, không bị xén cạnh)
            Gdiplus::RectF titleRect(cx + 10.0f * scale, cardY + 7.0f * scale, cardW - 20.0f * scale, 16.0f * scale);
            Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 156, 163, 175));
            g.DrawString(cards[i].title.c_str(), -1, &fontTitle, titleRect, &sf, &titleBrush);

            // 2. Giá trị số đo lớn nổi bật với màu cảnh báo tương ứng
            Gdiplus::RectF valRect(cx + 10.0f * scale, cardY + 25.0f * scale, cardW - 20.0f * scale, 24.0f * scale);
            Gdiplus::SolidBrush valBrush(cards[i].accentColor);
            g.DrawString(cards[i].value.c_str(), -1, &fontVal, valRect, &sf, &valBrush);

            // 3. Mini progress bar nằm ở đáy thẻ
            if (cards[i].percent >= 0) {
                float barH = 4.0f * scale;
                float barY = cardY + cardH - barH - 6.0f * scale;
                float barW = cardW - 20.0f * scale;
                Gdiplus::SolidBrush barBg(Gdiplus::Color(255, 48, 51, 62));
                g.FillRectangle(&barBg, cx + 10.0f * scale, barY, barW, barH);

                float fillW = barW * (float)cards[i].percent / 100.0f;
                if (fillW > barW) fillW = barW;
                Gdiplus::SolidBrush barFill(cards[i].accentColor);
                g.FillRectangle(&barFill, cx + 10.0f * scale, barY, fillW, barH);
            }
        }

        // Sao chép từ bộ nhớ ra màn hình
        Gdiplus::Graphics gScreen(hdc);
        gScreen.DrawImage(&memBmp, 0, 0);
    }

    // ========================================================================
    // HÀM XỬ LÝ THÔNG ĐIỆP CHÍNH CỦA CỬA SỔ TASK MANAGER (TASKWNDPROC)
    // ========================================================================
    LRESULT CALLBACK TaskWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        float scale = GetDpiScale(hwnd);
        int headerH = (int)std::round(80.0f * scale);

        switch (msg) {
            // Khởi tạo các control con khi cửa sổ vừa tạo
            case WM_CREATE: {
                g_hTaskWnd = hwnd;

                // 1. Ô tìm kiếm tiến trình
                g_hSearchEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_TASK_SEARCH, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hSearchEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Tìm kiếm tiến trình (Tên hoặc PID)...");

                // 2. Nút Làm mới (F5)
                g_hBtnRefresh = CreateWindowExW(0, L"BUTTON", L"Làm mới (F5)",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_TASK_REFRESH, GetModuleHandle(NULL), NULL);

                // 3. Nút Kết thúc tác vụ
                g_hBtnKill = CreateWindowExW(0, L"BUTTON", L"Kết thúc tác vụ",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_TASK_KILL, GetModuleHandle(NULL), NULL);

                // 4. Bảng danh sách tiến trình (SysListView32)
                g_hTaskList = CreateWindowExW(0, L"SysListView32", L"",
                    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_TASK_LIST, GetModuleHandle(NULL), NULL);

                SendMessageW(g_hTaskList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                    LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

                // Thêm 4 cột cho bảng tiến trình
                LVCOLUMNW lvc = { LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM, LVCFMT_LEFT, (int)(260 * scale), (LPWSTR)L"Tên Tiến Trình", 0, 0 };
                SendMessageW(g_hTaskList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);
                lvc.cx = (int)(75 * scale); lvc.pszText = (LPWSTR)L"PID"; SendMessageW(g_hTaskList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);
                lvc.cx = (int)(100 * scale); lvc.fmt = LVCFMT_RIGHT; lvc.pszText = (LPWSTR)L"RAM"; SendMessageW(g_hTaskList, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);
                lvc.cx = (int)(105 * scale); lvc.fmt = LVCFMT_RIGHT; lvc.pszText = (LPWSTR)L"Đĩa (MB/s)"; SendMessageW(g_hTaskList, LVM_INSERTCOLUMNW, 3, (LPARAM)&lvc);

                // 5. Nhãn trạng thái dưới đáy
                g_hStatusLabel = CreateWindowExW(0, L"STATIC", L"Đang tải dữ liệu tiến trình...",
                    WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
                    0, 0, 0, 0, hwnd, NULL, GetModuleHandle(NULL), NULL);

                // Gán phông chữ Segoe UI sắc nét
                HFONT hFont = CreateFontW((int)(-12 * scale), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
                HFONT hFontBtn = CreateFontW((int)(-12 * scale), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
                SendMessageW(g_hSearchEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
                SendMessageW(g_hBtnRefresh, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
                SendMessageW(g_hBtnKill, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
                SendMessageW(g_hStatusLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

                // Gửi yêu cầu dữ liệu ban đầu
                if (g_client) {
                    g_client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
                    g_client->SendPacket(Protocol::MSG_SYS_INFO_REQUEST, nullptr, 0);
                }
                SetTimer(hwnd, 1, 3000, NULL); // Đặt lịch tự động làm mới ngầm mỗi 3 giây
                return 0;
            }

            // Giới hạn kích thước tối thiểu của cửa sổ (không cho co nhỏ quá mức làm mất bố cục)
            case WM_GETMINMAXINFO: {
                MINMAXINFO* pMMI = (MINMAXINFO*)lp;
                pMMI->ptMinTrackSize.x = (LONG)std::round(660.0f * scale);
                pMMI->ptMinTrackSize.y = (LONG)std::round(480.0f * scale);
                return 0;
            }

            // Vẽ Dashboard phía trên khi có yêu cầu cập nhật
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT r;
                GetClientRect(hwnd, &r);
                if (ps.rcPaint.top < headerH) {
                    PaintHeaderCards(hwnd, hdc, r.right, headerH);
                }
                EndPaint(hwnd, &ps);
                return 0;
            }

            // Bộ đếm thời gian tự động yêu cầu cập nhật tiến trình mỗi 3000ms
            case WM_TIMER:
                if (g_client) {
                    g_client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
                    g_client->SendPacket(Protocol::MSG_SYS_INFO_REQUEST, nullptr, 0);
                }
                return 0;

            // Xử lý thay đổi kích thước cửa sổ (Responsive Layout)
            case WM_SIZE: {
                int w = LOWORD(lp);
                int h = HIWORD(lp);
                int actionH = (int)std::round(38.0f * scale);
                int statusH = (int)std::round(22.0f * scale);
                int gap = (int)std::round(8.0f * scale);
                int btnH = (int)std::round(30.0f * scale);

                // Định vị các nút bấm và ô tìm kiếm
                int actionY = headerH + (actionH - btnH) / 2;
                int btnRefW = (int)std::round(115.0f * scale);
                int btnKillW = (int)std::round(135.0f * scale);

                int killX = w - gap - btnKillW;
                int refX = killX - gap - btnRefW;
                int searchW = refX - gap - gap;
                if (searchW < (int)(150 * scale)) searchW = (int)(150 * scale);

                MoveWindow(g_hSearchEdit, gap, actionY, searchW, btnH, TRUE);
                MoveWindow(g_hBtnRefresh, refX, actionY, btnRefW, btnH, TRUE);
                MoveWindow(g_hBtnKill, killX, actionY, btnKillW, btnH, TRUE);

                // Định vị bảng danh sách tiến trình
                int listY = headerH + actionH;
                int listH = h - listY - statusH - gap;
                if (listH < 50) listH = 50;
                MoveWindow(g_hTaskList, gap, listY, w - gap * 2, listH, TRUE);

                // Tự động căn chỉnh độ rộng cột sao cho cột Tên Tiến Trình mở rộng phủ kín màn hình
                int colPidW = (int)std::round(75.0f * scale);
                int colRamW = (int)std::round(100.0f * scale);
                int colDiskW = (int)std::round(105.0f * scale);
                int scrollW = GetSystemMetrics(SM_CXVSCROLL) + 4;
                int colNameW = (w - gap * 2) - colPidW - colRamW - colDiskW - scrollW;
                if (colNameW < (int)(180.0f * scale)) colNameW = (int)(180.0f * scale);

                ListView_SetColumnWidth(g_hTaskList, 0, colNameW);
                ListView_SetColumnWidth(g_hTaskList, 1, colPidW);
                ListView_SetColumnWidth(g_hTaskList, 2, colRamW);
                ListView_SetColumnWidth(g_hTaskList, 3, colDiskW);

                // Định vị thanh trạng thái dưới đáy
                int statusY = listY + listH + 2;
                MoveWindow(g_hStatusLabel, gap, statusY, w - gap * 2, statusH, TRUE);

                RECT headerRect = { 0, 0, w, headerH };
                InvalidateRect(hwnd, &headerRect, FALSE);
                return 0;
            }

            // Nhận tín hiệu cập nhật thông số CPU/RAM/Đĩa
            case WM_TASK_UPDATE_SYS: {
                RECT headerRect = { 0, 0, 3000, headerH };
                InvalidateRect(hwnd, &headerRect, FALSE);
                return 0;
            }

            // Nhận tín hiệu làm mới bảng tiến trình
            case WM_TASK_UPDATE_LIST: {
                RenderProcessListOnUI();
                return 0;
            }

            // Xử lý các sự kiện bấm nút và nhập liệu tìm kiếm
            case WM_COMMAND: {
                int id = LOWORD(wp);
                int code = HIWORD(wp);

                // Bấm nút "Làm mới (F5)"
                if (id == IDC_TASK_REFRESH && code == BN_CLICKED) {
                    if (g_client) {
                        g_client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
                        g_client->SendPacket(Protocol::MSG_SYS_INFO_REQUEST, nullptr, 0);
                    }
                    return 0;
                }

                // Bấm nút "Kết thúc tác vụ"
                if (id == IDC_TASK_KILL && code == BN_CLICKED) {
                    TriggerKillSelectedProcess(hwnd);
                    return 0;
                }

                // Người dùng gõ từ khóa vào ô tìm kiếm -> Lọc danh sách ngay lập tức
                if (id == IDC_TASK_SEARCH && code == EN_CHANGE) {
                    wchar_t buf[128] = {0};
                    GetWindowTextW(g_hSearchEdit, buf, 128);
                    g_searchQuery = buf;
                    RenderProcessListOnUI();
                    return 0;
                }
                break;
            }

            // Xử lý sự kiện nhấp đúp (Double-Click) vào một tiến trình để tắt nhanh
            case WM_NOTIFY: {
                LPNMHDR nmh = (LPNMHDR)lp;
                if (nmh->idFrom == IDC_TASK_LIST && nmh->code == NM_DBLCLK) {
                    TriggerKillSelectedProcess(hwnd);
                    return 0;
                }
                break;
            }

            // Bắt phím tắt bàn phím
            case WM_KEYDOWN: {
                if (wp == VK_F5) {
                    // Phím F5: Gửi yêu cầu làm mới dữ liệu
                    if (g_client) {
                        g_client->SendPacket(Protocol::MSG_PROCESS_LIST_REQUEST, nullptr, 0);
                        g_client->SendPacket(Protocol::MSG_SYS_INFO_REQUEST, nullptr, 0);
                    }
                    return 0;
                }
                if (wp == VK_DELETE) {
                    // Phím Delete: Tắt tiến trình đang chọn
                    TriggerKillSelectedProcess(hwnd);
                    return 0;
                }
                break;
            }

            // Đóng cửa sổ và dọn dẹp các handle
            case WM_CLOSE:
                KillTimer(hwnd, 1);
                DestroyWindow(hwnd);
                g_hTaskWnd = NULL;
                g_hTaskList = NULL;
                g_hSearchEdit = NULL;
                g_hBtnRefresh = NULL;
                g_hBtnKill = NULL;
                g_hStatusLabel = NULL;
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

namespace TaskManagerWindow {
    // ========================================================================
    // HIỂN THỊ CỬA SỔ TASK MANAGER
    // ========================================================================
    void Show(HWND hParent, TcpClient* client) {
        if (!g_csInit) {
            InitializeCriticalSection(&g_dataCs);
            g_csInit = true;
        }
        g_client = client;
        if (!g_hTaskWnd) {
            // Đăng ký lớp cửa sổ TaskClassW
            WNDCLASSW wc = {};
            wc.lpfnWndProc = TaskWndProc;
            wc.lpszClassName = L"TaskClassW";
            wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassW(&wc);

            // Tự động scale kích thước cửa sổ ban đầu theo tỷ lệ DPI của màn hình
            float scale = 1.0f;
            HDC screenDc = GetDC(NULL);
            if (screenDc) {
                scale = (float)GetDeviceCaps(screenDc, LOGPIXELSY) / 96.0f;
                ReleaseDC(NULL, screenDc);
            }
            int winW = (int)std::round(780.0f * scale);
            int winH = (int)std::round(620.0f * scale);

            int scrW = GetSystemMetrics(SM_CXSCREEN);
            int scrH = GetSystemMetrics(SM_CYSCREEN);
            int winX = (scrW - winW) / 2;
            int winY = (scrH - winH) / 2;
            if (winX < 40) winX = 40;
            if (winY < 40) winY = 40;

            // Tạo cửa sổ luôn nổi (WS_EX_TOPMOST) phía trên cửa sổ điều khiển
            CreateWindowExW(WS_EX_TOPMOST, L"TaskClassW", L"Task Manager - Quản lý tác vụ từ xa",
                WS_OVERLAPPEDWINDOW | WS_VISIBLE, winX, winY, winW, winH, hParent, NULL, GetModuleHandle(NULL), NULL);
        } else {
            SetForegroundWindow(g_hTaskWnd); // Đưa cửa sổ đã mở lên trước mặt
        }
    }

    // ========================================================================
    // CẬP NHẬT THÔNG SỐ HỆ THỐNG (CPU, RAM, DISK)
    // ========================================================================
    void UpdateSysInfo(const Protocol::SysInfoPayload& sys) {
        if (!g_csInit || !g_hTaskWnd) return;
        EnterCriticalSection(&g_dataCs);
        g_latestSys = sys;
        LeaveCriticalSection(&g_dataCs);
        PostMessageW(g_hTaskWnd, WM_TASK_UPDATE_SYS, 0, 0); // Đẩy thông điệp yêu cầu vẽ lại sang luồng UI
    }

    // ========================================================================
    // CẬP NHẬT DANH SÁCH TIẾN TRÌNH
    // ========================================================================
    void UpdateProcessList(const Protocol::ProcessInfoPayload* pList, int count) {
        if (!g_csInit || !g_hTaskWnd || count <= 0 || !pList) return;
        EnterCriticalSection(&g_dataCs);
        g_latestProcesses.assign(pList, pList + count); // Gán mảng tiến trình mới
        LeaveCriticalSection(&g_dataCs);
        PostMessageW(g_hTaskWnd, WM_TASK_UPDATE_LIST, 0, 0); // Đẩy thông điệp cập nhật bảng sang luồng UI
    }
}
