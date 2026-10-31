#include "RemoteWindow.h"
#include "D2D1Renderer.h"
#include "InputHandler.h"
#include "TaskManagerWindow.h"
#include "FileExplorerWindow.h"
#include "ChatWindow.h"
#include "../Media/AudioPlayer.h"
#include <gdiplus.h>
#include <turbojpeg.h>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <atomic>

namespace {
    HWND g_hwnd = NULL;
    HWND g_hVideoWnd = NULL;
    TcpClient* g_client = nullptr;

    std::vector<uint32_t> g_framePixels;
    int g_frameWidth = 0;
    int g_frameHeight = 0;
    BITMAPINFO g_bmi = {};

    tjhandle g_tjDecompressor = nullptr;
    CRITICAL_SECTION g_cs;

    std::atomic<int> g_streamFrameCount(0);
    std::atomic<int> g_renderFrameCount(0);
    std::atomic<int> g_streamFps(0);
    std::atomic<int> g_renderFps(60);
    std::atomic<uint64_t> g_lastFrameTimeMs(0);

    int GetFrameWidth() { return g_frameWidth; }
    int GetFrameHeight() { return g_frameHeight; }

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

    enum ToolbarButtonId {
        BTN_NONE = 0,
        BTN_MODE,
        BTN_MUTE,
        BTN_VOL_DOWN,
        BTN_VOL_UP,
        BTN_TASK_MGR,
        BTN_FILE_EXP,
        BTN_CHAT
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

        bool isCtrl = InputHandler::IsControlling();
        std::wstring modeText = isCtrl ? L"Đang điều khiển" : L"Chỉ xem";
        std::wstring modeHint = L"Ctrl+Alt";
        float btnModeW = (float)std::round((isCtrl ? 144.0f : 100.0f) * scale);
        buttons.push_back({ BTN_MODE, Gdiplus::RectF(curX, btnY, btnModeW, btnHeight), modeText, modeHint, true });
        curX += btnModeW + gap;

        bool isMuted = AudioPlayer::IsMuted();
        std::wstring muteText = isMuted ? L"Đã tắt tiếng" : L"Âm thanh";
        std::wstring muteHint = L"Ctrl+M";
        float btnMuteW = (float)std::round((isMuted ? 122.0f : 106.0f) * scale);
        buttons.push_back({ BTN_MUTE, Gdiplus::RectF(curX, btnY, btnMuteW, btnHeight), muteText, muteHint, isMuted });
        curX += btnMuteW + gap;

        float btnVolMiniW = (float)std::round(26.0f * scale);
        buttons.push_back({ BTN_VOL_DOWN, Gdiplus::RectF(curX, btnY, btnVolMiniW, btnHeight), L"−", L"Ctrl+↓", false });
        curX += btnVolMiniW + 2.0f * scale;

        int volPercent = (int)std::round(AudioPlayer::GetVolume() * 100.0f);
        float lblVolW = (float)std::round(64.0f * scale);
        buttons.push_back({ BTN_NONE, Gdiplus::RectF(curX, btnY, lblVolW, btnHeight), std::to_wstring(volPercent) + L"%", L"", false });
        curX += lblVolW + 2.0f * scale;

        buttons.push_back({ BTN_VOL_UP, Gdiplus::RectF(curX, btnY, btnVolMiniW, btnHeight), L"+", L"Ctrl+↑", false });
        curX += btnVolMiniW + gap * 2.0f;

        float btnTaskW = (float)std::round(132.0f * scale);
        buttons.push_back({ BTN_TASK_MGR, Gdiplus::RectF(curX, btnY, btnTaskW, btnHeight), L"Task Manager", L"Ctrl+Tab", false });
        curX += btnTaskW + gap;

        float btnFileW = (float)std::round(124.0f * scale);
        buttons.push_back({ BTN_FILE_EXP, Gdiplus::RectF(curX, btnY, btnFileW, btnHeight), L"File Explorer", L"Ctrl+E", false });
        curX += btnFileW + gap;

        int unread = ChatWindow::GetUnreadCount();
        std::wstring chatText = (unread > 0) ? (L"Trò chuyện (" + std::to_wstring(unread) + L")") : L"Trò chuyện";
        std::wstring chatHint = L"Ctrl+T";
        float btnChatW = (float)std::round((unread > 0 ? 128.0f : 106.0f) * scale);
        bool isChatActive = (unread > 0 || ChatWindow::IsOpen());
        buttons.push_back({ BTN_CHAT, Gdiplus::RectF(curX, btnY, btnChatW, btnHeight), chatText, chatHint, isChatActive });
        curX += btnChatW + gap;

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

        Gdiplus::Bitmap memBmp(width, tbHeight, PixelFormat32bppPARGB);
        Gdiplus::Graphics g(&memBmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 24, 25, 30));
        g.FillRectangle(&bgBrush, 0, 0, width, tbHeight);

        Gdiplus::Pen borderPen(Gdiplus::Color(255, 45, 47, 56), 1.0f);
        g.DrawLine(&borderPen, 0.0f, (float)(tbHeight - 1), (float)width, (float)(tbHeight - 1));

        auto buttons = BuildToolbarButtons(hwnd, width, tbHeight);

        Gdiplus::FontFamily fontFamily(L"Segoe UI");
        Gdiplus::Font fontBtn(&fontFamily, 12.0f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font fontVol(&fontFamily, 12.0f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

        Gdiplus::StringFormat sfCenter;
        sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
        sfCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        sfCenter.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoClip);

        Gdiplus::GraphicsPath path;
        float radius = 5.0f * scale;

        for (const auto& btn : buttons) {
            if (btn.rect.GetRight() > (float)(width - 160 * scale)) continue;

            AddRoundRectPath(path, btn.rect, radius);

            if (btn.id == BTN_NONE) {
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
            } else if (btn.id == BTN_CHAT && btn.isSpecial) {
                fillColor = isPressed ? Gdiplus::Color(255, 30, 58, 138) : (isHovered ? Gdiplus::Color(255, 37, 99, 235) : Gdiplus::Color(255, 29, 78, 216));
                borderColor = Gdiplus::Color(255, 96, 165, 250);
                textColor = Gdiplus::Color(255, 255, 255, 255);
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
            } else if (btn.id == BTN_MUTE) {
                bool isMuted = AudioPlayer::IsMuted();
                float iconW = 12.0f * scale;
                float iconX = btn.rect.X + 10.0f * scale;
                float iconY = btn.rect.Y + (btn.rect.Height - 10.0f * scale) / 2.0f;
                Gdiplus::SolidBrush iconBrush(textColor);

                g.FillRectangle(&iconBrush, iconX, iconY + 3.0f * scale, 3.0f * scale, 4.0f * scale);

                Gdiplus::PointF conePts[3] = {
                    Gdiplus::PointF(iconX + 3.0f * scale, iconY + 3.0f * scale),
                    Gdiplus::PointF(iconX + 7.0f * scale, iconY),
                    Gdiplus::PointF(iconX + 7.0f * scale, iconY + 10.0f * scale)
                };
                g.FillPolygon(&iconBrush, conePts, 3);
                if (isMuted) {
                    Gdiplus::Pen slashPen(Gdiplus::Color(255, 239, 68, 68), 1.5f * scale);
                    g.DrawLine(&slashPen, iconX, iconY, iconX + 11.0f * scale, iconY + 10.0f * scale);
                } else {
                    Gdiplus::Pen arcPen(textColor, 1.2f * scale);
                    g.DrawArc(&arcPen, iconX + 5.0f * scale, iconY + 2.0f * scale, 6.0f * scale, 6.0f * scale, -45.0f, 90.0f);
                }
                Gdiplus::RectF textRect = btn.rect;
                textRect.X += (iconW + 4.0f * scale);
                textRect.Width -= (iconW + 4.0f * scale);
                g.DrawString(btn.text.c_str(), -1, &fontBtn, textRect, &sfCenter, &textBrush);
            } else if (btn.id == BTN_TASK_MGR) {
                float iconW = 12.0f * scale;
                float iconX = btn.rect.X + 10.0f * scale;
                float iconY = btn.rect.Y + (btn.rect.Height - 11.0f * scale) / 2.0f;
                Gdiplus::SolidBrush barBrush(textColor);
                g.FillRectangle(&barBrush, iconX, iconY + 5.0f * scale, 2.5f * scale, 6.0f * scale);
                g.FillRectangle(&barBrush, iconX + 4.0f * scale, iconY, 2.5f * scale, 11.0f * scale);
                g.FillRectangle(&barBrush, iconX + 8.0f * scale, iconY + 3.0f * scale, 2.5f * scale, 8.0f * scale);
                Gdiplus::RectF textRect = btn.rect;
                textRect.X += (iconW + 4.0f * scale);
                textRect.Width -= (iconW + 4.0f * scale);
                g.DrawString(btn.text.c_str(), -1, &fontBtn, textRect, &sfCenter, &textBrush);
            } else if (btn.id == BTN_FILE_EXP) {
                float iconW = 13.0f * scale;
                float iconX = btn.rect.X + 10.0f * scale;
                float iconY = btn.rect.Y + (btn.rect.Height - 10.0f * scale) / 2.0f;
                Gdiplus::SolidBrush folderBrush(textColor);
                g.FillRectangle(&folderBrush, iconX, iconY, 5.0f * scale, 3.0f * scale);
                g.FillRectangle(&folderBrush, iconX, iconY + 2.0f * scale, 12.0f * scale, 8.0f * scale);
                Gdiplus::RectF textRect = btn.rect;
                textRect.X += (iconW + 4.0f * scale);
                textRect.Width -= (iconW + 4.0f * scale);
                g.DrawString(btn.text.c_str(), -1, &fontBtn, textRect, &sfCenter, &textBrush);
            } else if (btn.id == BTN_CHAT) {
                float iconW = 12.0f * scale;
                float iconX = btn.rect.X + 10.0f * scale;
                float iconY = btn.rect.Y + (btn.rect.Height - 10.0f * scale) / 2.0f;
                Gdiplus::SolidBrush chatBrush(textColor);
                Gdiplus::RectF bubbleRect(iconX, iconY, 11.0f * scale, 7.5f * scale);
                Gdiplus::GraphicsPath bubblePath;
                AddRoundRectPath(bubblePath, bubbleRect, 2.0f * scale);
                g.FillPath(&chatBrush, &bubblePath);
                Gdiplus::PointF tailPts[3] = {
                    Gdiplus::PointF(iconX + 2.0f * scale, iconY + 6.0f * scale),
                    Gdiplus::PointF(iconX - 0.5f * scale, iconY + 10.0f * scale),
                    Gdiplus::PointF(iconX + 5.5f * scale, iconY + 6.0f * scale)
                };
                g.FillPolygon(&chatBrush, tailPts, 3);

                Gdiplus::RectF textRect = btn.rect;
                textRect.X += (iconW + 4.0f * scale);
                textRect.Width -= (iconW + 4.0f * scale);
                g.DrawString(btn.text.c_str(), -1, &fontBtn, textRect, &sfCenter, &textBrush);
            } else {
                g.DrawString(btn.text.c_str(), -1, &fontBtn, btn.rect, &sfCenter, &textBrush);
            }
        }

        float badgeW = 140.0f * scale;
        float badgeH = 28.0f * scale;
        float badgeX = (float)width - badgeW - (12.0f * scale);
        float badgeY = (float)(tbHeight - badgeH) / 2.0f;
        Gdiplus::RectF badgeRect(badgeX, badgeY, badgeW, badgeH);

        AddRoundRectPath(path, badgeRect, radius);
        Gdiplus::SolidBrush badgeBg(Gdiplus::Color(255, 18, 19, 23));
        g.FillPath(&badgeBg, &path);
        Gdiplus::Pen badgeBorder(Gdiplus::Color(255, 42, 44, 54), 1.0f);
        g.DrawPath(&badgeBorder, &path);

        Gdiplus::Color dotColor;
        std::wstring fpsStr;

        int streamFps = g_streamFps.load();
        int renderFps = g_renderFps.load();
        uint64_t nowMs = GetTickCount64();
        uint64_t lastFrameMs = g_lastFrameTimeMs.load(std::memory_order_relaxed);
        uint64_t msSinceLastFrame = (lastFrameMs > 0 && nowMs >= lastFrameMs) ? (nowMs - lastFrameMs) : 0;

        // Nhận diện trạng thái màn hình tĩnh thông minh:
        // Nếu trong 400ms qua không có frame mới (hoặc streamFps == 0), coi là màn hình tĩnh (Eco Mode).
        // Tránh hiện tượng nhấp nháy đèn cam hoặc báo ảo "2-3 FPS" khi người dùng vừa dừng tay.
        bool isStaticScreen = (streamFps == 0) || (streamFps <= 6 && msSinceLastFrame >= 350) || (msSinceLastFrame >= 500);

        if (isStaticScreen) {
            dotColor = Gdiplus::Color(255, 56, 189, 248); // Cyan: Màn hình tĩnh / đồng bộ tuyệt đối (Eco)
            fpsStr = L"60 FPS (Tĩnh)";
        } else {
            int dispFps = (renderFps > 0) ? renderFps : streamFps;
            if (streamFps >= 25) {
                dotColor = Gdiplus::Color(255, 34, 197, 94); // Xanh lá: Mượt mà 60 FPS
            } else if (streamFps >= 12) {
                dotColor = Gdiplus::Color(255, 245, 158, 11); // Vàng cam: Mạng trung bình
            } else {
                dotColor = Gdiplus::Color(255, 239, 68, 68); // Đỏ: Nghẽn mạng khi đang chuyển động
            }
            fpsStr = std::to_wstring(dispFps) + L" FPS (Net " + std::to_wstring(streamFps) + L")";
        }

        Gdiplus::SolidBrush dotBrush(dotColor);
        float dotSize = 7.0f * scale;
        float dotX = badgeX + 10.0f * scale;
        float dotY = badgeY + (badgeH - dotSize) / 2.0f;
        g.FillEllipse(&dotBrush, dotX, dotY, dotSize, dotSize);

        Gdiplus::RectF fpsTextRect(dotX + dotSize + 6.0f * scale, badgeY, badgeW - dotSize - 16.0f * scale, badgeH);
        Gdiplus::StringFormat sfLeft;
        sfLeft.SetAlignment(Gdiplus::StringAlignmentNear);
        sfLeft.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::SolidBrush fpsTextBrush(Gdiplus::Color(255, 215, 220, 230));
        g.DrawString(fpsStr.c_str(), -1, &fontBtn, fpsTextRect, &sfLeft, &fpsTextBrush);

        Gdiplus::Graphics targetG(hdc);
        targetG.DrawImage(&memBmp, 0, 0, width, tbHeight);
    }

    static void DispatchVideoMouse(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, uint8_t action, uint8_t btn) {
        if (!InputHandler::IsControlling()) return;

        int frameW = GetFrameWidth();
        int frameH = GetFrameHeight();
        if (frameW <= 0 || frameH <= 0) return;

        int16_t wheel = (msg == WM_MOUSEWHEEL) ? (int16_t)GET_WHEEL_DELTA_WPARAM(wp) : 0;

        if (D2D1Renderer::IsInitialized() && g_hVideoWnd) {
            float vx = 0, vy = 0, vw = 0, vh = 0;
            D2D1Renderer::GetViewport(vx, vy, vw, vh);
            if (vw <= 0.0f || vh <= 0.0f) return;

            POINT pt;
            if (msg == WM_MOUSEWHEEL) {
                pt.x = (short)LOWORD(lp);
                pt.y = (short)HIWORD(lp);
                ScreenToClient(g_hVideoWnd, &pt);
            } else if (hwnd == g_hVideoWnd) {
                pt.x = (short)LOWORD(lp);
                pt.y = (short)HIWORD(lp);
            } else {
                pt.x = (short)LOWORD(lp);
                pt.y = (short)HIWORD(lp);
                ClientToScreen(hwnd, &pt);
                ScreenToClient(g_hVideoWnd, &pt);
            }

            // Kẹp tọa độ chuột trong phạm vi viewport hiển thị thực tế (Letterbox viewport)
            float cx = (float)pt.x;
            if (cx < vx) cx = vx;
            if (cx > vx + vw) cx = vx + vw;

            float cy = (float)pt.y;
            if (cy < vy) cy = vy;
            if (cy > vy + vh) cy = vy + vh;

            // Ánh xạ chính xác tuyệt đối sang tọa độ pixel của màn hình Client
            int16_t imgX = (int16_t)((cx - vx) * (float)frameW / vw);
            int16_t imgY = (int16_t)((cy - vy) * (float)frameH / vh);

            if (imgX < 0) imgX = 0;
            if (imgX >= frameW) imgX = (int16_t)(frameW - 1);
            if (imgY < 0) imgY = 0;
            if (imgY >= frameH) imgY = (int16_t)(frameH - 1);

            InputHandler::SendMouseDirect(action, btn, imgX, imgY, wheel);
        } else {
            InputHandler::SendMouse(action, btn, lp, hwnd, frameW, frameH, (hwnd == g_hVideoWnd) ? 0 : GetToolbarHeight(hwnd), wheel);
        }
    }

    LRESULT CALLBACK VideoWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_SIZE: {
                int w = LOWORD(lp);
                int h = HIWORD(lp);
                if (D2D1Renderer::IsInitialized()) {
                    D2D1Renderer::Resize(w, h);
                }
                return 0;
            }

            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                if (D2D1Renderer::IsInitialized()) {
                    D2D1Renderer::Render();
                } else {
                    EnterCriticalSection(&g_cs);
                    if (!g_framePixels.empty() && g_frameWidth > 0 && g_frameHeight > 0) {
                        RECT r;
                        GetClientRect(hwnd, &r);
                        float scaleX = (float)r.right / (float)g_frameWidth;
                        float scaleY = (float)r.bottom / (float)g_frameHeight;
                        float scale = (std::min)(scaleX, scaleY);
                        int drawW = (int)(g_frameWidth * scale);
                        int drawH = (int)(g_frameHeight * scale);
                        int offX = (r.right - drawW) / 2;
                        int offY = (r.bottom - drawH) / 2;

                        HBRUSH bgBrush = CreateSolidBrush(RGB(18, 19, 23));
                        FillRect(hdc, &r, bgBrush);
                        DeleteObject(bgBrush);

                        SetStretchBltMode(hdc, HALFTONE);
                        StretchDIBits(
                            hdc,
                            offX, offY, drawW, drawH,
                            0, 0, g_frameWidth, g_frameHeight,
                            g_framePixels.data(), &g_bmi, DIB_RGB_COLORS, SRCCOPY
                        );
                    }
                    LeaveCriticalSection(&g_cs);
                }
                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_ERASEBKGND:
                return 1;

            case WM_SETCURSOR: {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
                return TRUE;
            }

            case WM_MOUSEMOVE: {
                DispatchVideoMouse(hwnd, msg, wp, lp, Protocol::MOUSE_ACTION_MOVE, Protocol::BUTTON_NONE);
                return 0;
            }

            case WM_LBUTTONDOWN: {
                SetFocus(hwnd);
                DispatchVideoMouse(hwnd, msg, wp, lp, Protocol::MOUSE_ACTION_PRESS, Protocol::BUTTON_LEFT);
                return 0;
            }

            case WM_LBUTTONUP: {
                DispatchVideoMouse(hwnd, msg, wp, lp, Protocol::MOUSE_ACTION_RELEASE, Protocol::BUTTON_LEFT);
                return 0;
            }

            case WM_RBUTTONDOWN: {
                DispatchVideoMouse(hwnd, msg, wp, lp, Protocol::MOUSE_ACTION_PRESS, Protocol::BUTTON_RIGHT);
                return 0;
            }

            case WM_RBUTTONUP: {
                DispatchVideoMouse(hwnd, msg, wp, lp, Protocol::MOUSE_ACTION_RELEASE, Protocol::BUTTON_RIGHT);
                return 0;
            }

            case WM_MOUSEWHEEL: {
                DispatchVideoMouse(hwnd, msg, wp, lp, Protocol::MOUSE_ACTION_WHEEL, Protocol::BUTTON_NONE);
                return 0;
            }

            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP: {
                if (g_hwnd) {
                    return SendMessageW(g_hwnd, msg, wp, lp);
                }
                break;
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        int tbHeight = GetToolbarHeight(hwnd);

        switch (msg) {
            case WM_TIMER: {
                if (wp == 1) {
                    g_streamFps.store(g_streamFrameCount.exchange(0));
                    g_renderFps.store(g_renderFrameCount.exchange(0));
                    RECT r;
                    GetClientRect(hwnd, &r);
                    RECT tbRect = { 0, 0, r.right, tbHeight };
                    InvalidateRect(hwnd, &tbRect, FALSE);
                    return 0;
                }
                break;
            }

            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT r;
                GetClientRect(hwnd, &r);
                RenderToolbar(hwnd, hdc, r, tbHeight);
                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_SIZE: {
                int w = LOWORD(lp);
                int h = HIWORD(lp);
                int vH = h - tbHeight;
                if (vH < 0) vH = 0;
                if (g_hVideoWnd) {
                    MoveWindow(g_hVideoWnd, 0, tbHeight, w, vH, TRUE);
                    D2D1Renderer::Resize(w, vH);
                }
                RECT tbRect = { 0, 0, w, tbHeight };
                InvalidateRect(hwnd, &tbRect, FALSE);
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
                int tbH = GetToolbarHeight(hwnd);
                RECT r;
                GetClientRect(hwnd, &r);
                int vH = (r.bottom - r.top) - tbH;
                if (vH < 0) vH = 0;
                if (g_hVideoWnd) {
                    MoveWindow(g_hVideoWnd, 0, tbH, r.right, vH, TRUE);
                    D2D1Renderer::Resize(r.right, vH);
                }
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

                DispatchVideoMouse(hwnd, msg, wp, lp, Protocol::MOUSE_ACTION_MOVE, Protocol::BUTTON_NONE);
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

                DispatchVideoMouse(hwnd, msg, wp, lp, Protocol::MOUSE_ACTION_PRESS, Protocol::BUTTON_LEFT);
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
                            case BTN_CHAT:
                                ChatWindow::Toggle(hwnd, g_client);
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

                DispatchVideoMouse(hwnd, msg, wp, lp, Protocol::MOUSE_ACTION_RELEASE, Protocol::BUTTON_LEFT);
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

                if (wp == 'T' && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                    ChatWindow::Toggle(hwnd, g_client);
                    RECT r; GetClientRect(hwnd, &r);
                    RECT toolbarRect = {0, 0, r.right, tbHeight};
                    InvalidateRect(hwnd, &toolbarRect, FALSE);
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

            case WM_RBUTTONDOWN: DispatchVideoMouse(hwnd, msg, wp, lp, Protocol::MOUSE_ACTION_PRESS, Protocol::BUTTON_RIGHT); return 0;

            case WM_RBUTTONUP:   DispatchVideoMouse(hwnd, msg, wp, lp, Protocol::MOUSE_ACTION_RELEASE, Protocol::BUTTON_RIGHT); return 0;

            case WM_MOUSEWHEEL:  DispatchVideoMouse(hwnd, msg, wp, lp, Protocol::MOUSE_ACTION_WHEEL, Protocol::BUTTON_NONE); return 0;

            case WM_DESTROY: {
                KillTimer(hwnd, 1);
                PostQuitMessage(0);
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

namespace RemoteWindow {
    HWND Create(HINSTANCE hInst, TcpClient* client) {
        g_client = client;
        InitializeCriticalSection(&g_cs);
        g_tjDecompressor = tjInitDecompress();

        WNDCLASSW wc = {};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"MgrWindow";
        RegisterClassW(&wc);

        WNDCLASSW vc = {};
        vc.lpfnWndProc = VideoWndProc;
        vc.hInstance = hInst;
        vc.hCursor = LoadCursor(NULL, IDC_ARROW);
        vc.lpszClassName = L"MgrVideoWnd";
        RegisterClassW(&vc);

        g_hwnd = CreateWindowExW(0, L"MgrWindow", L"LanRemote",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_MAXIMIZE,
            CW_USEDEFAULT, CW_USEDEFAULT, 1920, 1080, 0, 0, hInst, 0);

        if (g_hwnd) {
            int tbHeight = GetToolbarHeight(g_hwnd);
            RECT r = {};
            GetClientRect(g_hwnd, &r);
            int vH = r.bottom - tbHeight;
            if (vH < 0) vH = 0;

            g_hVideoWnd = CreateWindowExW(
                0, L"MgrVideoWnd", L"",
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                0, tbHeight, r.right, vH,
                g_hwnd, NULL, hInst, NULL
            );

            if (g_hVideoWnd) {
                D2D1Renderer::Initialize(g_hVideoWnd);
            }

            SetTimer(g_hwnd, 1, 1000, NULL);
            g_lastFrameTimeMs.store(GetTickCount64(), std::memory_order_relaxed);
        }
        return g_hwnd;
    }

    void UpdateFrame(const Protocol::VideoDeltaHeader& delta, const char* jpegData, int jpegSize) {
        if (jpegSize <= 0 || !jpegData || !g_tjDecompressor) return;

        int reqW = delta.x + delta.width;
        int reqH = delta.y + delta.height;
        if (reqW <= 0 || reqH <= 0 || delta.width <= 0 || delta.height <= 0) return;

        EnterCriticalSection(&g_cs);

        bool needRealloc = (g_frameWidth < reqW || g_frameHeight < reqH);
        if (delta.x == 0 && delta.y == 0 && (delta.width != g_frameWidth || delta.height != g_frameHeight)) {
            needRealloc = true;
        }

        if (needRealloc) {
            int newW = (delta.x == 0 && delta.y == 0) ? delta.width : (std::max)(g_frameWidth, reqW);
            int newH = (delta.x == 0 && delta.y == 0) ? delta.height : (std::max)(g_frameHeight, reqH);
            std::vector<uint32_t> newPixels(newW * newH, 0);
            if (g_frameWidth > 0 && g_frameHeight > 0 && (delta.x != 0 || delta.y != 0)) {
                int copyW = (std::min)(g_frameWidth, newW);
                int copyH = (std::min)(g_frameHeight, newH);
                for (int y = 0; y < copyH; ++y) {
                    std::memcpy(&newPixels[y * newW], &g_framePixels[y * g_frameWidth], copyW * sizeof(uint32_t));
                }
            }
            g_framePixels = std::move(newPixels);
            g_frameWidth = newW;
            g_frameHeight = newH;

            std::memset(&g_bmi, 0, sizeof(g_bmi));
            g_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            g_bmi.bmiHeader.biWidth = g_frameWidth;
            g_bmi.bmiHeader.biHeight = -g_frameHeight;
            g_bmi.bmiHeader.biPlanes = 1;
            g_bmi.bmiHeader.biBitCount = 32;
            g_bmi.bmiHeader.biCompression = BI_RGB;
        }

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

        if (decompRes == 0) {
            g_streamFrameCount++;
            g_lastFrameTimeMs.store(GetTickCount64(), std::memory_order_relaxed);

            if (D2D1Renderer::IsInitialized()) {
                // Đẩy trực tiếp vùng thay đổi vào GPU VRAM và vẽ phần cứng cực nhanh (<0.2ms)
                D2D1Renderer::UpdateDirtyRect(delta.x, delta.y, delta.width, delta.height, g_framePixels.data(), g_frameWidth, g_frameHeight);
                D2D1Renderer::Render();
                g_renderFrameCount++;
            } else if (g_hVideoWnd) {
                // Fallback GDI nếu Direct2D không khả dụng
                InvalidateRect(g_hVideoWnd, NULL, FALSE);
                g_renderFrameCount++;
            }
        }

        LeaveCriticalSection(&g_cs);
    }

    void Cleanup() {
        if (g_hwnd) {
            KillTimer(g_hwnd, 1);
            g_hwnd = NULL;
        }
        g_hVideoWnd = NULL;
        D2D1Renderer::Cleanup();
        ChatWindow::Cleanup();
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
