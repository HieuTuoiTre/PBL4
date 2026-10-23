#include "RemoteWindow.h"
#include "InputHandler.h"
#include "TaskManagerWindow.h"
#include "FileExplorerWindow.h"
#include "../Media/AudioPlayer.h"
#include <gdiplus.h>
#include <string>
#include <cmath>

#define TOOLBAR_HEIGHT 35

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

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT r;
                GetClientRect(hwnd, &r);

                if (ps.rcPaint.top < TOOLBAR_HEIGHT) {
                    RECT toolbarRect = {0, 0, r.right, TOOLBAR_HEIGHT};
                    HBRUSH brush = CreateSolidBrush(RGB(45, 45, 48));
                    FillRect(hdc, &toolbarRect, brush);
                    DeleteObject(brush);

                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(255, 255, 255));
                    HFONT hFont = CreateFontW(15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
                    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

                    int volPercent = (int)std::round(AudioPlayer::GetVolume() * 100.0f);
                    std::wstring statusStr = (InputHandler::IsControlling() ? L"🔴 ĐIỀU KHIỂN" : L"👁️ CHỈ XEM")
                                           + std::wstring(AudioPlayer::IsMuted() ? L" | 🔇 MUTE" : (L" | 🔊 " + std::to_wstring(volPercent) + L"%"));
                    std::wstring shortcutStr = L"[Ctrl+Tab: TaskMgr] | [Ctrl+E: File] | [Ctrl+M: Loa] | [Ctrl+↑/↓: Âm lượng]";
                    std::wstring fpsStr = L"FPS: " + std::to_wstring(g_fps);

                    RECT leftRect = toolbarRect; leftRect.left += 12;
                    DrawTextW(hdc, statusStr.c_str(), -1, &leftRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    DrawTextW(hdc, shortcutStr.c_str(), -1, &toolbarRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    RECT rightRect = toolbarRect; rightRect.right -= 12;
                    DrawTextW(hdc, fpsStr.c_str(), -1, &rightRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                    SelectObject(hdc, hOldFont);
                    DeleteObject(hFont);
                }

                if (ps.rcPaint.bottom > TOOLBAR_HEIGHT) {
                    EnterCriticalSection(&g_cs);
                    if (g_frame) {
                        Gdiplus::Graphics g(hdc);
                        g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
                        g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
                        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
                        g.DrawImage(g_frame, 0, TOOLBAR_HEIGHT, r.right, r.bottom - TOOLBAR_HEIGHT);
                    }
                    LeaveCriticalSection(&g_cs);
                }

                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_ERASEBKGND: return 1;

            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000)) {
                    if ((lp & (1 << 30)) == 0) {
                        InputHandler::ToggleControlling();
                        RECT toolbarRect = {0, 0, 4000, TOOLBAR_HEIGHT};
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
                        RECT toolbarRect = {0, 0, 4000, TOOLBAR_HEIGHT};
                        InvalidateRect(hwnd, &toolbarRect, FALSE);
                    }
                    return 0;
                }

                if (wp == VK_UP && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                    AudioPlayer::IncreaseVolume();
                    RECT toolbarRect = {0, 0, 4000, TOOLBAR_HEIGHT};
                    InvalidateRect(hwnd, &toolbarRect, FALSE);
                    return 0;
                }

                if (wp == VK_DOWN && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                    AudioPlayer::DecreaseVolume();
                    RECT toolbarRect = {0, 0, 4000, TOOLBAR_HEIGHT};
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

            case WM_MOUSEMOVE:   InputHandler::SendMouse(Protocol::MOUSE_ACTION_MOVE, Protocol::BUTTON_NONE, lp, hwnd, GetFrameWidth(), GetFrameHeight(), TOOLBAR_HEIGHT); return 0;
            case WM_LBUTTONDOWN: InputHandler::SendMouse(Protocol::MOUSE_ACTION_PRESS, Protocol::BUTTON_LEFT, lp, hwnd, GetFrameWidth(), GetFrameHeight(), TOOLBAR_HEIGHT); return 0;
            case WM_LBUTTONUP:   InputHandler::SendMouse(Protocol::MOUSE_ACTION_RELEASE, Protocol::BUTTON_LEFT, lp, hwnd, GetFrameWidth(), GetFrameHeight(), TOOLBAR_HEIGHT); return 0;
            case WM_RBUTTONDOWN: InputHandler::SendMouse(Protocol::MOUSE_ACTION_PRESS, Protocol::BUTTON_RIGHT, lp, hwnd, GetFrameWidth(), GetFrameHeight(), TOOLBAR_HEIGHT); return 0;
            case WM_RBUTTONUP:   InputHandler::SendMouse(Protocol::MOUSE_ACTION_RELEASE, Protocol::BUTTON_RIGHT, lp, hwnd, GetFrameWidth(), GetFrameHeight(), TOOLBAR_HEIGHT); return 0;
            case WM_MOUSEWHEEL:  InputHandler::SendMouse(Protocol::MOUSE_ACTION_WHEEL, Protocol::BUTTON_NONE, lp, hwnd, GetFrameWidth(), GetFrameHeight(), TOOLBAR_HEIGHT, GET_WHEEL_DELTA_WPARAM(wp)); return 0;

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
                if (fpsUpdated) {
                    InvalidateRect(g_hwnd, NULL, FALSE);
                } else {
                    RECT clientRect;
                    GetClientRect(g_hwnd, &clientRect);
                    clientRect.top = TOOLBAR_HEIGHT;
                    InvalidateRect(g_hwnd, &clientRect, FALSE);
                }
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