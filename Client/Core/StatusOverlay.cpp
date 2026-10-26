#include "StatusOverlay.h"
#include <chrono>

static const wchar_t* OVERLAY_CLASS_NAME = L"ClientStatusOverlayClass";
static const wchar_t* ALERT_CLASS_NAME = L"ConnectionAlertPopupClass";
static std::wstring g_alertMessage = L"";

StatusOverlay::StatusOverlay() 
    : m_hwnd(nullptr), m_hInstance(nullptr), 
      m_currentState(OverlayState::Disconnected), m_visible(false), m_isInitialized(false) {}

StatusOverlay::~StatusOverlay() {
    Destroy();
}

bool StatusOverlay::Initialize(HINSTANCE hInstance) {
    if (m_isInitialized.load()) return true;
    if (!hInstance) hInstance = GetModuleHandleW(NULL);
    m_hInstance = hInstance;

    std::atomic<bool> ready(false);

    m_uiThread = std::thread([this, hInstance, &ready]() {
        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = StatusOverlay::WndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = OVERLAY_CLASS_NAME;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr; // Double-buffering/manual paint prevents flicker

        RegisterClassExW(&wc);

        // Position: Top-right corner of primary monitor
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int posX = screenW - m_width - m_margin;
        int posY = m_margin;

        // Extended styles: topmost, click-through, toolwindow, no-activate
        DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;

        m_hwnd = CreateWindowExW(
            exStyle,
            OVERLAY_CLASS_NAME,
            L"RemoteStatusOverlay",
            WS_POPUP,
            posX, posY,
            m_width, m_height,
            nullptr,
            nullptr,
            hInstance,
            this
        );

        if (!m_hwnd) {
            ready = true;
            return;
        }

        if (m_visible.load()) {
            ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
            UpdateWindow(m_hwnd);
        }

        m_isInitialized = true;
        ready = true;

        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    });

    while (!ready.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return m_isInitialized.load();
}

void StatusOverlay::SetState(OverlayState newState) {
    if (m_currentState.load() == newState) return;

    m_currentState.store(newState);
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

void StatusOverlay::SetVisible(bool visible) {
    m_visible.store(visible);
    if (m_hwnd) {
        PostMessageW(m_hwnd, WM_USER + 1, visible ? 1 : 0, 0);
    }
}

void StatusOverlay::Destroy() {
    if (m_hwnd) {
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        if (m_uiThread.joinable()) {
            m_uiThread.join();
        }
        m_hwnd = nullptr;
    }
    if (m_hInstance) {
        UnregisterClassW(OVERLAY_CLASS_NAME, m_hInstance);
        m_hInstance = nullptr;
    }
    m_isInitialized.store(false);
}

LRESULT CALLBACK StatusOverlay::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    StatusOverlay* pThis = nullptr;

    if (msg == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<StatusOverlay*>(createStruct->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<StatusOverlay*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    switch (msg) {
    case WM_PAINT:
        if (pThis) {
            pThis->OnPaint(hwnd);
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        return 1; // Prevent GDI flickering

    case WM_USER + 1: // SetVisible custom message
        ShowWindow(hwnd, wParam ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (wParam) {
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void StatusOverlay::OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    // Setup off-screen memory DC to prevent flicker
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, m_width, m_height);
    HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);

    // Background pill/card (Dark Slate)
    RECT rect = { 0, 0, m_width, m_height };
    HBRUSH bgBrush = CreateSolidBrush(RGB(30, 30, 34));
    FillRect(memDC, &rect, bgBrush);
    DeleteObject(bgBrush);

    // Determine State Colors and Label
    COLORREF dotColor;
    const wchar_t* statusText = L"";

    switch (m_currentState.load()) {
    case OverlayState::Disconnected:
        dotColor = RGB(120, 120, 120);              // Gray
        statusText = L"Ready (No Manager)";
        break;
    case OverlayState::ConnectedViewing:
        dotColor = RGB(0, 175, 240);                // Cyan / Blue
        statusText = L"Manager: Monitoring";
        break;
    case OverlayState::ConnectedControlling:
        dotColor = RGB(235, 75, 75);                // Red
        statusText = L"Manager: Controlling";
        break;
    }

    // 1. Draw Status Indicator Dot
    HBRUSH dotBrush = CreateSolidBrush(dotColor);
    HGDIOBJ oldBrush = SelectObject(memDC, dotBrush);
    HPEN nullPen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
    HGDIOBJ oldPen = SelectObject(memDC, nullPen);

    int dotRadius = 5;
    int dotCenterX = 20;
    int dotCenterY = m_height / 2;
    Ellipse(memDC, dotCenterX - dotRadius, dotCenterY - dotRadius, 
                   dotCenterX + dotRadius, dotCenterY + dotRadius);

    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBrush);
    DeleteObject(dotBrush);
    DeleteObject(nullPen);

    // 2. Draw Text Label
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(240, 240, 240));

    HFONT hFont = CreateFontW(16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(memDC, hFont);

    RECT textRect = { 36, 0, m_width - 10, m_height };
    DrawTextW(memDC, statusText, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    SelectObject(memDC, oldFont);
    DeleteObject(hFont);

    // 3. Draw thin border
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(60, 60, 68));
    HGDIOBJ oldBorderPen = SelectObject(memDC, borderPen);
    SelectObject(memDC, GetStockObject(NULL_BRUSH));
    Rectangle(memDC, 0, 0, m_width, m_height);
    SelectObject(memDC, oldBorderPen);
    DeleteObject(borderPen);

    // Transfer back to window DC
    BitBlt(hdc, 0, 0, m_width, m_height, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);

    EndPaint(hwnd, &ps);
}

void ConnectionAlert::Show(HINSTANCE hInstance, const std::wstring& message) {
    if (!hInstance) hInstance = GetModuleHandleW(NULL);

    std::thread([hInstance, message]() {
        g_alertMessage = message;

        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = ConnectionAlert::WndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = ALERT_CLASS_NAME;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

        RegisterClassExW(&wc);

        // Calculate position: bottom-right corner above taskbar
        RECT workArea;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
        int posX = workArea.right - POPUP_WIDTH - 20;
        int posY = workArea.bottom - POPUP_HEIGHT - 20;

        HWND hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            ALERT_CLASS_NAME,
            L"ConnectionAlert",
            WS_POPUP,
            posX, posY,
            POPUP_WIDTH, POPUP_HEIGHT,
            nullptr, nullptr, hInstance, nullptr
        );

        if (hwnd) {
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            UpdateWindow(hwnd);
            // Automatically close after 5 seconds (5000 ms)
            SetTimer(hwnd, TIMER_DISMISS, 5000, nullptr);

            MSG msg;
            while (GetMessageW(&msg, NULL, 0, 0) > 0) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }).detach();
}

LRESULT CALLBACK ConnectionAlert::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT:
        Draw(hwnd);
        return 0;

    case WM_LBUTTONDOWN:
        // Dismiss immediately if user clicks the toast
        DestroyWindow(hwnd);
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_DISMISS) {
            KillTimer(hwnd, TIMER_DISMISS);
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void ConnectionAlert::Draw(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rect;
    GetClientRect(hwnd, &rect);

    // Double buffering
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBm = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
    HGDIOBJ oldBm = SelectObject(memDC, memBm);

    // 1. Dark background
    HBRUSH bgBrush = CreateSolidBrush(RGB(24, 24, 28));
    FillRect(memDC, &rect, bgBrush);
    DeleteObject(bgBrush);

    // 2. Accent border (Cyan / Blue)
    HPEN borderPen = CreatePen(PS_SOLID, 2, RGB(0, 150, 255));
    HGDIOBJ oldPen = SelectObject(memDC, borderPen);
    SelectObject(memDC, GetStockObject(NULL_BRUSH));
    Rectangle(memDC, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(memDC, oldPen);
    DeleteObject(borderPen);

    // 3. Header title
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(0, 195, 255));
    HFONT hTitleFont = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(memDC, hTitleFont);

    RECT titleRect = { 16, 10, rect.right - 16, 28 };
    DrawTextW(memDC, L"REMOTE SESSION ACTIVE", -1, &titleRect, DT_SINGLELINE | DT_LEFT);

    // 4. Alert message
    SetTextColor(memDC, RGB(230, 230, 230));
    HFONT hBodyFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SelectObject(memDC, hBodyFont);

    RECT bodyRect = { 16, 32, rect.right - 16, rect.bottom - 10 };
    DrawTextW(memDC, g_alertMessage.c_str(), -1, &bodyRect, DT_SINGLELINE | DT_LEFT);

    // Blit to screen
    BitBlt(hdc, 0, 0, rect.right, rect.bottom, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldFont);
    DeleteObject(hTitleFont);
    DeleteObject(hBodyFont);
    SelectObject(memDC, oldBm);
    DeleteObject(memBm);
    DeleteDC(memDC);

    EndPaint(hwnd, &ps);
}
