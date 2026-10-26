#include "RemoteWindow.h"
#include "InputHandler.h"
#include "TaskManagerWindow.h"
#include "FileExplorerWindow.h"
#include "../Media/AudioPlayer.h"
#include <turbojpeg.h>
#include <string>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>

#define TOOLBAR_HEIGHT 35

namespace {
    HWND g_hwnd = NULL;
    TcpClient* g_client = nullptr;
    
    // Bộ đệm màn hình 32bpp BGRA (Direct Framebuffer)
    std::vector<uint32_t> g_framePixels;
    int g_frameWidth = 0;
    int g_frameHeight = 0;
    BITMAPINFO g_bmi = {};

    tjhandle g_tjDecompressor = nullptr;
    CRITICAL_SECTION g_cs;

    int g_fps = 0;
    int g_frameCount = 0;
    DWORD g_lastTime = 0;

    int GetFrameWidth() { return g_frameWidth; }
    int GetFrameHeight() { return g_frameHeight; }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT r;
                GetClientRect(hwnd, &r);

                // 1. Vẽ thanh Toolbar ở phía trên
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

                // 2. Vẽ màn hình máy Client bằng StretchDIBits (Tăng tốc phần cứng GDI, <1ms)
                if (ps.rcPaint.bottom > TOOLBAR_HEIGHT) {
                    EnterCriticalSection(&g_cs);
                    if (!g_framePixels.empty() && g_frameWidth > 0 && g_frameHeight > 0) {
                        SetStretchBltMode(hdc, COLORONCOLOR);
                        StretchDIBits(
                            hdc,
                            0, TOOLBAR_HEIGHT, r.right, r.bottom - TOOLBAR_HEIGHT,
                            0, 0, g_frameWidth, g_frameHeight,
                            g_framePixels.data(), &g_bmi, DIB_RGB_COLORS, SRCCOPY
                        );
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

                // Khóa phím Windows: Không gửi lệnh nhấn Win sang Client để chống kẹt phím
                if (wp == VK_LWIN || wp == VK_RWIN) {
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
                if (wp == VK_LWIN || wp == VK_RWIN) {
                    if (InputHandler::IsControlling()) {
                        InputHandler::SendKey(wp, 0);
                    }
                    return 0;
                }

                if (InputHandler::IsControlling()) {
                    InputHandler::SendKey(wp, 0);
                    return 0;
                }
                break;
            }
            case WM_KILLFOCUS: {
                InputHandler::ReleaseModifiers();
                return 0;
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
        g_tjDecompressor = tjInitDecompress();
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
        if (jpegSize <= 0 || !jpegData || !g_tjDecompressor) return;

        int reqW = delta.x + delta.width;
        int reqH = delta.y + delta.height;
        if (reqW <= 0 || reqH <= 0 || delta.width <= 0 || delta.height <= 0) return;

        EnterCriticalSection(&g_cs);

        // Mở rộng bộ đệm nếu khung hình mới lớn hơn
        if (g_frameWidth < reqW || g_frameHeight < reqH) {
            int newW = (std::max)(g_frameWidth, reqW);
            int newH = (std::max)(g_frameHeight, reqH);
            std::vector<uint32_t> newPixels(newW * newH, 0);
            if (g_frameWidth > 0 && g_frameHeight > 0) {
                for (int y = 0; y < g_frameHeight; ++y) {
                    std::memcpy(&newPixels[y * newW], &g_framePixels[y * g_frameWidth], g_frameWidth * sizeof(uint32_t));
                }
            }
            g_framePixels = std::move(newPixels);
            g_frameWidth = newW;
            g_frameHeight = newH;

            std::memset(&g_bmi, 0, sizeof(g_bmi));
            g_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            g_bmi.bmiHeader.biWidth = g_frameWidth;
            g_bmi.bmiHeader.biHeight = -g_frameHeight; // Top-down
            g_bmi.bmiHeader.biPlanes = 1;
            g_bmi.bmiHeader.biBitCount = 32;
            g_bmi.bmiHeader.biCompression = BI_RGB;
        }

        // Giải nén trực tiếp vào đúng vị trí delta trên bộ đệm màn hình (Zero intermediate allocation, ~1-2ms)
        unsigned char* targetPtr = (unsigned char*)(g_framePixels.data() + (delta.y * g_frameWidth) + delta.x);
        int pitch = g_frameWidth * 4;

        int decompRes = tjDecompress2(
            g_tjDecompressor,
            (const unsigned char*)jpegData,
            (unsigned long)jpegSize,
            targetPtr,
            delta.width,
            pitch,
            delta.height,
            TJPF_BGRX,
            TJFLAG_FASTDCT
        );

        LeaveCriticalSection(&g_cs);

        if (decompRes == 0) {
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
    }

    void Cleanup() {
        EnterCriticalSection(&g_cs);
        g_framePixels.clear();
        g_frameWidth = 0;
        g_frameHeight = 0;
        if (g_tjDecompressor) {
            tjDestroy(g_tjDecompressor);
            g_tjDecompressor = nullptr;
        }
        LeaveCriticalSection(&g_cs);
        DeleteCriticalSection(&g_cs);
    }
}
