#include "RemoteWindow.h"
#include "InputHandler.h"
#include "TaskManagerWindow.h"
#include "FileExplorerWindow.h"
#include "../Media/AudioPlayer.h"
#include <gdiplus.h>
#include <string>

#define TOOLBAR_HEIGHT 35

namespace {
    HWND g_hwnd = NULL;
    TcpClient* g_client = nullptr;
    Gdiplus::Image* g_frame = nullptr;
    CRITICAL_SECTION g_cs;

    int g_fps = 0;
    int g_frameCount = 0;
    DWORD g_lastTime = 0;

    int GetFrameWidth() { return g_frame ? (int)g_frame->GetWidth() : 0; }
    int GetFrameHeight() { return g_frame ? (int)g_frame->GetHeight() : 0; }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
                RECT r; GetClientRect(hwnd, &r);

                RECT toolbarRect = {0, 0, r.right, TOOLBAR_HEIGHT};
                HBRUSH brush = CreateSolidBrush(RGB(45, 45, 48));
                FillRect(hdc, &toolbarRect, brush);
                DeleteObject(brush);

                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(255, 255, 255));
                HFONT hFont = CreateFontW(15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
                HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

                std::wstring statusStr = (InputHandler::IsControlling() ? L"🔴 ĐIỀU KHIỂN" : L"👁️ CHỈ XEM")
                                       + std::wstring(AudioPlayer::IsMuted() ? L" | 🔇 MUTE" : L" | 🔊 LOA");
                std::wstring shortcutStr = L"[Ctrl+Tab: TaskMgr] | [Ctrl+E: File] | [Ctrl+M: Loa] | [Ctrl+Alt: Chế độ]";
                std::wstring fpsStr = L"FPS: " + std::to_wstring(g_fps);

                RECT leftRect = toolbarRect; leftRect.left += 12;
                DrawTextW(hdc, statusStr.c_str(), -1, &leftRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                DrawTextW(hdc, shortcutStr.c_str(), -1, &toolbarRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                RECT rightRect = toolbarRect; rightRect.right -= 12;
                DrawTextW(hdc, fpsStr.c_str(), -1, &rightRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                SelectObject(hdc, hOldFont);
                DeleteObject(hFont);

                EnterCriticalSection(&g_cs);
                if (g_frame) {
                    Gdiplus::Graphics g(hdc);
                    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
                    g.DrawImage(g_frame, 0, TOOLBAR_HEIGHT, r.right, r.bottom - TOOLBAR_HEIGHT);
                }
                LeaveCriticalSection(&g_cs);

                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_ERASEBKGND: return 1;

            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000)) {
                    if ((lp & (1 << 30)) == 0) {
                        InputHandler::ToggleControlling();
                        RECT toolbarRect = {0, 0, 2000, TOOLBAR_HEIGHT};
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
                        RECT toolbarRect = {0, 0, 2000, TOOLBAR_HEIGHT};
                        InvalidateRect(hwnd, &toolbarRect, FALSE);
                    }
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
        g_lastTime = GetTickCount();

        WNDCLASSW wc = {};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"MgrWindow";
        RegisterClassW(&wc);

        g_hwnd = CreateWindowExW(0, L"MgrWindow", L"LanRemote",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, 0, 0, hInst, 0);
        return g_hwnd;
    }

    void UpdateFrame(const Protocol::VideoDeltaHeader& delta, const char* jpegData, int jpegSize) {
        if (jpegSize <= 0) return;
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, jpegSize);
        if (!hMem) return;
        memcpy(GlobalLock(hMem), jpegData, jpegSize);
        GlobalUnlock(hMem);

        IStream* pStream = nullptr;
        if (CreateStreamOnHGlobal(hMem, TRUE, &pStream) == S_OK) {
            Gdiplus::Image* img = Gdiplus::Image::FromStream(pStream);
            if (img && img->GetLastStatus() == Gdiplus::Ok) {
                EnterCriticalSection(&g_cs);
                if (!g_frame) g_frame = new Gdiplus::Bitmap(delta.width, delta.height, PixelFormat32bppARGB);
                Gdiplus::Graphics g(g_frame);
                g.DrawImage(img, delta.x, delta.y, delta.width, delta.height);
                LeaveCriticalSection(&g_cs);

                if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);

                g_frameCount++;
                DWORD currentTime = GetTickCount();
                if (currentTime - g_lastTime >= 1000) {
                    g_fps = g_frameCount;
                    g_frameCount = 0;
                    g_lastTime = currentTime;
                }
            }
            if (img) delete img;
            pStream->Release();
        } else {
            GlobalFree(hMem);
        }
    }

    void Cleanup() {
        if (g_frame) {
            delete g_frame;
            g_frame = nullptr;
        }
        DeleteCriticalSection(&g_cs);
    }
}