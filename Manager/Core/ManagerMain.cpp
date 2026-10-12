#include "../../Shared/Network.h"
#include "../../Shared/Protocol.h"
#include <windows.h>
#include <gdiplus.h>
#include <thread>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

// --- CÁC BIẾN TOÀN CỤC ---
TcpClient client;
Gdiplus::Image* g_currentFrame = nullptr;
CRITICAL_SECTION g_cs;

// ==========================================
// LUỒNG CHẠY NGẦM: NHẬN ẢNH TỪ CLIENT
// ==========================================
void ReceiveLoop(HWND hwnd) {
    while (true) {
        Protocol::PacketHeader header;
        
        if (!client.ReceiveExact((char*)&header, sizeof(header))) break;

        if (header.size > 0) {
            std::vector<char> buffer(header.size);
            if (!client.ReceiveExact(buffer.data(), header.size)) break;

            if (header.type == Protocol::MSG_VIDEO_FRAME) {
                // 1. Bóc tách Tọa độ Delta từ đầu mảng byte
                Protocol::VideoDeltaHeader delta;
                memcpy(&delta, buffer.data(), sizeof(delta));
                
                // 2. Tách phần ảnh JPEG ở phía sau
                int jpegSize = buffer.size() - sizeof(delta);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, jpegSize);
                
                if (hMem) {
                    void* pData = GlobalLock(hMem);
                    memcpy(pData, buffer.data() + sizeof(delta), jpegSize);
                    GlobalUnlock(hMem);

                    IStream* pStream = nullptr;
                    if (CreateStreamOnHGlobal(hMem, TRUE, &pStream) == S_OK) {
                        Gdiplus::Image* deltaImage = Gdiplus::Image::FromStream(pStream);
                        
                        if (deltaImage && deltaImage->GetLastStatus() == Gdiplus::Ok) {
                            
                            EnterCriticalSection(&g_cs);
                            // Nếu là lần đầu tiên, cấp phát 1 tờ giấy trắng bằng kích thước thật của máy Client
                            if (!g_currentFrame) {
                                g_currentFrame = new Gdiplus::Bitmap(delta.width, delta.height, PixelFormat32bppARGB);
                            }
                            
                            // Dùng bút vẽ mảnh ảnh nhỏ đè lên tờ giấy lớn tại đúng tọa độ x, y
                            Gdiplus::Graphics g(g_currentFrame);
                            g.DrawImage(deltaImage, delta.x, delta.y, delta.width, delta.height);
                            LeaveCriticalSection(&g_cs);

                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                        
                        if (deltaImage) delete deltaImage;
                        pStream->Release(); 
                    } else {
                        GlobalFree(hMem);
                    }
                }
            }
        }
    }
}

// ==========================================
// LUỒNG CHÍNH: XỬ LÝ GIAO DIỆN WINDOWS (UI)
// ==========================================
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            if (client.Connect("127.0.0.1", 8080)) {
                std::thread(ReceiveLoop, hwnd).detach();
            } else {
                MessageBoxW(hwnd, L"Khong the ket noi toi Client!", L"Loi", MB_OK | MB_ICONERROR);
            }
            return 0;
        }

        // ==========================================
        // ÉP TỶ LỆ KHUNG HÌNH 16:9 KHI KÉO CỬA SỔ
        // ==========================================
        case WM_SIZING: {
            RECT* pRect = (RECT*)lParam;
            int dragEdge = wParam;

            // Tính độ dày của viền cửa sổ và thanh Title bar
            RECT winRect, clientRect;
            GetWindowRect(hwnd, &winRect);
            GetClientRect(hwnd, &clientRect);
            int borderW = (winRect.right - winRect.left) - clientRect.right;
            int borderH = (winRect.bottom - winRect.top) - clientRect.bottom;

            // Tính kích thước phần hiển thị bên trong
            int clientW = (pRect->right - pRect->left) - borderW;
            int clientH = (pRect->bottom - pRect->top) - borderH;

            // Cố định tỷ lệ 16:9
            if (dragEdge == WMSZ_LEFT || dragEdge == WMSZ_RIGHT || dragEdge == WMSZ_BOTTOMLEFT || dragEdge == WMSZ_TOPLEFT) {
                // Kéo chiều ngang -> Cập nhật chiều dọc
                clientH = clientW * 9 / 16;
            } else {
                // Kéo chiều dọc -> Cập nhật chiều ngang
                clientW = clientH * 16 / 9;
            }

            // Gán lại kích thước vào con trỏ của Windows
            if (dragEdge == WMSZ_LEFT || dragEdge == WMSZ_TOPLEFT || dragEdge == WMSZ_BOTTOMLEFT) {
                pRect->left = pRect->right - clientW - borderW;
            } else {
                pRect->right = pRect->left + clientW + borderW;
            }

            if (dragEdge == WMSZ_TOP || dragEdge == WMSZ_TOPLEFT || dragEdge == WMSZ_TOPRIGHT) {
                pRect->top = pRect->bottom - clientH - borderH;
            } else {
                pRect->bottom = pRect->top + clientH + borderH;
            }
            return TRUE;
        }

        // Ngăn Windows xóa nền đen mỗi khi vẽ frame mới (Chống nhấp nháy màn hình)
        case WM_ERASEBKGND:
            return 1; 

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            EnterCriticalSection(&g_cs);
            if (g_currentFrame) {
                Gdiplus::Graphics graphics(hdc);
                
                // Khử răng cưa, loại bỏ viền nhòe góc
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
                graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
                
                RECT rect;
                GetClientRect(hwnd, &rect);
                
                graphics.DrawImage(g_currentFrame, 0, 0, rect.right, rect.bottom);
            }
            LeaveCriticalSection(&g_cs);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            client.Close();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// ==========================================
// HÀM KHỞI CHẠY (ENTRY POINT)
// ==========================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 1. Cấp quyền chụp và vẽ theo chuẩn Pixel thật (Bỏ qua cấu hình Scale màn hình của Windows)
    SetProcessDPIAware();

    Network::Initialize();
    InitializeCriticalSection(&g_cs);

    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    const wchar_t CLASS_NAME[] = L"ManagerWindowClass";
    WNDCLASSW wc = { };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    // Kích thước khởi tạo mặc định theo chuẩn 16:9
    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"LanRemote - Manager (Dev 3)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) return 0;
    ShowWindow(hwnd, nCmdShow);

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_currentFrame) delete g_currentFrame;
    Gdiplus::GdiplusShutdown(gdiplusToken);
    DeleteCriticalSection(&g_cs);
    Network::Cleanup();

    return 0;
}