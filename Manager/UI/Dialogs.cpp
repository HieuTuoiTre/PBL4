#include "Dialogs.h"
#include "UITheme.h"
#include "../../Shared/Network.h"
#include "../../Shared/Utils.h"
#include <commctrl.h>
#include <algorithm>
#include <thread>
#include <atomic>

namespace Dialogs {

constexpr wchar_t kDialogClass[] = L"RemoteCustomModalDialog";

constexpr int ID_BTN_OK = 3001;
constexpr int ID_BTN_CANCEL = 3002;
constexpr int ID_BTN_RETRY = 3003;
constexpr UINT WM_CONNECT_RESULT = WM_USER + 201;

enum class DialogType {
    Confirm,
    Connecting,
    Error,
    Warning,
    Info
};

struct DialogParams {
    DialogType type = DialogType::Info;
    std::wstring title;
    std::wstring message;
    bool allowRetry = false;
    DialogResult result = DialogResult::Cancel;
    bool isClosed = false;

    HWND hwnd = nullptr;
    HWND btnPrimary = nullptr;
    HWND btnSecondary = nullptr;
    HWND progressBar = nullptr;

    std::atomic<bool> cancelRequested{false};
    TcpClient** ppClient = nullptr;
    bool connectSuccess = false;
};

static void CenterDialog(HWND dialog, HWND parent) {
    RECT parentRect{};
    if (parent && IsWindow(parent)) {
        GetWindowRect(parent, &parentRect);
    } else {
        parentRect.left = 0;
        parentRect.top = 0;
        parentRect.right = GetSystemMetrics(SM_CXSCREEN);
        parentRect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    RECT dialogRect{};
    GetWindowRect(dialog, &dialogRect);
    int dWidth = dialogRect.right - dialogRect.left;
    int dHeight = dialogRect.bottom - dialogRect.top;

    int pWidth = parentRect.right - parentRect.left;
    int pHeight = parentRect.bottom - parentRect.top;

    int x = parentRect.left + (pWidth - dWidth) / 2;
    int y = parentRect.top + (pHeight - dHeight) / 2;

    SetWindowPos(dialog, HWND_TOP, std::max(0, x), std::max(0, y), 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
}

static LRESULT CALLBACK DialogWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* params = reinterpret_cast<DialogParams*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        params = reinterpret_cast<DialogParams*>(cs->lpCreateParams);
        params->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(params));

        if (params->type == DialogType::Confirm) {
            params->btnPrimary = CreateWindowExW(0, L"BUTTON", L"Đồng ý", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                                 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_BTN_OK), nullptr, nullptr);
            params->btnSecondary = CreateWindowExW(0, L"BUTTON", L"Hủy", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                                   0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_BTN_CANCEL), nullptr, nullptr);
            UITheme::SetControlFont(params->btnPrimary, UITheme::GetBoldFont());
            UITheme::SetControlFont(params->btnSecondary, UITheme::GetBoldFont());
        } else if (params->type == DialogType::Connecting) {
            params->progressBar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
                                                  WS_CHILD | WS_VISIBLE | PBS_MARQUEE,
                                                  0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(params->progressBar, PBM_SETMARQUEE, TRUE, 30);

            params->btnSecondary = CreateWindowExW(0, L"BUTTON", L"Hủy bỏ", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                                   0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_BTN_CANCEL), nullptr, nullptr);
            UITheme::SetControlFont(params->btnSecondary, UITheme::GetBoldFont());
        } else if (params->type == DialogType::Error) {
            if (params->allowRetry) {
                params->btnPrimary = CreateWindowExW(0, L"BUTTON", L"Thử lại", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                                     0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_BTN_RETRY), nullptr, nullptr);
                params->btnSecondary = CreateWindowExW(0, L"BUTTON", L"Đóng", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                                       0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_BTN_CANCEL), nullptr, nullptr);
                UITheme::SetControlFont(params->btnPrimary, UITheme::GetBoldFont());
                UITheme::SetControlFont(params->btnSecondary, UITheme::GetBoldFont());
            } else {
                params->btnPrimary = CreateWindowExW(0, L"BUTTON", L"Đóng", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                                     0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_BTN_CANCEL), nullptr, nullptr);
                UITheme::SetControlFont(params->btnPrimary, UITheme::GetBoldFont());
            }
        } else {
            params->btnPrimary = CreateWindowExW(0, L"BUTTON", L"Đã hiểu", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                                 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_BTN_OK), nullptr, nullptr);
            UITheme::SetControlFont(params->btnPrimary, UITheme::GetBoldFont());
        }
        return 0;
    }

    case WM_SIZE: {
        if (!params) return 0;
        RECT client{};
        GetClientRect(hwnd, &client);
        int btnWidth = 120;
        int btnHeight = 38;
        int btnY = client.bottom - btnHeight - 20;

        if (params->type == DialogType::Connecting) {
            int progW = client.right - 80;
            int progH = 16;
            int progX = 40;
            int progY = 138;
            MoveWindow(params->progressBar, progX, progY, progW, progH, TRUE);

            int startX = (client.right - btnWidth) / 2;
            MoveWindow(params->btnSecondary, startX, btnY, btnWidth, btnHeight, TRUE);
        } else if (params->type == DialogType::Confirm || (params->type == DialogType::Error && params->allowRetry)) {
            int gap = 16;
            int totalW = btnWidth * 2 + gap;
            int startX = (client.right - totalW) / 2;
            MoveWindow(params->btnPrimary, startX, btnY, btnWidth, btnHeight, TRUE);
            MoveWindow(params->btnSecondary, startX + btnWidth + gap, btnY, btnWidth, btnHeight, TRUE);
        } else {
            int startX = (client.right - btnWidth) / 2;
            MoveWindow(params->btnPrimary, startX, btnY, btnWidth, btnHeight, TRUE);
        }
        return 0;
    }

    case WM_CONNECT_RESULT:
        if (params) {
            params->connectSuccess = (wParam == 1);
            params->result = (wParam == 1) ? DialogResult::Ok : DialogResult::Cancel;
            params->isClosed = true;
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_BTN_OK) {
            params->result = DialogResult::Ok;
            params->isClosed = true;
            DestroyWindow(hwnd);
        } else if (id == ID_BTN_CANCEL) {
            params->cancelRequested = true;
            params->result = DialogResult::Cancel;
            params->isClosed = true;
            DestroyWindow(hwnd);
        } else if (id == ID_BTN_RETRY) {
            params->result = DialogResult::Retry;
            params->isClosed = true;
            DestroyWindow(hwnd);
        }
        return 0;
    }

    case WM_DRAWITEM: {
        const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        HDC dc = draw->hDC;
        RECT rect = draw->rcItem;
        const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
        wchar_t text[64]{};
        GetWindowTextW(draw->hwndItem, text, static_cast<int>(std::size(text)));

        SetBkMode(dc, TRANSPARENT);
        if (draw->CtlID == ID_BTN_OK || draw->CtlID == ID_BTN_RETRY) {
            COLORREF col = pressed ? RGB(11, 94, 215) : RGB(13, 110, 253);
            UITheme::DrawRoundedPanel(dc, rect, col, col, 6);
            SetTextColor(dc, RGB(255, 255, 255));
        } else if (draw->CtlID == ID_BTN_CANCEL) {
            COLORREF col = pressed ? RGB(92, 99, 106) : RGB(108, 117, 125);
            UITheme::DrawRoundedPanel(dc, rect, col, col, 6);
            SetTextColor(dc, RGB(255, 255, 255));
        }

        SelectObject(dc, UITheme::GetBoldFont());
        DrawTextW(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return TRUE;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);

        UITheme::FillSolid(dc, client, RGB(255, 255, 255));

        // Vẽ thanh Header màu
        RECT topBar = client;
        topBar.bottom = 6;
        COLORREF accentColor = RGB(13, 110, 253);
        if (params->type == DialogType::Error) {
            accentColor = RGB(220, 53, 69);
        } else if (params->type == DialogType::Warning) {
            accentColor = RGB(234, 179, 8);
        }
        UITheme::FillSolid(dc, topBar, accentColor);

        // Vẽ tiêu đề
        RECT titleRect = client;
        titleRect.top = 22;
        titleRect.bottom = 58;
        titleRect.left = 24;
        titleRect.right -= 24;

        SetBkMode(dc, TRANSPARENT);
        SelectObject(dc, UITheme::GetBoldFont());
        SetTextColor(dc, params->type == DialogType::Error ? RGB(185, 28, 28) : RGB(15, 23, 42));
        DrawTextW(dc, params->title.c_str(), -1, &titleRect, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);

        // Vẽ nội dung thông báo
        int btnHeight = 38;
        int btnY = client.bottom - btnHeight - 20;

        RECT msgRect = client;
        msgRect.top = 66;
        if (params->type == DialogType::Connecting) {
            msgRect.bottom = 130;
        } else {
            msgRect.bottom = btnY - 14;
        }
        msgRect.left = 30;
        msgRect.right -= 30;

        SelectObject(dc, UITheme::GetNormalFont());
        SetTextColor(dc, RGB(71, 85, 105));
        DrawTextW(dc, params->message.c_str(), -1, &msgRect, DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            params->cancelRequested = true;
            params->result = DialogResult::Cancel;
            params->isClosed = true;
            DestroyWindow(hwnd);
            return 0;
        } else if (wParam == VK_RETURN) {
            params->result = (params->type == DialogType::Confirm) ? DialogResult::Ok : DialogResult::Cancel;
            params->isClosed = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_DESTROY:
        if (params) {
            params->isClosed = true;
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool Register(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DialogWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kDialogClass;
    return RegisterClassExW(&wc) != 0;
}

static DialogResult ExecuteModal(HWND parent, DialogParams& params, int width, int height) {
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    if (parent && IsWindow(parent)) {
        EnableWindow(parent, FALSE);
    }

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kDialogClass, params.title.c_str(),
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                               CW_USEDEFAULT, CW_USEDEFAULT, width, height,
                               parent, nullptr, hInst, &params);

    if (!dlg) {
        if (parent && IsWindow(parent)) EnableWindow(parent, TRUE);
        return DialogResult::Cancel;
    }

    CenterDialog(dlg, parent);

    MSG msg{};
    while (!params.isClosed && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (parent && IsWindow(parent)) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }

    return params.result;
}

bool ShowConfirm(HWND parent, const std::wstring& ip, const std::wstring& machineName) {
    DialogParams params{};
    params.type = DialogType::Confirm;
    params.title = L"Xác nhận kết nối";
    params.message = L"Bạn có muốn kết nối đến máy tính từ xa:\n" + machineName + L" (" + ip + L") không?";
    return ExecuteModal(parent, params, 520, 250) == DialogResult::Ok;
}

bool ShowConnecting(HWND parent, const std::wstring& ip, const std::wstring& machineName, TcpClient*& outClient) {
    DialogParams params{};
    params.type = DialogType::Connecting;
    params.title = L"Đang kết nối tới thiết bị...";
    params.message = L"Đang kết nối tới máy:\n" + machineName + L" (" + ip + L")\nVui lòng chờ trong giây lát...";
    params.ppClient = &outClient;

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    if (parent && IsWindow(parent)) {
        EnableWindow(parent, FALSE);
    }

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kDialogClass, params.title.c_str(),
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                               CW_USEDEFAULT, CW_USEDEFAULT, 520, 270,
                               parent, nullptr, hInst, &params);
    if (!dlg) {
        if (parent && IsWindow(parent)) EnableWindow(parent, TRUE);
        return false;
    }

    CenterDialog(dlg, parent);

    std::string ipA = Utils::WideToUtf8(ip);

    // Luồng nền thực hiện kết nối TCP socket
    std::thread connectThread([&params, ipA, dlg]() {
        auto* client = new TcpClient();
        bool ok = client->Connect(ipA, 8080);

        if (params.cancelRequested.load()) {
            client->Close();
            delete client;
        } else {
            if (ok) {
                *params.ppClient = client;
            } else {
                delete client;
            }
            if (IsWindow(dlg)) {
                PostMessageW(dlg, WM_CONNECT_RESULT, ok ? 1 : 0, 0);
            }
        }
    });
    connectThread.detach();

    MSG msg{};
    while (!params.isClosed && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (parent && IsWindow(parent)) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }

    return params.connectSuccess && !params.cancelRequested.load();
}

DialogResult ShowError(HWND parent, const std::wstring& errorMessage, bool allowRetry) {
    DialogParams params{};
    params.type = DialogType::Error;
    params.title = L"Lỗi kết nối từ xa";
    params.message = errorMessage;
    params.allowRetry = allowRetry;
    return ExecuteModal(parent, params, 540, 270);
}

void ShowWarning(HWND parent, const std::wstring& warningMessage) {
    DialogParams params{};
    params.type = DialogType::Warning;
    params.title = L"Thông báo cảnh báo";
    params.message = warningMessage;
    ExecuteModal(parent, params, 500, 240);
}

void ShowInfo(HWND parent, const std::wstring& title, const std::wstring& infoMessage) {
    DialogParams params{};
    params.type = DialogType::Info;
    params.title = title;
    params.message = infoMessage;
    ExecuteModal(parent, params, 520, 250);
}

} // namespace Dialogs

