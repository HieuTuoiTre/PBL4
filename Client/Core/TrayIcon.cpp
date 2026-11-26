#include "TrayIcon.h"
#include "../../Shared/ClipboardManager.h"
#include "SessionManager.h"
#include <shellapi.h>

namespace{
    // 1. Định nghĩa mã thông điệp tùy chỉnh (User Message) để Windows báo sự kiện chuột từ Tray Icon về cửa sổ
    constexpr UINT WM_TRAYICON = WM_USER + 1;

    constexpr UINT ID_TRAY_EXIT = 1001;
    constexpr UINT ID_TRAY_PIN_INFO = 1002;
    constexpr UINT ID_TRAY_COPY_PIN = 1003;
    constexpr UINT ID_TRAY_NEW_PIN = 1004;

    constexpr UINT TRAY_ICON_ID = 1;

    const wchar_t CLASS_NAME[] = L"Client_TrayClass";

    HWND g_hWnd = NULL;
    NOTIFYICONDATA g_nid = {0};
    bool g_isInitialized = false;
    UINT g_uTaskbarRestartMsg = 0;
    std::string g_currentPin = "";

    LRESULT CALLBACK TrayWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void ShowContextMenu(HWND hWnd);
}

namespace TrayIcon {

    bool Initialize(HINSTANCE hInstance) {
        if (g_isInitialized) return true;

        // 1. Đăng ký thông điệp hệ thống để tự phục hồi icon khi Explorer khởi động lại
        g_uTaskbarRestartMsg = RegisterWindowMessageW(L"TaskbarCreated");

        // 2. Đăng ký lớp cửa sổ (Window Class)
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = TrayWindowProc;     // Gán hàm xử lý sự kiện
        wc.hInstance = hInstance;
        wc.lpszClassName = CLASS_NAME;

        if (!RegisterClassW(&wc)) {
            return false;
        }

        // 3. Tạo cửa sổ ẩn chuẩn (Top-level Hidden Window, không dùng HWND_MESSAGE)
        // WS_OVERLAPPED không có WS_VISIBLE sẽ ẩn hoàn toàn khỏi màn hình và Taskbar,
        // nhưng tương thích 100% với các thông điệp từ khay hệ thống và TrackPopupMenu.
        g_hWnd = CreateWindowExW(
            0,
            CLASS_NAME,
            L"TrayIconReceiver",
            WS_OVERLAPPED, 
            0, 0, 0, 0,
            NULL, 
            NULL,
            hInstance,
            NULL
        );

        if (!g_hWnd) {
            UnregisterClassW(CLASS_NAME, hInstance);
            return false;
        }

        // 4. Cấu hình icon và đẩy xuống khay hệ thống kèm thông báo Balloon/Toast
        g_nid.cbSize = sizeof(NOTIFYICONDATAW);
        g_nid.hWnd = g_hWnd;
        g_nid.uID = TRAY_ICON_ID;
        g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_INFO;
        g_nid.uCallbackMessage = WM_TRAYICON; // Nhận sự kiện chuột qua mã thông điệp này
        
        // Tạm thời dùng icon ứng dụng mặc định của hệ điều hành
        g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION); 
        wcscpy_s(g_nid.szTip, L"Remote Desktop Client"); // Dòng chữ hiện ra khi rê chuột vào icon
        wcscpy_s(g_nid.szInfoTitle, L"Remote Desktop Client");
        wcscpy_s(g_nid.szInfo, L"Client đang chạy ngầm sẵn sàng kết nối.");
        g_nid.dwInfoFlags = NIIF_INFO;

        // Gọi API nạp icon xuống khay hệ thống
        if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) {
            DestroyWindow(g_hWnd);
            UnregisterClassW(CLASS_NAME, hInstance);
            g_hWnd = NULL;
            return false;
        }

        ClipboardManager::Initialize(g_hWnd);

        g_isInitialized = true;
        return true;
    }

    HWND GetHwnd() {
        return g_hWnd;
    }

    void SetPin(const std::string& pin) {
        g_currentPin = pin;
        if (!g_isInitialized || !g_hWnd) return;

        std::wstring tipW = L"Remote Desktop Client - PIN: " + std::wstring(pin.begin(), pin.end());
        wcsncpy_s(g_nid.szTip, tipW.c_str(), sizeof(g_nid.szTip) / sizeof(wchar_t) - 1);
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    }

    std::string GetPin() {
        return g_currentPin;
    }

    void ShowPinNotification() {
        if (!g_isInitialized || !g_hWnd) return;

        g_nid.uFlags |= NIF_INFO;
        wcscpy_s(g_nid.szInfoTitle, L"Remote Desktop Client - Mã PIN");
        std::wstring infoW = L"Mã PIN kết nối mới của bạn: " + std::wstring(g_currentPin.begin(), g_currentPin.end()) +
                             L"\nHãy nhập mã này trên máy Manager để kết nối.";
        wcsncpy_s(g_nid.szInfo, infoW.c_str(), sizeof(g_nid.szInfo) / sizeof(wchar_t) - 1);
        g_nid.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    }

} // namespace TrayIcon

namespace {

    void ShowContextMenu(HWND hWnd) {
        // 1. Tạo một Popup Menu
        HMENU hMenu = CreatePopupMenu();
        if (!hMenu) return;

        std::wstring pinW = std::wstring(g_currentPin.begin(), g_currentPin.end());
        std::wstring pinTitle = L"Mã PIN: " + (pinW.empty() ? L"(Chưa tạo)" : pinW);

        // 2. Thêm các mục menu
        InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_GRAYED | MF_STRING, ID_TRAY_PIN_INFO, pinTitle.c_str());
        InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_STRING, ID_TRAY_COPY_PIN, L"Sao chép mã PIN");
        InsertMenuW(hMenu, 2, MF_BYPOSITION | MF_STRING, ID_TRAY_NEW_PIN, L"Đổi mã PIN mới");
        InsertMenuW(hMenu, 3, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
        InsertMenuW(hMenu, 4, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, L"Thoát");

        // 3. Lấy vị trí hiện tại của con trỏ chuột trên màn hình
        POINT pt;
        GetCursorPos(&pt);

        // Đặt cửa sổ ẩn làm tiền cảnh trước khi mở menu để menu tự đóng nếu bấm trượt ra ngoài
        SetForegroundWindow(hWnd);

        // 4. Hiển thị menu nổi tại vị trí con trỏ chuột
        TrackPopupMenu(
            hMenu,
            TPM_BOTTOMALIGN | TPM_RIGHTALIGN,
            pt.x,
            pt.y,
            0,
            hWnd,
            NULL
        );

        // 5. Giải phóng menu sau khi người dùng chọn xong hoặc bỏ click
        DestroyMenu(hMenu);

        // 6. Theo chuẩn Microsoft KB PRB (Q139526): Post WM_NULL để ép chuyển giao focus tránh menu bị kẹt
        PostMessageW(hWnd, WM_NULL, 0, 0);
    }

}

namespace {

    LRESULT CALLBACK TrayWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        // Tự động phục hồi Tray Icon nếu Explorer.exe bị restart hoặc crash
        if (g_uTaskbarRestartMsg != 0 && uMsg == g_uTaskbarRestartMsg) {
            Shell_NotifyIconW(NIM_ADD, &g_nid);
            return 0;
        }

        switch (uMsg) {
        case WM_TRAYICON:
            // lParam chứa sự kiện chuột xảy ra trên Icon
            if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU || LOWORD(lParam) == WM_LBUTTONDBLCLK) {
                // Người dùng click chuột phải hoặc nhấp đúp trên Icon -> Mở menu
                ShowContextMenu(hWnd);
            }
            break;

        case WM_COMMAND: {
            UINT cmd = LOWORD(wParam);
            if (cmd == ID_TRAY_EXIT) {
                PostQuitMessage(0);
            } else if (cmd == ID_TRAY_COPY_PIN) {
                if (!g_currentPin.empty()) {
                    ClipboardManager::WriteText(hWnd, g_currentPin);
                }
            } else if (cmd == ID_TRAY_NEW_PIN) {
                std::string newPin = SessionManager::GenerateNewPin();
                TrayIcon::SetPin(newPin);
                TrayIcon::ShowPinNotification();
                ClipboardManager::WriteText(hWnd, newPin);
            }
            break;
        }

        case WM_CLIPBOARDUPDATE: {
            if (SessionManager::IsConnected()) {
                std::string utf8Text;
                if (ClipboardManager::ReadText(hWnd, utf8Text)) {
                    if (ClipboardManager::ShouldSync(utf8Text)) {
                        SessionManager::SendClipboardText(utf8Text);
                    }
                }
            }
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcW(hWnd, uMsg, wParam, lParam);
        }
        return 0;
    }

}

namespace TrayIcon {

    void Cleanup() {
        if (!g_isInitialized) return;

        // 1. Hủy lắng nghe sự kiện clipboard
        if (g_hWnd) {
            ClipboardManager::Cleanup(g_hWnd);
        }

        // 2. Xóa icon khỏi khay hệ thống
        Shell_NotifyIconW(NIM_DELETE, &g_nid);

        // 3. Hủy cửa sổ ẩn và hủy đăng ký lớp cửa sổ
        if (g_hWnd) {
            DestroyWindow(g_hWnd);
            g_hWnd = NULL;
        }

        HINSTANCE hInstance = GetModuleHandleW(NULL);
        UnregisterClassW(CLASS_NAME, hInstance);

        g_isInitialized = false;
    }

} // namespace TrayIcon
