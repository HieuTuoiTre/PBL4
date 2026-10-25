// ============================================================================
// File: FileExplorerWindow.cpp
// Mô tả: Hiện thực chi tiết giao diện cửa sổ File Explorer duyệt và tải tệp tin từ xa.
// Bao gồm:
//   - Thanh công cụ điều hướng nhanh: Nút Ổ đĩa, Lên cấp, Làm mới, Tải file về và Thanh địa chỉ.
//   - Hiển thị danh sách ổ đĩa cục bộ của Client (C:\, D:\, ...) và cây thư mục/tập tin.
//   - Tự động định dạng kích thước tệp tin thông minh (B, KB, MB, GB).
//   - Tích hợp thanh tiến trình tải file (msctls_progress32) trực quan theo thời gian thực.
// ============================================================================

// Nạp file header khai báo giao diện của FileExplorerWindow
#include "FileExplorerWindow.h"

// Nạp các hàm tiện ích chuyển đổi bảng mã UTF-8 và Unicode UTF-16
#include "../../Shared/Utils.h"

// Nạp module quản lý ghi file ra đĩa cục bộ
#include "../FileTransfer/FileManager.h"

// Nạp thư viện điều khiển giao diện phổ thông Windows Common Controls (SysListView32, Progress Bar)
#include <commctrl.h>

// Nạp các thư viện luồng chuỗi, mảng động, chuỗi ký tự và toán học chuẩn C++
#include <sstream>
#include <vector>
#include <string>
#include <cwchar>
#include <cmath>

// Định nghĩa các mã thông điệp tùy biến để luồng mạng DataReceiver thông báo cho luồng UI
#define WM_FILE_DRIVE_LIST (WM_APP + 201) // Thông điệp báo có danh sách ổ đĩa mới từ Client
#define WM_FILE_DIR_LIST   (WM_APP + 202) // Thông điệp báo có nội dung thư mục mới từ Client
#define WM_FILE_START_DL   (WM_APP + 203) // Thông điệp báo bắt đầu quá trình tải một tệp tin
#define WM_FILE_CHUNK_DL   (WM_APP + 204) // Thông điệp báo đã nhận thêm một chunk dữ liệu file
#define WM_FILE_END_DL     (WM_APP + 205) // Thông điệp báo quá trình tải file đã hoàn thành 100%

// Định danh các thành phần điều khiển trên giao diện
#define IDC_FILE_LIST      2001 // ID của bảng danh sách SysListView32
#define IDC_FILE_ROOT      2002 // ID của nút "Ổ đĩa" (về thư mục gốc)
#define IDC_FILE_UP        2003 // ID của nút "Lên cấp" (lên thư mục cha)
#define IDC_FILE_REFRESH   2004 // ID của nút "Làm mới"
#define IDC_FILE_DOWNLOAD  2005 // ID của nút "Tải file về"
#define IDC_FILE_ADDR      2006 // ID của ô hiển thị đường dẫn thư mục hiện tại
#define IDC_FILE_PROGRESS  2007 // ID của thanh tiến trình ProgressBar
#define IDC_FILE_STATUS    2008 // ID của nhãn hiển thị trạng thái tải và số lượng tệp tin

namespace {
    // ========================================================================
    // CÁC HANDLE GIAO DIỆN VÀ BIẾN QUẢN LÝ TIẾN TRÌNH TRUYỀN FILE
    // ========================================================================
    HWND g_hFileWnd = NULL;      // Handle cửa sổ FileExplorerWindow
    HWND g_hFileList = NULL;     // Handle bảng danh sách file/folder (SysListView32)
    HWND g_hBtnRoot = NULL;      // Handle nút "Ổ đĩa"
    HWND g_hBtnUp = NULL;        // Handle nút "Lên cấp"
    HWND g_hBtnRefresh = NULL;   // Handle nút "Làm mới"
    HWND g_hBtnDownload = NULL;  // Handle nút "Tải file về"
    HWND g_hAddressEdit = NULL;  // Handle thanh hiển thị đường dẫn
    HWND g_hProgressBar = NULL;  // Handle thanh tiến trình (msctls_progress32)
    HWND g_hStatusLabel = NULL;  // Handle nhãn trạng thái dưới đáy
    TcpClient* g_client = nullptr; // Kênh TCP Điều khiển để gửi yêu cầu dữ liệu tệp tin

    std::string g_curPath = "";        // Đường dẫn thư mục hiện tại trên máy Client
    std::string g_prevPath = "";       // Đường dẫn thư mục trước đó (dùng để hoàn tác nếu gặp lỗi không có quyền truy cập)
    FileManager g_fileMgr;             // Đối tượng ghi dữ liệu nhị phân của tệp tin tải về vào ổ cứng
    std::string g_downloadingFile = "";// Tên tệp tin đang được tải về
    uint64_t g_totalFileSize = 0;      // Tổng dung lượng tính bằng byte của tệp tin đang tải
    uint64_t g_downloadedBytes = 0;    // Số byte dữ liệu thực tế đã nhận được và ghi xuống đĩa

    CRITICAL_SECTION g_fileCs;         // Khóa đồng bộ bảo vệ biến nội dung file giữa luồng mạng và luồng UI
    bool g_csInit = false;             // Cờ đánh dấu đã khởi tạo khóa g_fileCs hay chưa
    std::string g_pendingDriveContent; // Bộ đệm tạm chứa danh sách ổ đĩa chờ nạp vào UI
    std::string g_pendingDirContent;   // Bộ đệm tạm chứa danh sách thư mục chờ nạp vào UI

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

    // ========================================================================
    // ĐỊNH DẠNG DUNG LƯỢNG BYTE SANG CHUỖI ĐƠN VỊ ĐỌC ĐƯỢC (FORMATBYTES)
    // Quy đổi tự động sang B, KB, MB, GB với 2 chữ số thập phân
    // ========================================================================
    std::wstring FormatBytes(uint64_t bytes) {
        wchar_t buf[64];
        if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
            swprintf(buf, 64, L"%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
        } else if (bytes >= 1024ULL * 1024ULL) {
            swprintf(buf, 64, L"%.2f MB", (double)bytes / (1024.0 * 1024.0));
        } else if (bytes >= 1024ULL) {
            swprintf(buf, 64, L"%.1f KB", (double)bytes / 1024.0);
        } else {
            swprintf(buf, 64, L"%llu B", bytes);
        }
        return buf;
    }

    // ========================================================================
    // CHÈN MỘT DÒNG VÀO BẢNG DANH SÁCH SYSLISTVIEW32
    // Cột 0: Tên mục, Cột 1: Loại mục, Cột 2: Kích thước
    // ========================================================================
    void InsertFileRow(int index, const std::wstring& name, const std::wstring& type, const std::wstring& size) {
        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = index;
        lvi.pszText = (LPWSTR)name.c_str();
        SendMessageW(g_hFileList, LVM_INSERTITEMW, 0, (LPARAM)&lvi); // Chèn dòng mới

        lvi.iSubItem = 1;
        lvi.pszText = (LPWSTR)type.c_str();
        SendMessageW(g_hFileList, LVM_SETITEMTEXTW, index, (LPARAM)&lvi); // Gán Loại mục

        lvi.iSubItem = 2;
        lvi.pszText = (LPWSTR)size.c_str();
        SendMessageW(g_hFileList, LVM_SETITEMTEXTW, index, (LPARAM)&lvi); // Gán Kích thước
    }

    // ========================================================================
    // QUAY VỀ DANH SÁCH Ổ ĐĨA GỐC (NAVIGATE ROOT)
    // Gửi yêu cầu MSG_DRIVE_LIST_REQUEST sang máy Client
    // ========================================================================
    void NavigateRoot(HWND hwnd) {
        if (!g_client) return;
        g_prevPath = g_curPath;
        g_curPath = ""; // Làm rỗng đường dẫn hiện tại để biểu thị cấp cao nhất
        if (g_hAddressEdit) SetWindowTextW(g_hAddressEdit, L"Danh sách ổ đĩa");
        SetWindowTextW(hwnd, L"File Explorer - Danh sách ổ đĩa");
        g_client->SendPacket(Protocol::MSG_DRIVE_LIST_REQUEST, nullptr, 0);
    }

    // ========================================================================
    // ĐIỀU HƯỚNG LÊN THƯ MỤC CHA (NAVIGATE UP)
    // Cắt bỏ phần tử thư mục cuối cùng trên đường dẫn và gửi MSG_DIR_REQUEST
    // ========================================================================
    void NavigateUp(HWND hwnd) {
        if (!g_client) return;

        // Nếu đường dẫn là gốc ổ đĩa (ví dụ "C:\" hoặc "D:\") -> Quay về danh sách ổ đĩa
        if (g_curPath.length() <= 3) {
            NavigateRoot(hwnd);
        } else {
            g_prevPath = g_curPath;
            // Xóa dấu gạch chéo ngược ở cuối nếu có
            if (g_curPath.back() == '\\') g_curPath.pop_back();

            // Tìm vị trí của dấu gạch chéo phân cách thư mục cuối cùng
            size_t pos = g_curPath.find_last_of("\\/");
            if (pos != std::string::npos) {
                g_curPath = g_curPath.substr(0, pos);
                // Nếu lùi về ổ đĩa (ví dụ "C:") thì bổ sung thêm dấu "\" thành "C:\"
                if (g_curPath.length() == 2 && g_curPath[1] == ':') g_curPath += "\\";
            }
            if (g_hAddressEdit) SetWindowTextW(g_hAddressEdit, Utils::Utf8ToWide(g_curPath).c_str());
            SetWindowTextW(hwnd, (L"File Explorer - " + Utils::Utf8ToWide(g_curPath)).c_str());
            // Gửi yêu cầu nạp thư mục cha sang Client
            g_client->SendPacket(Protocol::MSG_DIR_REQUEST, g_curPath.c_str(), (int)g_curPath.size());
        }
    }

    // ========================================================================
    // LÀM MỚI DANH SÁCH THƯ MỤC HIỆN TẠI (REFRESH CURRENT)
    // ========================================================================
    void RefreshCurrent(HWND hwnd) {
        if (!g_client) return;
        if (g_curPath.empty()) {
            g_client->SendPacket(Protocol::MSG_DRIVE_LIST_REQUEST, nullptr, 0);
        } else {
            g_client->SendPacket(Protocol::MSG_DIR_REQUEST, g_curPath.c_str(), (int)g_curPath.size());
        }
    }

    // ========================================================================
    // XỬ LÝ KHI NGƯỜI DÙNG NHẤP ĐÚP VÀO MỘT DÒNG TRONG BẢNG (TRIGGER ACTION)
    // Mở thư mục nếu là thư mục/ổ đĩa, hoặc hỏi tải về nếu là tệp tin
    // ========================================================================
    void TriggerActionOnSelected(HWND hwnd) {
        if (!g_hFileList || !g_client) return;
        int sel = (int)SendMessageW(g_hFileList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
        if (sel == -1) return;

        wchar_t nameBuf[260] = {0}, typeBuf[128] = {0};
        LVITEMW lvi = {};
        lvi.iSubItem = 0; lvi.pszText = nameBuf; lvi.cchTextMax = 260;
        SendMessageW(g_hFileList, LVM_GETITEMTEXTW, sel, (LPARAM)&lvi);
        lvi.iSubItem = 1; lvi.pszText = typeBuf; lvi.cchTextMax = 128;
        SendMessageW(g_hFileList, LVM_GETITEMTEXTW, sel, (LPARAM)&lvi);

        std::string itemName = Utils::WideToUtf8(nameBuf);
        std::wstring itemType = typeBuf;

        // Nếu bấm vào dòng ".." -> Lên cấp thư mục cha
        if (itemName == "..") {
            NavigateUp(hwnd);
        }
        // Nếu nhấp vào Ổ ĐĨA -> Mở ổ đĩa đó
        else if (itemType == L"[Ổ ĐĨA]") {
            g_prevPath = g_curPath;
            g_curPath = itemName;
            if (g_hAddressEdit) SetWindowTextW(g_hAddressEdit, Utils::Utf8ToWide(g_curPath).c_str());
            SetWindowTextW(hwnd, (L"File Explorer - " + Utils::Utf8ToWide(g_curPath)).c_str());
            g_client->SendPacket(Protocol::MSG_DIR_REQUEST, g_curPath.c_str(), (int)g_curPath.size());
        }
        // Nếu nhấp vào THƯ MỤC -> Đi vào thư mục con đó
        else if (itemType == L"[THƯ MỤC]") {
            g_prevPath = g_curPath;
            if (!g_curPath.empty() && g_curPath.back() != '\\') g_curPath += "\\";
            g_curPath += itemName;
            if (g_hAddressEdit) SetWindowTextW(g_hAddressEdit, Utils::Utf8ToWide(g_curPath).c_str());
            SetWindowTextW(hwnd, (L"File Explorer - " + Utils::Utf8ToWide(g_curPath)).c_str());
            g_client->SendPacket(Protocol::MSG_DIR_REQUEST, g_curPath.c_str(), (int)g_curPath.size());
        }
        // Nếu nhấp vào TẬP TIN -> Hỏi xác nhận tải file về máy Manager
        else {
            std::wstring confirmMsg = L"Bạn có muốn tải file '" + std::wstring(nameBuf) + L"' về máy không?";
            if (MessageBoxW(hwnd, confirmMsg.c_str(), L"Xác nhận tải file", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                std::string fullPath = g_curPath;
                if (!fullPath.empty() && fullPath.back() != '\\') fullPath += "\\";
                fullPath += itemName;
                g_client->SendPacket(Protocol::MSG_FILE_DOWNLOAD_REQ, fullPath.c_str(), (int)fullPath.size());
            }
        }
    }

    // ========================================================================
    // XỬ LÝ KHI NGƯỜI DÙNG BẤM NÚT "TẢI FILE VỀ"
    // Kiểm tra dòng được chọn phải là tệp tin hợp lệ trước khi gửi lệnh tải
    // ========================================================================
    void TriggerDownloadSelected(HWND hwnd) {
        if (!g_hFileList || !g_client) return;
        int sel = (int)SendMessageW(g_hFileList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
        if (sel == -1) {
            MessageBoxW(hwnd, L"Vui lòng nhấp chọn một tệp tin trong danh sách trước khi bấm tải về!", L"Thông báo", MB_OK | MB_ICONINFORMATION);
            return;
        }

        wchar_t nameBuf[260] = {0}, typeBuf[128] = {0};
        LVITEMW lvi = {};
        lvi.iSubItem = 0; lvi.pszText = nameBuf; lvi.cchTextMax = 260;
        SendMessageW(g_hFileList, LVM_GETITEMTEXTW, sel, (LPARAM)&lvi);
        lvi.iSubItem = 1; lvi.pszText = typeBuf; lvi.cchTextMax = 128;
        SendMessageW(g_hFileList, LVM_GETITEMTEXTW, sel, (LPARAM)&lvi);

        // Chỉ cho phép tải nếu loại mục là tệp tin
        if (wcscmp(typeBuf, L"[TẬP TIN]") == 0) {
            std::wstring confirmMsg = L"Bạn có muốn tải file '" + std::wstring(nameBuf) + L"' về máy không?";
            if (MessageBoxW(hwnd, confirmMsg.c_str(), L"Xác nhận tải file", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                std::string fullPath = g_curPath;
                if (!fullPath.empty() && fullPath.back() != '\\') fullPath += "\\";
                fullPath += Utils::WideToUtf8(nameBuf);
                g_client->SendPacket(Protocol::MSG_FILE_DOWNLOAD_REQ, fullPath.c_str(), (int)fullPath.size());
            }
        } else {
            MessageBoxW(hwnd, L"Mục được chọn là thư mục hoặc ổ đĩa. Vui lòng chọn một tệp tin để tải về!", L"Thông báo", MB_OK | MB_ICONINFORMATION);
        }
    }

    // ========================================================================
    // HÀM XỬ LÝ THÔNG ĐIỆP CHÍNH CỦA CỬA SỔ FILE EXPLORER (FILEWNDPROC)
    // ========================================================================
    LRESULT CALLBACK FileWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        float scale = GetDpiScale(hwnd);

        switch (msg) {
            // Khởi tạo các control trên giao diện khi cửa sổ vừa mở
            case WM_CREATE: {
                g_hFileWnd = hwnd;

                // 1. Nút "Ổ đĩa" (về màn hình danh sách ổ đĩa ban đầu)
                g_hBtnRoot = CreateWindowExW(0, L"BUTTON", L"Ổ đĩa",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_FILE_ROOT, GetModuleHandle(NULL), NULL);

                // 2. Nút "Lên cấp" (lên thư mục cha)
                g_hBtnUp = CreateWindowExW(0, L"BUTTON", L"Lên cấp",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_FILE_UP, GetModuleHandle(NULL), NULL);

                // 3. Nút "Làm mới" (tải lại danh sách hiện tại)
                g_hBtnRefresh = CreateWindowExW(0, L"BUTTON", L"Làm mới",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_FILE_REFRESH, GetModuleHandle(NULL), NULL);

                // 4. Thanh hiển thị đường dẫn hiện tại (Address Bar - Chế độ chỉ đọc)
                g_hAddressEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"Danh sách ổ đĩa",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_FILE_ADDR, GetModuleHandle(NULL), NULL);

                // 5. Nút "Tải file về"
                g_hBtnDownload = CreateWindowExW(0, L"BUTTON", L"Tải file về",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_FILE_DOWNLOAD, GetModuleHandle(NULL), NULL);

                // 6. Bảng danh sách tệp tin & thư mục (SysListView32)
                g_hFileList = CreateWindowExW(0, L"SysListView32", L"",
                    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_FILE_LIST, GetModuleHandle(NULL), NULL);

                SendMessageW(g_hFileList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                    LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

                // Thêm 3 cột: Tên, Loại, Kích thước
                LVCOLUMNW lvc = { LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM, LVCFMT_LEFT, (int)(320 * scale), (LPWSTR)L"Tên Thư mục / Tập tin", 0, 0 };
                SendMessageW(g_hFileList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);
                lvc.cx = (int)(110 * scale); lvc.pszText = (LPWSTR)L"Loại"; SendMessageW(g_hFileList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);
                lvc.cx = (int)(130 * scale); lvc.fmt = LVCFMT_RIGHT; lvc.pszText = (LPWSTR)L"Kích thước"; SendMessageW(g_hFileList, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);

                // 7. Thanh tiến trình tải tệp tin (Progress Bar)
                g_hProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, L"",
                    WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_FILE_PROGRESS, GetModuleHandle(NULL), NULL);
                SendMessageW(g_hProgressBar, PBM_SETRANGE32, 0, 100); // Thang điểm phần trăm từ 0 đến 100%
                SendMessageW(g_hProgressBar, PBM_SETPOS, 0, 0);

                // 8. Nhãn trạng thái dưới chân trang
                g_hStatusLabel = CreateWindowExW(0, L"STATIC", L"Sẵn sàng | Nhấp đúp thư mục để mở, hoặc chọn file và bấm 'Tải file về'",
                    WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
                    0, 0, 0, 0, hwnd, (HMENU)IDC_FILE_STATUS, GetModuleHandle(NULL), NULL);

                // Gán phông chữ Segoe UI đồng bộ
                HFONT hFont = CreateFontW((int)(-12 * scale), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
                HFONT hFontBtn = CreateFontW((int)(-12 * scale), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
                SendMessageW(g_hBtnRoot, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
                SendMessageW(g_hBtnUp, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
                SendMessageW(g_hBtnRefresh, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
                SendMessageW(g_hBtnDownload, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
                SendMessageW(g_hAddressEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
                SendMessageW(g_hStatusLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

                g_curPath = ""; // Khởi tạo đường dẫn trống
                return 0;
            }

            // Giới hạn kích thước tối thiểu của cửa sổ
            case WM_GETMINMAXINFO: {
                MINMAXINFO* pMMI = (MINMAXINFO*)lp;
                float scale = GetDpiScale(hwnd);
                pMMI->ptMinTrackSize.x = (LONG)std::round(680.0f * scale);
                pMMI->ptMinTrackSize.y = (LONG)std::round(480.0f * scale);
                return 0;
            }

            // Bố trí giao diện Responsive khi thay đổi kích thước cửa sổ
            case WM_SIZE: {
                int w = LOWORD(lp);
                int h = HIWORD(lp);
                int gap = (int)std::round(6.0f * scale);
                int btnH = (int)std::round(30.0f * scale);
                int navY = gap;

                // Các nút điều hướng phía trên
                int btnRootW = (int)std::round(75.0f * scale);
                int btnUpW = (int)std::round(80.0f * scale);
                int btnRefW = (int)std::round(80.0f * scale);
                int btnDlW = (int)std::round(115.0f * scale);

                MoveWindow(g_hBtnRoot, gap, navY, btnRootW, btnH, TRUE);
                MoveWindow(g_hBtnUp, gap + btnRootW + gap, navY, btnUpW, btnH, TRUE);
                MoveWindow(g_hBtnRefresh, gap + btnRootW + gap + btnUpW + gap, navY, btnRefW, btnH, TRUE);

                int leftOffset = gap + btnRootW + gap + btnUpW + gap + btnRefW + gap;
                int rightOffset = w - gap - btnDlW;
                int addrW = rightOffset - leftOffset - gap;
                if (addrW < 80) addrW = 80;

                MoveWindow(g_hAddressEdit, leftOffset, navY, addrW, btnH, TRUE);
                MoveWindow(g_hBtnDownload, rightOffset, navY, btnDlW, btnH, TRUE);

                // Thanh tiến trình & trạng thái ở đáy
                int progressH = (int)std::round(12.0f * scale);
                int statusH = (int)std::round(22.0f * scale);
                int bottomY = h - gap - statusH - progressH;

                MoveWindow(g_hProgressBar, gap, bottomY, w - gap * 2, progressH, TRUE);
                MoveWindow(g_hStatusLabel, gap, bottomY + progressH + 2, w - gap * 2, statusH, TRUE);

                // Bảng danh sách tệp tin nằm giữa
                int listY = navY + btnH + gap;
                int listH = bottomY - listY - gap;
                if (listH < 60) listH = 60;
                MoveWindow(g_hFileList, gap, listY, w - gap * 2, listH, TRUE);

                // Tự động căn chỉnh độ rộng cột sao cho không bị tràn thanh cuộn ngang
                int colTypeW = (int)std::round(110.0f * scale);
                int colSizeW = (int)std::round(120.0f * scale);
                int scrollW = GetSystemMetrics(SM_CXVSCROLL) + 4;
                int colNameW = (w - gap * 2) - colTypeW - colSizeW - scrollW;
                if (colNameW < (int)(180.0f * scale)) colNameW = (int)(180.0f * scale);

                ListView_SetColumnWidth(g_hFileList, 0, colNameW);
                ListView_SetColumnWidth(g_hFileList, 1, colTypeW);
                ListView_SetColumnWidth(g_hFileList, 2, colSizeW);

                return 0;
            }

            // Nhận danh sách ổ đĩa từ Client và hiển thị lên bảng
            case WM_FILE_DRIVE_LIST: {
                if (!g_hFileList) return 0;
                EnterCriticalSection(&g_fileCs);
                std::string content = g_pendingDriveContent;
                LeaveCriticalSection(&g_fileCs);

                SendMessageW(g_hFileList, LVM_DELETEALLITEMS, 0, 0); // Xóa dữ liệu cũ
                std::stringstream ss(content);
                std::string line;
                int idx = 0;
                while (std::getline(ss, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (!line.empty()) {
                        InsertFileRow(idx++, Utils::Utf8ToWide(line), L"[Ổ ĐĨA]", L"--");
                    }
                }
                if (g_hAddressEdit) SetWindowTextW(g_hAddressEdit, L"Danh sách ổ đĩa");
                SetWindowTextW(hwnd, L"File Explorer - Danh sách ổ đĩa");
                if (g_hStatusLabel) SetWindowTextW(g_hStatusLabel, L"Đã tải danh sách ổ đĩa | Nhấp đúp để mở ổ đĩa");
                return 0;
            }

            // Nhận nội dung thư mục từ Client và bóc tách hiển thị
            case WM_FILE_DIR_LIST: {
                if (!g_hFileList) return 0;
                EnterCriticalSection(&g_fileCs);
                std::string content = g_pendingDirContent;
                LeaveCriticalSection(&g_fileCs);

                // Nếu Client trả về chuỗi bắt đầu bằng "ERROR:" (do bị chặn quyền Access Denied)
                if (content.rfind("ERROR:", 0) == 0) {
                    // Hoàn tác lại đường dẫn cũ để người dùng không bị kẹt ở đường dẫn lỗi
                    g_curPath = g_prevPath;
                    if (g_hAddressEdit) {
                        SetWindowTextW(g_hAddressEdit, g_curPath.empty() ? L"Danh sách ổ đĩa" : Utils::Utf8ToWide(g_curPath).c_str());
                    }
                    MessageBoxW(hwnd, Utils::Utf8ToWide(content).c_str(), L"Lỗi truy cập thư mục", MB_OK | MB_ICONWARNING);
                    return 0;
                }

                SendMessageW(g_hFileList, LVM_DELETEALLITEMS, 0, 0);
                int idx = 0;
                // Thêm mục quay lại thư mục cha ".."
                InsertFileRow(idx++, L"..", L"[QUAY LẠI]", L"--");

                std::stringstream ss(content);
                std::string line;
                int fileCount = 0, dirCount = 0;

                while (std::getline(ss, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();

                    // Mục là thư mục: "[DIR] <Tên thư mục>"
                    if (line.rfind("[DIR] ", 0) == 0) {
                        std::string folderName = line.substr(6);
                        InsertFileRow(idx++, Utils::Utf8ToWide(folderName), L"[THƯ MỤC]", L"--");
                        dirCount++;
                    }
                    // Mục là tệp tin: "[FILE] <Tên tệp tin> | <Số byte dung lượng>"
                    else if (line.rfind("[FILE] ", 0) == 0) {
                        std::string rest = line.substr(7);
                        size_t barPos = rest.rfind(" | "); // Tìm từ cuối chuỗi để tránh lỗi tên file chứa dấu " | "
                        if (barPos != std::string::npos) {
                            std::string fileName = rest.substr(0, barPos);
                            std::string sizeStr = rest.substr(barPos + 3);
                            uint64_t byteVal = 0;
                            try {
                                byteVal = std::stoull(sizeStr);
                            } catch (...) {}
                            std::wstring formattedSize = (byteVal > 0) ? FormatBytes(byteVal) : Utils::Utf8ToWide(sizeStr);
                            InsertFileRow(idx++, Utils::Utf8ToWide(fileName), L"[TẬP TIN]", formattedSize);
                            fileCount++;
                        }
                    }
                }

                if (g_hAddressEdit) SetWindowTextW(g_hAddressEdit, Utils::Utf8ToWide(g_curPath).c_str());
                SetWindowTextW(hwnd, (L"File Explorer - " + Utils::Utf8ToWide(g_curPath)).c_str());

                // Hiển thị tổng kết số lượng thư mục và tệp tin
                if (g_hStatusLabel) {
                    std::wstring statusStr = L"Thư mục: " + std::to_wstring(dirCount) + L" | Tệp tin: " + std::to_wstring(fileCount);
                    SetWindowTextW(g_hStatusLabel, statusStr.c_str());
                }
                return 0;
            }

            // Bắt đầu quá trình tải tệp tin
            case WM_FILE_START_DL: {
                EnterCriticalSection(&g_fileCs);
                std::string dlName = g_downloadingFile;
                uint64_t total = g_totalFileSize;
                LeaveCriticalSection(&g_fileCs);

                ShowWindow(g_hProgressBar, SW_SHOW);
                SendMessageW(g_hProgressBar, PBM_SETRANGE32, 0, 100);
                SendMessageW(g_hProgressBar, PBM_SETPOS, 0, 0);

                std::wstring statusStr = L"Đang tải: " + Utils::Utf8ToWide(dlName) + L" (0 / " + FormatBytes(total) + L" - 0%)";
                if (g_hStatusLabel) SetWindowTextW(g_hStatusLabel, statusStr.c_str());
                return 0;
            }

            // Nhận thêm chunk dữ liệu và cập nhật thanh tiến trình %
            case WM_FILE_CHUNK_DL: {
                EnterCriticalSection(&g_fileCs);
                std::string dlName = g_downloadingFile;
                uint64_t dl = g_downloadedBytes;
                uint64_t total = g_totalFileSize;
                LeaveCriticalSection(&g_fileCs);

                int percent = total > 0 ? (int)((dl * 100) / total) : 0;
                if (percent > 100) percent = 100;
                SendMessageW(g_hProgressBar, PBM_SETPOS, percent, 0);

                std::wstring statusStr = L"Đang tải: " + Utils::Utf8ToWide(dlName) + L" (" + FormatBytes(dl) + L" / " + FormatBytes(total) + L" - " + std::to_wstring(percent) + L"%)";
                if (g_hStatusLabel) SetWindowTextW(g_hStatusLabel, statusStr.c_str());
                return 0;
            }

            // Đã tải xong tệp tin
            case WM_FILE_END_DL: {
                EnterCriticalSection(&g_fileCs);
                std::string dlName = g_downloadingFile;
                uint64_t total = g_totalFileSize;
                LeaveCriticalSection(&g_fileCs);

                SendMessageW(g_hProgressBar, PBM_SETPOS, 100, 0);
                std::wstring statusStr = L"Đã tải xong: " + Utils::Utf8ToWide(dlName) + L" (" + FormatBytes(total) + L") - Đã lưu tại thư mục Manager.exe";
                if (g_hStatusLabel) SetWindowTextW(g_hStatusLabel, statusStr.c_str());
                return 0;
            }

            // Bắt sự kiện bấm nút điều hướng
            case WM_COMMAND: {
                int id = LOWORD(wp);
                int code = HIWORD(wp);

                if (code == BN_CLICKED) {
                    if (id == IDC_FILE_ROOT) {
                        NavigateRoot(hwnd); // Về gốc ổ đĩa
                        return 0;
                    }
                    if (id == IDC_FILE_UP) {
                        NavigateUp(hwnd);   // Lên cấp
                        return 0;
                    }
                    if (id == IDC_FILE_REFRESH) {
                        RefreshCurrent(hwnd); // Làm mới danh sách
                        return 0;
                    }
                    if (id == IDC_FILE_DOWNLOAD) {
                        TriggerDownloadSelected(hwnd); // Tải file đang chọn
                        return 0;
                    }
                }
                break;
            }

            // Nhấp đúp vào dòng trong bảng
            case WM_NOTIFY: {
                LPNMHDR nmh = (LPNMHDR)lp;
                if (nmh->idFrom == IDC_FILE_LIST && nmh->code == NM_DBLCLK && g_client) {
                    TriggerActionOnSelected(hwnd);
                    return 0;
                }
                break;
            }

            // Bắt phím tắt bàn phím
            case WM_KEYDOWN: {
                if (wp == VK_F5) {
                    RefreshCurrent(hwnd); // Phím F5: Làm mới
                    return 0;
                }
                if (wp == VK_BACK) {
                    NavigateUp(hwnd);     // Phím Backspace: Lên thư mục cha
                    return 0;
                }
                if (wp == VK_RETURN) {
                    TriggerActionOnSelected(hwnd); // Phím Enter: Mở hoặc tải
                    return 0;
                }
                break;
            }

            // Đóng cửa sổ và đặt lại các handle về NULL
            case WM_CLOSE:
                DestroyWindow(hwnd);
                g_hFileWnd = NULL;
                g_hFileList = NULL;
                g_hBtnRoot = NULL;
                g_hBtnUp = NULL;
                g_hBtnRefresh = NULL;
                g_hBtnDownload = NULL;
                g_hAddressEdit = NULL;
                g_hProgressBar = NULL;
                g_hStatusLabel = NULL;
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

namespace FileExplorerWindow {
    // ========================================================================
    // HIỂN THỊ CỬA SỔ FILE EXPLORER
    // ========================================================================
    void Show(HWND hParent, TcpClient* client) {
        if (!g_csInit) {
            InitializeCriticalSection(&g_fileCs);
            g_csInit = true;
        }
        g_client = client;
        if (!g_hFileWnd) {
            // Đăng ký lớp cửa sổ FileExpClass
            WNDCLASSW wc = {};
            wc.lpfnWndProc = FileWndProc;
            wc.lpszClassName = L"FileExpClass";
            wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassW(&wc);

            // Scale kích thước cửa sổ theo DPI màn hình
            float scale = 1.0f;
            HDC screenDc = GetDC(NULL);
            if (screenDc) {
                scale = (float)GetDeviceCaps(screenDc, LOGPIXELSY) / 96.0f;
                ReleaseDC(NULL, screenDc);
            }
            int winW = (int)std::round(780.0f * scale);
            int winH = (int)std::round(580.0f * scale);

            int scrW = GetSystemMetrics(SM_CXSCREEN);
            int scrH = GetSystemMetrics(SM_CYSCREEN);
            int winX = (scrW - winW) / 2 + (int)(30 * scale);
            int winY = (scrH - winH) / 2 + (int)(30 * scale);
            if (winX < 40) winX = 40;
            if (winY < 40) winY = 40;

            // Tạo cửa sổ luôn nổi phía trên
            CreateWindowExW(WS_EX_TOPMOST, L"FileExpClass", L"File Explorer - Danh sách ổ đĩa từ xa",
                WS_OVERLAPPEDWINDOW | WS_VISIBLE, winX, winY, winW, winH, hParent, NULL, GetModuleHandle(NULL), NULL);

            // Yêu cầu danh sách ổ đĩa ngay khi mở cửa sổ
            if (g_client && g_hFileWnd) {
                g_client->SendPacket(Protocol::MSG_DRIVE_LIST_REQUEST, nullptr, 0);
            }
        } else {
            SetForegroundWindow(g_hFileWnd); // Đưa cửa sổ đã mở lên trước mặt
        }
    }

    // ========================================================================
    // TIẾP NHẬN PHẢN HỒI DANH SÁCH Ổ ĐĨA
    // ========================================================================
    void OnDriveListResponse(const std::string& content) {
        if (!g_csInit || !g_hFileWnd) return;
        EnterCriticalSection(&g_fileCs);
        g_pendingDriveContent = content;
        LeaveCriticalSection(&g_fileCs);
        PostMessageW(g_hFileWnd, WM_FILE_DRIVE_LIST, 0, 0); // Báo sang luồng UI
    }

    // ========================================================================
    // TIẾP NHẬN PHẢN HỒI NỘI DUNG THƯ MỤC
    // ========================================================================
    void OnDirResponse(const std::string& content) {
        if (!g_csInit || !g_hFileWnd) return;
        EnterCriticalSection(&g_fileCs);
        g_pendingDirContent = content;
        LeaveCriticalSection(&g_fileCs);
        PostMessageW(g_hFileWnd, WM_FILE_DIR_LIST, 0, 0); // Báo sang luồng UI
    }

    // ========================================================================
    // BẮT ĐẦU NHẬN TỆP TIN TẢI VỀ
    // ========================================================================
    void OnFileInfo(const Protocol::FileInfoPayload& info) {
        if (!g_csInit) return;
        EnterCriticalSection(&g_fileCs);
        std::string fileName = info.fileName;
        // Trích xuất tên file thuần túy không chứa đường dẫn thư mục Client
        size_t slashPos = fileName.find_last_of("\\/");
        if (slashPos != std::string::npos) fileName = fileName.substr(slashPos + 1);

        g_downloadingFile = fileName;
        g_totalFileSize = info.fileSize;
        g_downloadedBytes = 0;
        // Mở file cục bộ trên Manager để chuẩn bị ghi dữ liệu nhị phân
        g_fileMgr.OpenFileForWrite(g_downloadingFile);
        LeaveCriticalSection(&g_fileCs);

        if (g_hFileWnd) PostMessageW(g_hFileWnd, WM_FILE_START_DL, 0, 0);
    }

    // ========================================================================
    // TIẾP NHẬN CHUNK DỮ LIỆU NHỊ PHÂN CỦA FILE
    // ========================================================================
    void OnFileChunk(const char* data, int size) {
        if (!g_csInit) return;
        EnterCriticalSection(&g_fileCs);
        g_fileMgr.WriteChunk(data, size); // Ghi khối nhị phân xuống ổ cứng
        g_downloadedBytes += size;
        LeaveCriticalSection(&g_fileCs);

        if (g_hFileWnd) PostMessageW(g_hFileWnd, WM_FILE_CHUNK_DL, 0, 0);
    }

    // ========================================================================
    // KẾT THÚC QUÁ TRÌNH TẢI FILE
    // ========================================================================
    void OnFileEnd() {
        if (!g_csInit) return;
        EnterCriticalSection(&g_fileCs);
        g_fileMgr.CloseFile(); // Đóng file và giải phóng tài nguyên
        LeaveCriticalSection(&g_fileCs);

        if (g_hFileWnd) PostMessageW(g_hFileWnd, WM_FILE_END_DL, 0, 0);
    }
}
