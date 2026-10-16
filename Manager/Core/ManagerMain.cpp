#include "../../Shared/Network.h"
#include "../../Shared/Protocol.h"
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <thread>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

TcpClient g_client;
Gdiplus::Image* g_frame = nullptr;
CRITICAL_SECTION g_cs;
HWND g_hwnd = NULL;

// BIẾN TRẠNG THÁI: true = Đang điều khiển, false = Chỉ xem
bool g_isControlling = true; 

// LUỒNG NGẦM: NHẬN VÀ GIẢI MÃ ẢNH
void ReceiveLoop() {
    Protocol::PacketHeader hdr;
    while (g_client.ReceiveExact((char*)&hdr, sizeof(hdr))) {
        if (hdr.size <= 0) continue;
        
        std::vector<char> buf(hdr.size);
        if (!g_client.ReceiveExact(buf.data(), hdr.size)) break;
        
        if (hdr.type == Protocol::MSG_VIDEO_FRAME) {
            Protocol::VideoDeltaHeader delta;
            memcpy(&delta, buf.data(), sizeof(delta));
            
            int jpgSize = buf.size() - sizeof(delta);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, jpgSize);
            if (!hMem) continue;
            memcpy(GlobalLock(hMem), buf.data() + sizeof(delta), jpgSize);
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
                    
                    InvalidateRect(g_hwnd, NULL, FALSE);
                }
                if (img) delete img;
                pStream->Release();
            } else GlobalFree(hMem);
        }
    }
}

// GỬI CHUỘT (CÓ KIỂM TRA TRẠNG THÁI)
void SendMouse(uint8_t action, uint8_t btn, LPARAM lp, int16_t wheel = 0) {
    if (!g_frame || !g_isControlling) return; // Chặn lại nếu đang "Chỉ xem"
    RECT r; GetClientRect(g_hwnd, &r);
    
    Protocol::MousePayload mp;
    mp.actionType = action;
    mp.buttonType = btn;
    mp.x = (int16_t)(GET_X_LPARAM(lp) * g_frame->GetWidth() / r.right);
    mp.y = (int16_t)(GET_Y_LPARAM(lp) * g_frame->GetHeight() / r.bottom);
    mp.wheelDelta = wheel;
    
    g_client.SendPacket(Protocol::MSG_MOUSE_EVENT, (char*)&mp, sizeof(mp));
}

// GỬI BÀN PHÍM (CÓ KIỂM TRA TRẠNG THÁI)
void SendKey(WPARAM wp, uint8_t isDown) {
    if (!g_isControlling) return; // Chặn lại nếu đang "Chỉ xem"
    Protocol::KeyboardPayload kp;
    kp.vkCode = (uint32_t)wp;
    kp.isDown = isDown;
    g_client.SendPacket(Protocol::MSG_KEY_EVENT, (char*)&kp, sizeof(kp));
}

// LUỒNG CHÍNH: BẮT SỰ KIỆN VÀ VẼ
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            EnterCriticalSection(&g_cs);
            if (g_frame) {
                Gdiplus::Graphics g(hdc);
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
                RECT r; GetClientRect(hwnd, &r);
                g.DrawImage(g_frame, 0, 0, r.right, r.bottom);
            }
            LeaveCriticalSection(&g_cs);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;

        // --- BẮT BÀN PHÍM VÀ KIỂM TRA CTRL + ALT ---
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: { // SYSKEYDOWN dùng để bắt phím Alt
            // Kiểm tra xem cả Ctrl và Alt có đang bị đè không
            if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000)) {
                // Bit thứ 30 = 0 nghĩa là phím vừa được bấm xuống (không phải giữ đè auto-repeat)
                if ((lp & (1 << 30)) == 0) {
                    g_isControlling = !g_isControlling; // Đảo trạng thái
                    
                    // Cập nhật tiêu đề cửa sổ để người dùng biết
                    SetWindowTextW(hwnd, g_isControlling ? 
                        L"LanRemote - [ĐANG ĐIỀU KHIỂN] (Nhấn Ctrl+Alt để Thoát)" : 
                        L"LanRemote - [CHỈ XEM] (Nhấn Ctrl+Alt để Điều khiển)");
                }
                return 0; // Chặn không gửi tổ hợp phím này đi
            }
            
            if (g_isControlling) { SendKey(wp, 1); return 0; }
            break; // Nếu đang "Chỉ xem", nhả ra để Windows tự xử lý (ví dụ Alt+F4 tắt app)
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            if (g_isControlling) { SendKey(wp, 0); return 0; }
            break;
        }

        // --- BẮT SỰ KIỆN CHUỘT ---
        case WM_MOUSEMOVE:   SendMouse(Protocol::MOUSE_ACTION_MOVE, Protocol::BUTTON_NONE, lp); return 0;
        case WM_LBUTTONDOWN: SendMouse(Protocol::MOUSE_ACTION_PRESS, Protocol::BUTTON_LEFT, lp); return 0;
        case WM_LBUTTONUP:   SendMouse(Protocol::MOUSE_ACTION_RELEASE, Protocol::BUTTON_LEFT, lp); return 0;
        case WM_RBUTTONDOWN: SendMouse(Protocol::MOUSE_ACTION_PRESS, Protocol::BUTTON_RIGHT, lp); return 0;
        case WM_RBUTTONUP:   SendMouse(Protocol::MOUSE_ACTION_RELEASE, Protocol::BUTTON_RIGHT, lp); return 0;
        case WM_MOUSEWHEEL:  SendMouse(Protocol::MOUSE_ACTION_WHEEL, Protocol::BUTTON_NONE, lp, GET_WHEEL_DELTA_WPARAM(wp)); return 0;

        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ENTRY POINT
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    SetProcessDPIAware(); 
    Network::Initialize();
    InitializeCriticalSection(&g_cs);
    ULONG_PTR gdiToken; 
    Gdiplus::GdiplusStartupInput gdiInput;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, NULL);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = L"MgrWindow";
    RegisterClassW(&wc);
    
    // Tiêu đề mặc định khi mới bật
    g_hwnd = CreateWindowExW(0, L"MgrWindow", L"LanRemote - [ĐANG ĐIỀU KHIỂN] (Nhấn Ctrl+Alt để Thoát)", 
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, 0, 0, hInst, 0);

    if (g_client.Connect("127.0.0.1", 8080)) {
        std::thread(ReceiveLoop).detach();
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { 
        TranslateMessage(&msg); 
        DispatchMessage(&msg); 
    }

    g_client.Close();
    if (g_frame) delete g_frame;
    Gdiplus::GdiplusShutdown(gdiToken);
    DeleteCriticalSection(&g_cs);
    Network::Cleanup();
    return 0;
}