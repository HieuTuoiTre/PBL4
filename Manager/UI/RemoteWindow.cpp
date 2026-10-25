#include "RemoteWindow.h"
#include "InputHandler.h"
#include "TaskManagerWindow.h"
#include "FileExplorerWindow.h"
#include "../Media/AudioPlayer.h"
#include <gdiplus.h>
#include <string>
#include <vector>
#include <cmath>

namespace {
    HWND g_hwnd = NULL;
    TcpClient* g_client = nullptr;
    Gdiplus::Bitmap* g_frame = nullptr;
    IStream* g_decodeStream = nullptr; // Tái sử dụng 1 Stream duy nhất để tối ưu FPS
    CRITICAL_SECTION g_cs;

    int g_fps = 0;
    int g_frameCount = 0;
    DWORD g_lastTime = 0;

    int GetFrameWidth() { return g_frame ? (int)g_frame->GetWidth() : 0; }
    int GetFrameHeight() { return g_frame ? (int)g_frame->GetHeight() : 0; }

    // --- Hỗ trợ Dynamic DPI Scaling chuẩn hóa ---
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

    int GetToolbarHeight(HWND hwnd) {
        return (int)std::round(46.0f * GetDpiScale(hwnd));
    }

    // --- Định nghĩa nút Toolbar hiện đại ---
    enum ToolbarButtonId {
        BTN_NONE = 0,
        BTN_MODE,
        BTN_MUTE,
        BTN_VOL_DOWN,
        BTN_VOL_UP,
        BTN_TASK_MGR,
        BTN_FILE_EXP
    };

    struct ToolbarButton {
        ToolbarButtonId id;
        Gdiplus::RectF rect;
        std::wstring text;
        std::wstring hint;
        bool isSpecial;
    };

    ToolbarButtonId g_hoveredBtn = BTN_NONE;
    ToolbarButtonId g_pressedBtn = BTN_NONE;
    bool g_trackingMouseLeave = false;

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

    std::vector<ToolbarButton> BuildToolbarButtons(HWND hwnd, int clientWidth, int tbHeight) {
        std::vector<ToolbarButton> buttons;
        float scale = GetDpiScale(hwnd);
        float gap = 6.0f * scale;
        float btnHeight = 30.0f * scale;
        float btnY = (float)(tbHeight - btnHeight) / 2.0f;
        float curX = 12.0f * scale;

        // 1. Nút Chế độ điều khiển / Chỉ xem (dùng vector dot thay cho emoji để không bao giờ lỗi font)
        bool isCtrl = InputHandler::IsControlling();
        std::wstring modeText = isCtrl ? L"Đang điều khiển" : L"Chỉ xem";
        std::wstring modeHint = L"Ctrl+Alt";
        float btnModeW = (float)std::round((isCtrl ? 140.0f : 96.0f) * scale);
        buttons.push_back({ BTN_MODE, Gdiplus::RectF(curX, btnY, btnModeW, btnHeight), modeText, modeHint, true });
        curX += btnModeW + gap;

        // 2. Nút Bật / Tắt âm thanh
        bool isMuted = AudioPlayer::IsMuted();
        std::wstring muteText = isMuted ? L"Đã tắt tiếng" : L"Âm thanh";
        std::wstring muteHint = L"Ctrl+M";
        float btnMuteW = (float)std::round((isMuted ? 112.0f : 92.0f) * scale);
        buttons.push_back({ BTN_MUTE, Gdiplus::RectF(curX, btnY, btnMuteW, btnHeight), muteText, muteHint, isMuted });
        curX += btnMuteW + gap;

        // 3. Cụm điều chỉnh âm lượng: [ - ] [ 100% ] [ + ]
        float btnVolMiniW = (float)std::round(26.0f * scale);
        buttons.push_back({ BTN_VOL_DOWN, Gdiplus::RectF(curX, btnY, btnVolMiniW, btnHeight), L"−", L"Ctrl+↓", false });
        curX += btnVolMiniW + 2.0f * scale;

        int volPercent = (int)std::round(AudioPlayer::GetVolume() * 100.0f);
        float lblVolW = (float)std::round(52.0f * scale);
        buttons.push_back({ BTN_NONE, Gdiplus::RectF(curX, btnY, lblVolW, btnHeight), std::to_wstring(volPercent) + L"%", L"", false });
        curX += lblVolW + 2.0f * scale;

        buttons.push_back({ BTN_VOL_UP, Gdiplus::RectF(curX, btnY, btnVolMiniW, btnHeight), L"+", L"Ctrl+↑", false });
        curX += btnVolMiniW + gap * 2.0f;

        // 4. Nút Task Manager
        float btnTaskW = (float)std::round(120.0f * scale);
        buttons.push_back({ BTN_TASK_MGR, Gdiplus::RectF(curX, btnY, btnTaskW, btnHeight), L"Task Manager", L"Ctrl+Tab", false });
        curX += btnTaskW + gap;

        // 5. Nút File Explorer
        float btnFileW = (float)std::round(115.0f * scale);
        buttons.push_back({ BTN_FILE_EXP, Gdiplus::RectF(curX, btnY, btnFileW, btnHeight), L"File Explorer", L"Ctrl+E", false });
        curX += btnFileW + gap;

        return buttons;
    }

    ToolbarButtonId HitTestToolbar(HWND hwnd, int x, int y, int clientWidth, int tbHeight) {
        if (y < 0 || y >= tbHeight) return BTN_NONE;
        auto buttons = BuildToolbarButtons(hwnd, clientWidth, tbHeight);
        Gdiplus::PointF pt((float)x, (float)y);
        for (const auto& btn : buttons) {
            if (btn.id != BTN_NONE && btn.rect.Contains(pt)) {
                return btn.id;
            }
        }
        return BTN_NONE;
    }

    void RenderToolbar(HWND hwnd, HDC hdc, const RECT& r, int tbHeight) {
        float scale = GetDpiScale(hwnd);
        int width = r.right;
        if (width <= 0 || tbHeight <= 0) return;

        // Double-buffering toàn bộ thanh toolbar bằng GDI+ Bitmap trong bộ nhớ (Tuyệt đối không nhấp nháy)
        Gdiplus::Bitmap memBmp(width, tbHeight, PixelFormat32bppPARGB);
        Gdiplus::Graphics g(&memBmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        // Nền Toolbar: Slate Graphite cao cấp
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 24, 25, 30));
        g.FillRectangle(&bgBrush, 0, 0, width, tbHeight);

        // Viền dưới 1px tinh tế ngăn cách màn hình điều khiển
        Gdiplus::Pen borderPen(Gdiplus::Color(255, 45, 47, 56), 1.0f);
        g.DrawLine(&borderPen, 0.0f, (float)(tbHeight - 1), (float)width, (float)(tbHeight - 1));

        auto buttons = BuildToolbarButtons(hwnd, width, tbHeight);

        Gdiplus::FontFamily fontFamily(L"Segoe UI");
        Gdiplus::Font fontBtn(&fontFamily, 12.0f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font fontVol(&fontFamily, 12.0f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

        Gdiplus::StringFormat sfCenter;
        sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
        sfCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        sfCenter.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

        Gdiplus::GraphicsPath path;
        float radius = 5.0f * scale;

        for (const auto& btn : buttons) {
            // Không vẽ các nút vượt quá chiều rộng cho phép trước badge FPS
            if (btn.rect.GetRight() > (float)(width - 125 * scale)) continue;

            AddRoundRectPath(path, btn.rect, radius);

            if (btn.id == BTN_NONE) {
                // Nhãn % âm lượng
                Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 200, 205, 215));
                g.DrawString(btn.text.c_str(), -1, &fontVol, btn.rect, &sfCenter, &textBrush);
                continue;
            }

            bool isHovered = (g_hoveredBtn == btn.id);
            bool isPressed = (g_pressedBtn == btn.id);

            Gdiplus::Color fillColor;
            Gdiplus::Color borderColor;
            Gdiplus::Color textColor;

            if (btn.id == BTN_MODE) {
                bool isCtrl = InputHandler::IsControlling();
                if (isCtrl) {
                    fillColor = isPressed ? Gdiplus::Color(255, 14, 52, 28) : (isHovered ? Gdiplus::Color(255, 20, 68, 36) : Gdiplus::Color(255, 16, 58, 31));
                    borderColor = Gdiplus::Color(255, 34, 197, 94);
                    textColor = Gdiplus::Color(255, 134, 239, 172);
                } else {
                    fillColor = isPressed ? Gdiplus::Color(255, 30, 32, 38) : (isHovered ? Gdiplus::Color(255, 50, 53, 64) : Gdiplus::Color(255, 38, 40, 48));
                    borderColor = Gdiplus::Color(255, 75, 80, 95);
                    textColor = Gdiplus::Color(255, 160, 165, 180);
                }
            } else if (btn.id == BTN_MUTE && AudioPlayer::IsMuted()) {
                fillColor = isPressed ? Gdiplus::Color(255, 60, 20, 20) : (isHovered ? Gdiplus::Color(255, 80, 28, 28) : Gdiplus::Color(255, 68, 24, 24));
                borderColor = Gdiplus::Color(255, 239, 68, 68);
                textColor = Gdiplus::Color(255, 252, 165, 165);
            } else {
                fillColor = isPressed ? Gdiplus::Color(255, 28, 30, 36) : (isHovered ? Gdiplus::Color(255, 52, 55, 66) : Gdiplus::Color(255, 38, 40, 48));
                borderColor = isHovered ? Gdiplus::Color(255, 85, 90, 108) : Gdiplus::Color(255, 58, 61, 74);
                textColor = isHovered ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 220, 224, 232);
            }

            Gdiplus::SolidBrush fillBrush(fillColor);
            g.FillPath(&fillBrush, &path);
            Gdiplus::Pen pen(borderColor, 1.0f);
            g.DrawPath(&pen, &path);

            Gdiplus::SolidBrush textBrush(textColor);

            if (btn.id == BTN_MODE) {
                // Vẽ dot tròn trạng thái vector trước chữ
                bool isCtrl = InputHandler::IsControlling();
                float dotSize = 7.0f * scale;
                float dotX = btn.rect.X + 10.0f * scale;
                float dotY = btn.rect.Y + (btn.rect.Height - dotSize) / 2.0f;
                Gdiplus::Color dotColor = isCtrl ? Gdiplus::Color(255, 34, 197, 94) : Gdiplus::Color(255, 148, 154, 168);
                Gdiplus::SolidBrush dotBrush(dotColor);
                g.FillEllipse(&dotBrush, dotX, dotY, dotSize, dotSize);

                Gdiplus::RectF textRect = btn.rect;
                textRect.X += (dotSize + 4.0f * scale);
                textRect.Width -= (dotSize + 4.0f * scale);
                g.DrawString(btn.text.c_str(), -1, &fontBtn, textRect, &sfCenter, &textBrush);
            } else {
                g.DrawString(btn.text.c_str(), -1, &fontBtn, btn.rect, &sfCenter, &textBrush);
            }
        }

        // 3. Huy hiệu FPS & Trạng thái phát trực tiếp bên phải
        float badgeW = 96.0f * scale;
        float badgeH = 28.0f * scale;
        float badgeX = (float)width - badgeW - (12.0f * scale);
        float badgeY = (float)(tbHeight - badgeH) / 2.0f;
        Gdiplus::RectF badgeRect(badgeX, badgeY, badgeW, badgeH);

        AddRoundRectPath(path, badgeRect, radius);
        Gdiplus::SolidBrush badgeBg(Gdiplus::Color(255, 18, 19, 23));
        g.FillPath(&badgeBg, &path);
        Gdiplus::Pen badgeBorder(Gdiplus::Color(255, 42, 44, 54), 1.0f);
        g.DrawPath(&badgeBorder, &path);

        Gdiplus::Color dotColor = (g_fps >= 20) ? Gdiplus::Color(255, 34, 197, 94) : Gdiplus::Color(255, 245, 158, 11);
        Gdiplus::SolidBrush dotBrush(dotColor);
        float dotSize = 7.0f * scale;
        float dotX = badgeX + 12.0f * scale;
        float dotY = badgeY + (badgeH - dotSize) / 2.0f;
        g.FillEllipse(&dotBrush, dotX, dotY, dotSize, dotSize);

        std::wstring fpsStr = std::to_wstring(g_fps) + L" FPS";
        Gdiplus::RectF fpsTextRect(dotX + dotSize + 6.0f * scale, badgeY, badgeW - dotSize - 18.0f * scale, badgeH);
        Gdiplus::StringFormat sfLeft;
        sfLeft.SetAlignment(Gdiplus::StringAlignmentNear);
        sfLeft.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::SolidBrush fpsTextBrush(Gdiplus::Color(255, 215, 220, 230));
        g.DrawString(fpsStr.c_str(), -1, &fontBtn, fpsTextRect, &sfLeft, &fpsTextBrush);

        // Blit từ bộ nhớ ra màn hình
        Gdiplus::Graphics targetG(hdc);
        targetG.DrawImage(&memBmp, 0, 0);
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        int tbHeight = GetToolbarHeight(hwnd);

        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT r;
                GetClientRect(hwnd, &r);

                if (ps.rcPaint.top < tbHeight) {
                    RenderToolbar(hwnd, hdc, r, tbHeight);
                }

                if (ps.rcPaint.bottom > tbHeight) {
                    EnterCriticalSection(&g_cs);
                    if (g_frame) {
                        Gdiplus::Graphics g(hdc);
                        g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
                        g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
                        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
                        g.DrawImage(g_frame, 0, tbHeight, r.right, r.bottom - tbHeight);
                    }
                    LeaveCriticalSection(&g_cs);
                }

                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_ERASEBKGND: return 1;

            case WM_DPICHANGED: {
                RECT* const prcNewWindow = (RECT*)lp;
                SetWindowPos(hwnd, NULL,
                    prcNewWindow->left, prcNewWindow->top,
                    prcNewWindow->right - prcNewWindow->left,
                    prcNewWindow->bottom - prcNewWindow->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            case WM_SETCURSOR: {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                if (pt.y >= 0 && pt.y < tbHeight) {
                    RECT r;
                    GetClientRect(hwnd, &r);
                    ToolbarButtonId hit = HitTestToolbar(hwnd, pt.x, pt.y, r.right, tbHeight);
                    if (hit != BTN_NONE) {
                        SetCursor(LoadCursor(NULL, IDC_HAND));
                        return TRUE;
                    } else {
                        SetCursor(LoadCursor(NULL, IDC_ARROW));
                        return TRUE;
                    }
                }
                break;
            }

            case WM_MOUSEMOVE: {
                int mouseX = (short)LOWORD(lp);
                int mouseY = (short)HIWORD(lp);

                if (mouseY < tbHeight) {
                    RECT r;
                    GetClientRect(hwnd, &r);
                    ToolbarButtonId hit = HitTestToolbar(hwnd, mouseX, mouseY, r.right, tbHeight);
                    if (hit != g_hoveredBtn) {
                        g_hoveredBtn = hit;
                        RECT tbRect = { 0, 0, r.right, tbHeight };
                        InvalidateRect(hwnd, &tbRect, FALSE);
                    }
                    if (!g_trackingMouseLeave) {
                        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                        TrackMouseEvent(&tme);
                        g_trackingMouseLeave = true;
                    }
                    return 0;
                }

                if (g_hoveredBtn != BTN_NONE) {
                    g_hoveredBtn = BTN_NONE;
                    RECT r;
                    GetClientRect(hwnd, &r);
                    RECT tbRect = { 0, 0, r.right, tbHeight };
                    InvalidateRect(hwnd, &tbRect, FALSE);
                }

                InputHandler::SendMouse(Protocol::MOUSE_ACTION_MOVE, Protocol::BUTTON_NONE, lp, hwnd, GetFrameWidth(), GetFrameHeight(), tbHeight);
                return 0;
            }

            case WM_MOUSELEAVE: {
                g_trackingMouseLeave = false;
                if (g_hoveredBtn != BTN_NONE || g_pressedBtn != BTN_NONE) {
                    g_hoveredBtn = BTN_NONE;
                    g_pressedBtn = BTN_NONE;
                    RECT r;
                    GetClientRect(hwnd, &r);
                    RECT tbRect = { 0, 0, r.right, tbHeight };
                    InvalidateRect(hwnd, &tbRect, FALSE);
                }
                return 0;
            }

            case WM_LBUTTONDOWN: {
                int mouseX = (short)LOWORD(lp);
                int mouseY = (short)HIWORD(lp);

                if (mouseY < tbHeight) {
                    RECT r;
                    GetClientRect(hwnd, &r);
                    ToolbarButtonId hit = HitTestToolbar(hwnd, mouseX, mouseY, r.right, tbHeight);
                    if (hit != BTN_NONE) {
                        g_pressedBtn = hit;
                        RECT tbRect = { 0, 0, r.right, tbHeight };
                        InvalidateRect(hwnd, &tbRect, FALSE);
                    }
                    return 0;
                }

                InputHandler::SendMouse(Protocol::MOUSE_ACTION_PRESS, Protocol::BUTTON_LEFT, lp, hwnd, GetFrameWidth(), GetFrameHeight(), tbHeight);
                return 0;
            }

            case WM_LBUTTONUP: {
                int mouseX = (short)LOWORD(lp);
                int mouseY = (short)HIWORD(lp);

                if (g_pressedBtn != BTN_NONE) {
                    RECT r;
                    GetClientRect(hwnd, &r);
                    ToolbarButtonId hit = HitTestToolbar(hwnd, mouseX, mouseY, r.right, tbHeight);
                    if (hit == g_pressedBtn) {
                        switch (hit) {
                            case BTN_MODE:
                                InputHandler::ToggleControlling();
                                break;
                            case BTN_MUTE:
                                AudioPlayer::ToggleMute();
                                break;
                            case BTN_VOL_DOWN:
                                AudioPlayer::DecreaseVolume();
                                break;
                            case BTN_VOL_UP:
                                AudioPlayer::IncreaseVolume();
                                break;
                            case BTN_TASK_MGR:
                                TaskManagerWindow::Show(hwnd, g_client);
                                break;
                            case BTN_FILE_EXP:
                                FileExplorerWindow::Show(hwnd, g_client);
                                break;
                            default:
                                break;
                        }
                    }
                    g_pressedBtn = BTN_NONE;
                    RECT tbRect = { 0, 0, r.right, tbHeight };
                    InvalidateRect(hwnd, &tbRect, FALSE);
                    return 0;
                }

                InputHandler::SendMouse(Protocol::MOUSE_ACTION_RELEASE, Protocol::BUTTON_LEFT, lp, hwnd, GetFrameWidth(), GetFrameHeight(), tbHeight);
                return 0;
            }

            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000)) {
                    if ((lp & (1 << 30)) == 0) {
                        InputHandler::ToggleControlling();
                        RECT r; GetClientRect(hwnd, &r);
                        RECT toolbarRect = {0, 0, r.right, tbHeight};
                        InvalidateRect(hwnd, &toolbarRect, FALSE);
                    }
                    return 0;
                }

                if (wp == VK_TAB && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                    TaskManagerWindow::Show(hwnd, g_client);
                    return 0;
                }

                if (wp == 'E' && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                    FileExplorerWindow::Show(hwnd, g_client);
                    return 0;
                }

                if (wp == 'M' && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                    if ((lp & (1 << 30)) == 0) {
                        AudioPlayer::ToggleMute();
                        RECT r; GetClientRect(hwnd, &r);
                        RECT toolbarRect = {0, 0, r.right, tbHeight};
                        InvalidateRect(hwnd, &toolbarRect, FALSE);
                    }
                    return 0;
                }

                if (wp == VK_UP && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                    AudioPlayer::IncreaseVolume();
                    RECT r; GetClientRect(hwnd, &r);
                    RECT toolbarRect = {0, 0, r.right, tbHeight};
                    InvalidateRect(hwnd, &toolbarRect, FALSE);
                    return 0;
                }

                if (wp == VK_DOWN && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                    AudioPlayer::DecreaseVolume();
                    RECT r; GetClientRect(hwnd, &r);
                    RECT toolbarRect = {0, 0, r.right, tbHeight};
                    InvalidateRect(hwnd, &toolbarRect, FALSE);
                    return 0;
                }

                if (InputHandler::IsControlling()) {
                    InputHandler::SendKey(wp, 1);
                    return 0;
                }
                break;
            }
            case WM_KEYUP:
            case WM_SYSKEYUP: {
                if (InputHandler::IsControlling()) {
                    InputHandler::SendKey(wp, 0);
                    return 0;
                }
                break;
            }

            case WM_RBUTTONDOWN: InputHandler::SendMouse(Protocol::MOUSE_ACTION_PRESS, Protocol::BUTTON_RIGHT, lp, hwnd, GetFrameWidth(), GetFrameHeight(), tbHeight); return 0;
            case WM_RBUTTONUP:   InputHandler::SendMouse(Protocol::MOUSE_ACTION_RELEASE, Protocol::BUTTON_RIGHT, lp, hwnd, GetFrameWidth(), GetFrameHeight(), tbHeight); return 0;
            case WM_MOUSEWHEEL:  InputHandler::SendMouse(Protocol::MOUSE_ACTION_WHEEL, Protocol::BUTTON_NONE, lp, hwnd, GetFrameWidth(), GetFrameHeight(), tbHeight, GET_WHEEL_DELTA_WPARAM(wp)); return 0;

            case WM_DESTROY: PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

namespace RemoteWindow {
    HWND Create(HINSTANCE hInst, TcpClient* client) {
        g_client = client;
        InitializeCriticalSection(&g_cs);
        CreateStreamOnHGlobal(NULL, TRUE, &g_decodeStream);
        g_lastTime = GetTickCount();

        WNDCLASSW wc = {};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"MgrWindow";
        RegisterClassW(&wc);

        g_hwnd = CreateWindowExW(0, L"MgrWindow", L"LanRemote",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_MAXIMIZE,
            CW_USEDEFAULT, CW_USEDEFAULT, 1920, 1080, 0, 0, hInst, 0);
        return g_hwnd;
    }

    void UpdateFrame(const Protocol::VideoDeltaHeader& delta, const char* jpegData, int jpegSize) {
        if (jpegSize <= 0 || !jpegData || !g_decodeStream) return;

        // Tái sử dụng g_decodeStream để không cấp phát/giải phóng HGLOBAL mỗi frame
        LARGE_INTEGER liZero = {};
        ULARGE_INTEGER uliSize = {};
        uliSize.QuadPart = (ULONGLONG)jpegSize;

        g_decodeStream->Seek(liZero, STREAM_SEEK_SET, NULL);
        g_decodeStream->SetSize(uliSize);

        ULONG written = 0;
        if (FAILED(g_decodeStream->Write(jpegData, (ULONG)jpegSize, &written)) || (int)written != jpegSize) {
            return;
        }
        g_decodeStream->Seek(liZero, STREAM_SEEK_SET, NULL);

        Gdiplus::Image* img = Gdiplus::Image::FromStream(g_decodeStream);
        if (img && img->GetLastStatus() == Gdiplus::Ok) {
            int reqW = delta.x + delta.width;
            int reqH = delta.y + delta.height;

            EnterCriticalSection(&g_cs);
            if (!g_frame) {
                g_frame = new Gdiplus::Bitmap(reqW, reqH, PixelFormat32bppPARGB);
            } else if ((int)g_frame->GetWidth() < reqW || (int)g_frame->GetHeight() < reqH) {
                // Tự động mở rộng khung nền nếu frame đầu tiên là Delta nhỏ
                int newW = ((int)g_frame->GetWidth() > reqW) ? (int)g_frame->GetWidth() : reqW;
                int newH = ((int)g_frame->GetHeight() > reqH) ? (int)g_frame->GetHeight() : reqH;
                Gdiplus::Bitmap* expanded = new Gdiplus::Bitmap(newW, newH, PixelFormat32bppPARGB);
                Gdiplus::Graphics gExp(expanded);
                gExp.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
                gExp.DrawImage(g_frame, 0, 0);
                delete g_frame;
                g_frame = expanded;
            }

            Gdiplus::Graphics g(g_frame);
            g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
            g.DrawImage(img, delta.x, delta.y, delta.width, delta.height);
            LeaveCriticalSection(&g_cs);

            g_frameCount++;
            DWORD currentTime = GetTickCount();
            bool fpsUpdated = false;
            if (currentTime - g_lastTime >= 1000) {
                g_fps = g_frameCount;
                g_frameCount = 0;
                g_lastTime = currentTime;
                fpsUpdated = true;
            }

            if (g_hwnd) {
                int tbHeight = GetToolbarHeight(g_hwnd);
                if (fpsUpdated) {
                    RECT r;
                    GetClientRect(g_hwnd, &r);
                    RECT tbRect = { 0, 0, r.right, tbHeight };
                    InvalidateRect(g_hwnd, &tbRect, FALSE);
                }
                RECT clientRect;
                GetClientRect(g_hwnd, &clientRect);
                clientRect.top = tbHeight;
                InvalidateRect(g_hwnd, &clientRect, FALSE);
            }
        }
        if (img) delete img;
    }

    void Cleanup() {
        if (g_decodeStream) {
            g_decodeStream->Release();
            g_decodeStream = nullptr;
        }
        if (g_frame) {
            delete g_frame;
            g_frame = nullptr;
        }
        DeleteCriticalSection(&g_cs);
    }
}
