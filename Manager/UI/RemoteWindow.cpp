// ============================================================================
// File: RemoteWindow.cpp
// Mô tả: Hiện thực chi tiết cửa sổ điều khiển từ xa (RemoteWindow).
// Bao gồm:
//   - Thanh công cụ Toolbar đệm đôi chống rung (Double-Buffered GDI+) với đồ họa vector sắc nét.
//   - Cơ chế giải mã JPEG siêu tốc thông qua tái sử dụng đối tượng COM IStream (chống phân mảnh RAM).
//   - Hỗ trợ ghép khung hình chênh lệch (Delta Frame Blitting) đa luồng an toàn với Critical Section.
//   - Đo lường và hiển thị thời gian thực chỉ số khung hình trên giây (FPS Counter).
//   - Chuyển tiếp chuột/phím mượt mà và hỗ trợ các tổ hợp phím tắt nhanh (Ctrl+Alt, Ctrl+M, Ctrl+Tab, Ctrl+E).
// ============================================================================

// Nạp file header khai báo giao diện của RemoteWindow
#include "RemoteWindow.h"

// Nạp module xử lý và chuẩn hóa tọa độ tương tác chuột/phím
#include "InputHandler.h"

// Nạp module cửa sổ quản lý tác vụ Task Manager từ xa
#include "TaskManagerWindow.h"

// Nạp module cửa sổ duyệt tập tin File Explorer từ xa
#include "FileExplorerWindow.h"

// Nạp module phát âm thanh máy Client
#include "../Media/AudioPlayer.h"

// Nạp thư viện đồ họa cao cấp Windows GDI+
#include <gdiplus.h>

// Nạp các thư viện chuỗi ký tự, mảng động và toán học chuẩn C++
#include <string>
#include <vector>
#include <cmath>

namespace {
    // ========================================================================
    // CÁC BIẾN TOÀN CỤC NỘI BỘ VÀ TÀI NGUYÊN ĐỒ HỌA
    // ========================================================================
    HWND g_hwnd = NULL;                 // Handle cửa sổ điều khiển RemoteWindow
    TcpClient* g_client = nullptr;      // Kênh mạng TCP Điều khiển (Control Channel 8080)
    Gdiplus::Bitmap* g_frame = nullptr; // Khung đệm ảnh Bitmap tổng thể lưu trữ toàn bộ màn hình Client
    IStream* g_decodeStream = nullptr;  // Đối tượng luồng IStream COM tái sử dụng để nạp và giải nén JPEG
    CRITICAL_SECTION g_cs;              // Khóa đồng bộ bảo vệ khung hình g_frame giữa luồng nhận mạng và luồng vẽ giao diện

    int g_fps = 0;                      // Chỉ số khung hình trên giây tính toán được
    int g_frameCount = 0;               // Biến đếm số lượng frame nhận được trong khoảng thời gian 1 giây
    DWORD g_lastTime = 0;               // Thời điểm (TickCount) bắt đầu chu kỳ đo FPS 1000ms

    // Lấy kích thước chiều rộng và chiều cao ảnh màn hình máy Client từ xa hiện tại
    int GetFrameWidth() { return g_frame ? (int)g_frame->GetWidth() : 0; }
    int GetFrameHeight() { return g_frame ? (int)g_frame->GetHeight() : 0; }

    // ========================================================================
    // HỖ TRỢ DYNAMIC DPI SCALING
    // Giúp giao diện hiển thị rõ nét, không bị mờ nhòe trên màn hình 2K, 4K hoặc 150% Scale
    // ========================================================================
    float GetDpiScale(HWND hwnd) {
        typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
        // Nạp động hàm GetDpiForWindow từ thư viện user32.dll (chỉ có từ Windows 10 trở lên)
        static PFN_GetDpiForWindow pGetDpiForWindow = (PFN_GetDpiForWindow)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        UINT dpi = 96;
        if (pGetDpiForWindow && hwnd) {
            dpi = pGetDpiForWindow(hwnd); // Lấy mật độ điểm ảnh thực tế của cửa sổ
        } else {
            HDC hdc = GetDC(hwnd);
            if (hdc) {
                dpi = GetDeviceCaps(hdc, LOGPIXELSY); // Dự phòng cho các phiên bản Windows cũ hơn
                ReleaseDC(hwnd, hdc);
            }
        }
        if (dpi == 0) dpi = 96;
        return (float)dpi / 96.0f; // Tỷ lệ phóng to (ví dụ: 1.0f cho 100%, 1.25f cho 125%, 1.5f cho 150%)
    }

    // Tính toán chiều cao thanh công cụ Toolbar theo tỷ lệ DPI hiện tại
    int GetToolbarHeight(HWND hwnd) {
        return (int)std::round(46.0f * GetDpiScale(hwnd)); // Chuẩn 46px ở 100% DPI
    }

    // ========================================================================
    // ĐỊNH NGHĨA CÁC NÚT BẤM VÀ CẤU TRÚC DỮ LIỆU THANH CÔNG CỤ TOOLBAR
    // ========================================================================
    enum ToolbarButtonId {
        BTN_NONE = 0,    // Không có nút (hoặc khu vực nhãn không bấm được)
        BTN_MODE,        // Nút chuyển chế độ Điều khiển / Chỉ xem
        BTN_MUTE,        // Nút Bật / Tắt âm thanh từ máy Client
        BTN_VOL_DOWN,    // Nút Giảm âm lượng (-)
        BTN_VOL_UP,      // Nút Tăng âm lượng (+)
        BTN_TASK_MGR,    // Nút mở Task Manager quản lý tác vụ từ xa
        BTN_FILE_EXP     // Nút mở File Explorer quản lý tập tin từ xa
    };

    // Cấu trúc mô tả thuộc tính hình học và văn bản của mỗi nút bấm Toolbar
    struct ToolbarButton {
        ToolbarButtonId id;       // Định danh loại nút
        Gdiplus::RectF rect;      // Tọa độ hình học và kích thước nút
        std::wstring text;        // Nhãn chữ hiển thị trên nút
        std::wstring hint;        // Gợi ý phím tắt bàn phím
        bool isSpecial;           // Cờ đánh dấu nút đặc biệt cần vẽ viền nổi bật
    };

    ToolbarButtonId g_hoveredBtn = BTN_NONE; // Nút hiện đang được chuột rê vào (Hover state)
    ToolbarButtonId g_pressedBtn = BTN_NONE; // Nút hiện đang được nhấn giữ chuột trái (Pressed state)
    bool g_trackingMouseLeave = false;       // Cờ theo dõi khi chuột rời khỏi phạm vi cửa sổ

    // ========================================================================
    // HÀM TẠO ĐƯỜNG DẪN HÌNH CHỮ NHẬT BO GÓC (ROUNDED RECTANGLE PATH)
    // Dùng để vẽ các nút bấm bo góc hiện đại trong GDI+
    // ========================================================================
    void AddRoundRectPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
        float d = radius * 2.0f;
        if (d > rect.Width) d = rect.Width;
        if (d > rect.Height) d = rect.Height;
        path.Reset();
        path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);                               // Cung bo góc trên bên trái
        path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);              // Cung bo góc trên bên phải
        path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f); // Cung bo góc dưới bên phải
        path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);              // Cung bo góc dưới bên trái
        path.CloseFigure();
    }

    // ========================================================================
    // TÍNH TOÁN BỐ CỤC VÀ VỊ TRÍ CÁC NÚT TRÊN THANH CÔNG CỤ TOOLBAR
    // Tự động căn chỉnh khoảng cách theo tỷ lệ DPI của màn hình
    // ========================================================================
    std::vector<ToolbarButton> BuildToolbarButtons(HWND hwnd, int clientWidth, int tbHeight) {
        std::vector<ToolbarButton> buttons;
        float scale = GetDpiScale(hwnd);
        float gap = 6.0f * scale;              // Khoảng cách giữa các nút
        float btnHeight = 30.0f * scale;       // Chiều cao nút
        float btnY = (float)(tbHeight - btnHeight) / 2.0f; // Căn giữa theo trục dọc thanh toolbar
        float curX = 12.0f * scale;            // Tọa độ X xuất phát từ lề trái

        // 1. Nút Chế độ điều khiển / Chỉ xem (sử dụng icon chấm tròn vector thay vì emoji Unicode)
        bool isCtrl = InputHandler::IsControlling();
        std::wstring modeText = isCtrl ? L"Đang điều khiển" : L"Chỉ xem";
        std::wstring modeHint = L"Ctrl+Alt";
        float btnModeW = (float)std::round((isCtrl ? 140.0f : 96.0f) * scale);
        buttons.push_back({ BTN_MODE, Gdiplus::RectF(curX, btnY, btnModeW, btnHeight), modeText, modeHint, true });
        curX += btnModeW + gap;

        // 2. Nút Bật / Tắt âm thanh từ máy Client
        bool isMuted = AudioPlayer::IsMuted();
        std::wstring muteText = isMuted ? L"Đã tắt tiếng" : L"Âm thanh";
        std::wstring muteHint = L"Ctrl+M";
        float btnMuteW = (float)std::round((isMuted ? 112.0f : 92.0f) * scale);
        buttons.push_back({ BTN_MUTE, Gdiplus::RectF(curX, btnY, btnMuteW, btnHeight), muteText, muteHint, isMuted });
        curX += btnMuteW + gap;

        // 3. Cụm nút và nhãn điều chỉnh âm lượng: [ - ] [ 150% ] [ + ]
        float btnVolMiniW = (float)std::round(26.0f * scale);
        buttons.push_back({ BTN_VOL_DOWN, Gdiplus::RectF(curX, btnY, btnVolMiniW, btnHeight), L"−", L"Ctrl+↓", false });
        curX += btnVolMiniW + 2.0f * scale;

        int volPercent = (int)std::round(AudioPlayer::GetVolume() * 100.0f);
        float lblVolW = (float)std::round(52.0f * scale);
        buttons.push_back({ BTN_NONE, Gdiplus::RectF(curX, btnY, lblVolW, btnHeight), std::to_wstring(volPercent) + L"%", L"", false });
        curX += lblVolW + 2.0f * scale;

        buttons.push_back({ BTN_VOL_UP, Gdiplus::RectF(curX, btnY, btnVolMiniW, btnHeight), L"+", L"Ctrl+↑", false });
        curX += btnVolMiniW + gap * 2.0f;

        // 4. Nút mở bảng quản lý tác vụ Task Manager
        float btnTaskW = (float)std::round(120.0f * scale);
        buttons.push_back({ BTN_TASK_MGR, Gdiplus::RectF(curX, btnY, btnTaskW, btnHeight), L"Task Manager", L"Ctrl+Tab", false });
        curX += btnTaskW + gap;

        // 5. Nút mở bảng quản lý tập tin File Explorer
        float btnFileW = (float)std::round(115.0f * scale);
        buttons.push_back({ BTN_FILE_EXP, Gdiplus::RectF(curX, btnY, btnFileW, btnHeight), L"File Explorer", L"Ctrl+E", false });
        curX += btnFileW + gap;

        return buttons;
    }

    // ========================================================================
    // KIỂM TRA ĐIỂM CHẠM CHUỘT TRÊN TOOLBAR (HIT-TESTING)
    // Xác định xem chuột đang trỏ vào nút bấm nào trên thanh công cụ
    // ========================================================================
    ToolbarButtonId HitTestToolbar(HWND hwnd, int x, int y, int clientWidth, int tbHeight) {
        if (y < 0 || y >= tbHeight) return BTN_NONE; // Tọa độ Y nằm ngoài thanh toolbar
        auto buttons = BuildToolbarButtons(hwnd, clientWidth, tbHeight);
        Gdiplus::PointF pt((float)x, (float)y);
        for (const auto& btn : buttons) {
            if (btn.id != BTN_NONE && btn.rect.Contains(pt)) {
                return btn.id; // Trúng vào nút bấm
            }
        }
        return BTN_NONE;
    }

    // ========================================================================
    // VẼ THANH CÔNG CỤ TOOLBAR (RENDER TOOLBAR)
    // Sử dụng kỹ thuật Double-Buffering với Bitmap trong bộ nhớ, khử răng cưa toàn diện
    // ========================================================================
    void RenderToolbar(HWND hwnd, HDC hdc, const RECT& r, int tbHeight) {
        float scale = GetDpiScale(hwnd);
        int width = r.right;
        if (width <= 0 || tbHeight <= 0) return;

        // Tạo bộ đệm ảnh Bitmap 32-bit Alpha trong bộ nhớ RAM (tuyệt đối không nhấp nháy màn hình)
        Gdiplus::Bitmap memBmp(width, tbHeight, PixelFormat32bppPARGB);
        Gdiplus::Graphics g(&memBmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);                  // Khử răng cưa hình học (đường thẳng, hình tròn)
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit); // Tối ưu hiển thị văn bản ClearType sắc nét

        // Nền Toolbar: Màu than chì Graphite cao cấp RGB(24, 25, 30)
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 24, 25, 30));
        g.FillRectangle(&bgBrush, 0, 0, width, tbHeight);

        // Viền dưới 1px tinh tế ngăn cách giữa Toolbar và khung hình máy tính từ xa
        Gdiplus::Pen borderPen(Gdiplus::Color(255, 45, 47, 56), 1.0f);
        g.DrawLine(&borderPen, 0.0f, (float)(tbHeight - 1), (float)width, (float)(tbHeight - 1));

        auto buttons = BuildToolbarButtons(hwnd, width, tbHeight);

        // Khởi tạo các phông chữ Segoe UI
        Gdiplus::FontFamily fontFamily(L"Segoe UI");
        Gdiplus::Font fontBtn(&fontFamily, 12.0f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font fontVol(&fontFamily, 12.0f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

        // Định dạng căn giữa cho văn bản nút
        Gdiplus::StringFormat sfCenter;
        sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
        sfCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        sfCenter.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

        Gdiplus::GraphicsPath path;
        float radius = 5.0f * scale; // Bán kính bo góc của nút bấm

        for (const auto& btn : buttons) {
            // Không vẽ các nút nếu kích thước cửa sổ quá hẹp và đè vào badge FPS bên phải
            if (btn.rect.GetRight() > (float)(width - 125 * scale)) continue;

            AddRoundRectPath(path, btn.rect, radius);

            if (btn.id == BTN_NONE) {
                // Nhãn hiển thị phần trăm âm lượng (chỉ hiển thị văn bản tĩnh)
                Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 200, 205, 215));
                g.DrawString(btn.text.c_str(), -1, &fontVol, btn.rect, &sfCenter, &textBrush);
                continue;
            }

            bool isHovered = (g_hoveredBtn == btn.id);
            bool isPressed = (g_pressedBtn == btn.id);

            Gdiplus::Color fillColor;
            Gdiplus::Color borderColor;
            Gdiplus::Color textColor;

            // Xử lý phối màu thông minh cho từng loại nút tùy thuộc vào trạng thái
            if (btn.id == BTN_MODE) {
                bool isCtrl = InputHandler::IsControlling();
                if (isCtrl) {
                    // Màu xanh lá đậm khi đang được quyền điều khiển
                    fillColor = isPressed ? Gdiplus::Color(255, 14, 52, 28) : (isHovered ? Gdiplus::Color(255, 20, 68, 36) : Gdiplus::Color(255, 16, 58, 31));
                    borderColor = Gdiplus::Color(255, 34, 197, 94);
                    textColor = Gdiplus::Color(255, 134, 239, 172);
                } else {
                    // Màu xám khi đang ở chế độ Chỉ xem
                    fillColor = isPressed ? Gdiplus::Color(255, 30, 32, 38) : (isHovered ? Gdiplus::Color(255, 50, 53, 64) : Gdiplus::Color(255, 38, 40, 48));
                    borderColor = Gdiplus::Color(255, 75, 80, 95);
                    textColor = Gdiplus::Color(255, 160, 165, 180);
                }
            } else if (btn.id == BTN_MUTE && AudioPlayer::IsMuted()) {
                // Màu đỏ cảnh báo khi âm thanh bị tắt (Muted)
                fillColor = isPressed ? Gdiplus::Color(255, 60, 20, 20) : (isHovered ? Gdiplus::Color(255, 80, 28, 28) : Gdiplus::Color(255, 68, 24, 24));
                borderColor = Gdiplus::Color(255, 239, 68, 68);
                textColor = Gdiplus::Color(255, 252, 165, 165);
            } else {
                // Màu nút tiêu chuẩn hiện đại
                fillColor = isPressed ? Gdiplus::Color(255, 28, 30, 36) : (isHovered ? Gdiplus::Color(255, 52, 55, 66) : Gdiplus::Color(255, 38, 40, 48));
                borderColor = isHovered ? Gdiplus::Color(255, 85, 90, 108) : Gdiplus::Color(255, 58, 61, 74);
                textColor = isHovered ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 220, 224, 232);
            }

            // Tô nền và vẽ viền nút bấm
            Gdiplus::SolidBrush fillBrush(fillColor);
            g.FillPath(&fillBrush, &path);
            Gdiplus::Pen pen(borderColor, 1.0f);
            g.DrawPath(&pen, &path);

            Gdiplus::SolidBrush textBrush(textColor);

            if (btn.id == BTN_MODE) {
                // Vẽ icon chấm tròn vector trạng thái ngay phía trước chữ
                bool isCtrl = InputHandler::IsControlling();
                float dotSize = 7.0f * scale;
                float dotX = btn.rect.X + 10.0f * scale;
                float dotY = btn.rect.Y + (btn.rect.Height - dotSize) / 2.0f;
                Gdiplus::Color dotColor = isCtrl ? Gdiplus::Color(255, 34, 197, 94) : Gdiplus::Color(255, 148, 154, 168);
                Gdiplus::SolidBrush dotBrush(dotColor);
                g.FillEllipse(&dotBrush, dotX, dotY, dotSize, dotSize);

                // Dịch văn bản sang phải để chừa chỗ cho chấm tròn
                Gdiplus::RectF textRect = btn.rect;
                textRect.X += (dotSize + 4.0f * scale);
                textRect.Width -= (dotSize + 4.0f * scale);
                g.DrawString(btn.text.c_str(), -1, &fontBtn, textRect, &sfCenter, &textBrush);
            } else {
                g.DrawString(btn.text.c_str(), -1, &fontBtn, btn.rect, &sfCenter, &textBrush);
            }
        }

        // ====================================================================
        // HUY HIỆU ĐO FPS VÀ TÍN HIỆU TRỰC TIẾP (LIVE FPS BADGE) Ở GÓC PHẢI
        // ====================================================================
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

        // Chấm tín hiệu: Xanh lá cây nếu FPS >= 20, Vàng hổ phách nếu mạng chậm FPS < 20
        Gdiplus::Color dotColor = (g_fps >= 20) ? Gdiplus::Color(255, 34, 197, 94) : Gdiplus::Color(255, 245, 158, 11);
        Gdiplus::SolidBrush dotBrush(dotColor);
        float dotSize = 7.0f * scale;
        float dotX = badgeX + 12.0f * scale;
        float dotY = badgeY + (badgeH - dotSize) / 2.0f;
        g.FillEllipse(&dotBrush, dotX, dotY, dotSize, dotSize);

        // Chuỗi chỉ số FPS
        std::wstring fpsStr = std::to_wstring(g_fps) + L" FPS";
        Gdiplus::RectF fpsTextRect(dotX + dotSize + 6.0f * scale, badgeY, badgeW - dotSize - 18.0f * scale, badgeH);
        Gdiplus::StringFormat sfLeft;
        sfLeft.SetAlignment(Gdiplus::StringAlignmentNear);
        sfLeft.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::SolidBrush fpsTextBrush(Gdiplus::Color(255, 215, 220, 230));
        g.DrawString(fpsStr.c_str(), -1, &fontBtn, fpsTextRect, &sfLeft, &fpsTextBrush);

        // Sao chép nhanh (BitBlt) từ khung đệm Bitmap trong RAM ra thiết bị hiển thị HDC
        Gdiplus::Graphics targetG(hdc);
        targetG.DrawImage(&memBmp, 0, 0);
    }

    // ========================================================================
    // HÀM XỬ LÝ THÔNG ĐIỆP CHÍNH CỦA CỬA SỔ REMOTEWINDOW (WNDPROC)
    // ========================================================================
    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        int tbHeight = GetToolbarHeight(hwnd);

        switch (msg) {
            // Xử lý sự kiện vẽ màn hình
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT r;
                GetClientRect(hwnd, &r);

                // Nếu vùng cần vẽ chạm vào thanh Toolbar phía trên -> Vẽ Toolbar
                if (ps.rcPaint.top < tbHeight) {
                    RenderToolbar(hwnd, hdc, r, tbHeight);
                }

                // Nếu vùng cần vẽ nằm dưới Toolbar -> Vẽ khung hình máy tính Client
                if (ps.rcPaint.bottom > tbHeight) {
                    EnterCriticalSection(&g_cs);
                    if (g_frame) {
                        Gdiplus::Graphics g(hdc);
                        // Cấu hình chế độ vẽ siêu tốc không nội suy để đạt tốc độ FPS tối đa
                        g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
                        g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
                        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
                        // Kéo giãn khung hình sao cho vừa vặn vùng hiển thị còn lại
                        g.DrawImage(g_frame, 0, tbHeight, r.right, r.bottom - tbHeight);
                    }
                    LeaveCriticalSection(&g_cs);
                }

                EndPaint(hwnd, &ps);
                return 0;
            }

            // Bỏ qua vẽ nền để chống giật và chớp trắng màn hình
            case WM_ERASEBKGND: return 1;

            // Xử lý khi người dùng đổi độ phân giải màn hình hoặc kéo cửa sổ qua màn hình khác có DPI khác
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

            // Thay đổi con trỏ chuột thành hình bàn tay (Hand) khi di chuyển qua các nút bấm Toolbar
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

            // Xử lý khi chuột di chuyển
            case WM_MOUSEMOVE: {
                int mouseX = (short)LOWORD(lp);
                int mouseY = (short)HIWORD(lp);

                // Chuột đang di chuyển trong phạm vi thanh Toolbar
                if (mouseY < tbHeight) {
                    RECT r;
                    GetClientRect(hwnd, &r);
                    ToolbarButtonId hit = HitTestToolbar(hwnd, mouseX, mouseY, r.right, tbHeight);
                    if (hit != g_hoveredBtn) {
                        g_hoveredBtn = hit;
                        RECT tbRect = { 0, 0, r.right, tbHeight };
                        InvalidateRect(hwnd, &tbRect, FALSE); // Vẽ lại thanh Toolbar để hiển thị hiệu ứng Hover
                    }
                    if (!g_trackingMouseLeave) {
                        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                        TrackMouseEvent(&tme); // Đăng ký lắng nghe khi chuột rời khỏi cửa sổ
                        g_trackingMouseLeave = true;
                    }
                    return 0;
                }

                // Chuột rời khỏi Toolbar đi vào khung ảnh máy tính từ xa
                if (g_hoveredBtn != BTN_NONE) {
                    g_hoveredBtn = BTN_NONE;
                    RECT r;
                    GetClientRect(hwnd, &r);
                    RECT tbRect = { 0, 0, r.right, tbHeight };
                    InvalidateRect(hwnd, &tbRect, FALSE);
                }

                // Gửi sự kiện di chuyển chuột sang máy Client
                InputHandler::SendMouse(Protocol::MOUSE_ACTION_MOVE, Protocol::BUTTON_NONE, lp, hwnd, GetFrameWidth(), GetFrameHeight(), tbHeight);
                return 0;
            }

            // Xử lý khi chuột rời khỏi phạm vi cửa sổ
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

            // Xử lý khi nhấn giữ chuột trái
            case WM_LBUTTONDOWN: {
                int mouseX = (short)LOWORD(lp);
                int mouseY = (short)HIWORD(lp);

                // Nhấn chuột trên thanh Toolbar
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

                // Nhấn chuột trong vùng màn hình máy từ xa
                InputHandler::SendMouse(Protocol::MOUSE_ACTION_PRESS, Protocol::BUTTON_LEFT, lp, hwnd, GetFrameWidth(), GetFrameHeight(), tbHeight);
                return 0;
            }

            // Xử lý khi nhả chuột trái
            case WM_LBUTTONUP: {
                int mouseX = (short)LOWORD(lp);
                int mouseY = (short)HIWORD(lp);

                // Nếu trước đó đang nhấn nút Toolbar -> Thực thi hành động tương ứng của nút
                if (g_pressedBtn != BTN_NONE) {
                    RECT r;
                    GetClientRect(hwnd, &r);
                    ToolbarButtonId hit = HitTestToolbar(hwnd, mouseX, mouseY, r.right, tbHeight);
                    if (hit == g_pressedBtn) {
                        switch (hit) {
                            case BTN_MODE:
                                InputHandler::ToggleControlling(); // Bật/Tắt chế độ điều khiển
                                break;
                            case BTN_MUTE:
                                AudioPlayer::ToggleMute();         // Bật/Tắt âm thanh
                                break;
                            case BTN_VOL_DOWN:
                                AudioPlayer::DecreaseVolume();     // Giảm âm lượng
                                break;
                            case BTN_VOL_UP:
                                AudioPlayer::IncreaseVolume();     // Tăng âm lượng
                                break;
                            case BTN_TASK_MGR:
                                TaskManagerWindow::Show(hwnd, g_client); // Mở Task Manager từ xa
                                break;
                            case BTN_FILE_EXP:
                                FileExplorerWindow::Show(hwnd, g_client); // Mở File Explorer từ xa
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

                // Nhả chuột trong vùng màn hình máy từ xa
                InputHandler::SendMouse(Protocol::MOUSE_ACTION_RELEASE, Protocol::BUTTON_LEFT, lp, hwnd, GetFrameWidth(), GetFrameHeight(), tbHeight);
                return 0;
            }

            // Xử lý sự kiện nhấn phím và các phím tắt hệ thống
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                // Phím tắt Ctrl + Alt: Chuyển đổi nhanh chế độ Điều khiển / Chỉ xem
                if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000)) {
                    if ((lp & (1 << 30)) == 0) { // Kiểm tra chỉ kích hoạt 1 lần khi phím vừa được nhấn xuống
                        InputHandler::ToggleControlling();
                        RECT r; GetClientRect(hwnd, &r);
                        RECT toolbarRect = {0, 0, r.right, tbHeight};
                        InvalidateRect(hwnd, &toolbarRect, FALSE);
                    }
                    return 0;
                }

                // Phím tắt Ctrl + Tab: Mở cửa sổ Task Manager từ xa
                if (wp == VK_TAB && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                    TaskManagerWindow::Show(hwnd, g_client);
                    return 0;
                }

                // Phím tắt Ctrl + E: Mở cửa sổ File Explorer từ xa
                if (wp == 'E' && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                    FileExplorerWindow::Show(hwnd, g_client);
                    return 0;
                }

                // Phím tắt Ctrl + M: Bật / Tắt âm thanh
                if (wp == 'M' && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                    if ((lp & (1 << 30)) == 0) {
                        AudioPlayer::ToggleMute();
                        RECT r; GetClientRect(hwnd, &r);
                        RECT toolbarRect = {0, 0, r.right, tbHeight};
                        InvalidateRect(hwnd, &toolbarRect, FALSE);
                    }
                    return 0;
                }

                // Phím tắt Ctrl + Mũi tên Lên: Tăng âm lượng
                if (wp == VK_UP && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                    AudioPlayer::IncreaseVolume();
                    RECT r; GetClientRect(hwnd, &r);
                    RECT toolbarRect = {0, 0, r.right, tbHeight};
                    InvalidateRect(hwnd, &toolbarRect, FALSE);
                    return 0;
                }

                // Phím tắt Ctrl + Mũi tên Xuống: Giảm âm lượng
                if (wp == VK_DOWN && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                    AudioPlayer::DecreaseVolume();
                    RECT r; GetClientRect(hwnd, &r);
                    RECT toolbarRect = {0, 0, r.right, tbHeight};
                    InvalidateRect(hwnd, &toolbarRect, FALSE);
                    return 0;
                }

                // Nếu đang ở chế độ cho phép tương tác thì chuyển tiếp phím bấm sang máy Client
                if (InputHandler::IsControlling()) {
                    InputHandler::SendKey(wp, 1);
                    return 0;
                }
                break;
            }

            // Xử lý sự kiện nhả phím
            case WM_KEYUP:
            case WM_SYSKEYUP: {
                if (InputHandler::IsControlling()) {
                    InputHandler::SendKey(wp, 0); // Gửi thông báo nhả phím sang máy Client
                    return 0;
                }
                break;
            }

            // Nhấn chuột phải
            case WM_RBUTTONDOWN: InputHandler::SendMouse(Protocol::MOUSE_ACTION_PRESS, Protocol::BUTTON_RIGHT, lp, hwnd, GetFrameWidth(), GetFrameHeight(), tbHeight); return 0;

            // Nhả chuột phải
            case WM_RBUTTONUP:   InputHandler::SendMouse(Protocol::MOUSE_ACTION_RELEASE, Protocol::BUTTON_RIGHT, lp, hwnd, GetFrameWidth(), GetFrameHeight(), tbHeight); return 0;

            // Cuộn bánh xe chuột
            case WM_MOUSEWHEEL:  InputHandler::SendMouse(Protocol::MOUSE_ACTION_WHEEL, Protocol::BUTTON_NONE, lp, hwnd, GetFrameWidth(), GetFrameHeight(), tbHeight, GET_WHEEL_DELTA_WPARAM(wp)); return 0;

            // Đóng cửa sổ và thoát ứng dụng
            case WM_DESTROY: PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

namespace RemoteWindow {
    // ========================================================================
    // KHỞI TẠO CỬA SỔ REMOTEWINDOW
    // ========================================================================
    HWND Create(HINSTANCE hInst, TcpClient* client) {
        g_client = client;
        InitializeCriticalSection(&g_cs); // Khởi tạo Critical Section bảo vệ khung hình
        CreateStreamOnHGlobal(NULL, TRUE, &g_decodeStream); // Cấp phát 1 IStream COM duy nhất dùng chung cho giải nén
        g_lastTime = GetTickCount();

        // Đăng ký lớp cửa sổ MgrWindow
        WNDCLASSW wc = {};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"MgrWindow";
        RegisterClassW(&wc);

        // Tạo cửa sổ ở chế độ phóng to cực đại (WS_MAXIMIZE)
        g_hwnd = CreateWindowExW(0, L"MgrWindow", L"LanRemote",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_MAXIMIZE,
            CW_USEDEFAULT, CW_USEDEFAULT, 1920, 1080, 0, 0, hInst, 0);
        return g_hwnd;
    }

    // ========================================================================
    // CẬP NHẬT VÀ VẼ KHỐI KHUNG HÌNH MỚI (UPDATEFRAME)
    // Tái sử dụng đối tượng IStream COM để giải nén JPEG và vẽ chắp vá vùng thay đổi (Delta Blitting)
    // ========================================================================
    void UpdateFrame(const Protocol::VideoDeltaHeader& delta, const char* jpegData, int jpegSize) {
        if (jpegSize <= 0 || !jpegData || !g_decodeStream) return;

        // Đưa con trỏ đọc/ghi của IStream về vị trí ban đầu mà không giải phóng bộ nhớ
        LARGE_INTEGER liZero = {};
        ULARGE_INTEGER uliSize = {};
        uliSize.QuadPart = (ULONGLONG)jpegSize;

        g_decodeStream->Seek(liZero, STREAM_SEEK_SET, NULL);
        g_decodeStream->SetSize(uliSize); // Đặt lại kích thước luồng theo đúng dung lượng gói JPEG

        // Ghi dữ liệu JPEG nhị phân vào luồng IStream
        ULONG written = 0;
        if (FAILED(g_decodeStream->Write(jpegData, (ULONG)jpegSize, &written)) || (int)written != jpegSize) {
            return;
        }
        g_decodeStream->Seek(liZero, STREAM_SEEK_SET, NULL); // Đưa lại về đầu luồng để chuẩn bị nạp vào GDI+

        // Nạp và giải mã ảnh JPEG thông qua GDI+ Image Stream
        Gdiplus::Image* img = Gdiplus::Image::FromStream(g_decodeStream);
        if (img && img->GetLastStatus() == Gdiplus::Ok) {
            int reqW = delta.x + delta.width;
            int reqH = delta.y + delta.height;

            EnterCriticalSection(&g_cs); // Bắt đầu vùng khóa đồng bộ an toàn dữ liệu ảnh

            // Nếu khung hình tổng chưa tồn tại thì tạo mới theo kích thước khối ảnh
            if (!g_frame) {
                g_frame = new Gdiplus::Bitmap(reqW, reqH, PixelFormat32bppPARGB);
            } else if ((int)g_frame->GetWidth() < reqW || (int)g_frame->GetHeight() < reqH) {
                // Tự động mở rộng khung nền nếu ban đầu Client gửi khối Delta nhỏ hơn kích thước đầy đủ
                int newW = ((int)g_frame->GetWidth() > reqW) ? (int)g_frame->GetWidth() : reqW;
                int newH = ((int)g_frame->GetHeight() > reqH) ? (int)g_frame->GetHeight() : reqH;
                Gdiplus::Bitmap* expanded = new Gdiplus::Bitmap(newW, newH, PixelFormat32bppPARGB);
                Gdiplus::Graphics gExp(expanded);
                gExp.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
                gExp.DrawImage(g_frame, 0, 0);
                delete g_frame;
                g_frame = expanded;
            }

            // Vẽ đè vùng ảnh JPEG vừa giải mã lên đúng tọa độ x, y, width, height của màn hình
            Gdiplus::Graphics g(g_frame);
            g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
            g.DrawImage(img, delta.x, delta.y, delta.width, delta.height);

            LeaveCriticalSection(&g_cs); // Rời khỏi vùng khóa đồng bộ

            // Tính toán chỉ số khung hình FPS
            g_frameCount++;
            DWORD currentTime = GetTickCount();
            bool fpsUpdated = false;
            if (currentTime - g_lastTime >= 1000) {
                g_fps = g_frameCount;      // Ghi nhận số frame nhận được trong 1 giây qua
                g_frameCount = 0;          // Đặt lại bộ đếm cho giây tiếp theo
                g_lastTime = currentTime;
                fpsUpdated = true;
            }

            // Kích hoạt vẽ lại vùng ảnh trên cửa sổ
            if (g_hwnd) {
                int tbHeight = GetToolbarHeight(g_hwnd);
                if (fpsUpdated) {
                    RECT r;
                    GetClientRect(g_hwnd, &r);
                    RECT tbRect = { 0, 0, r.right, tbHeight };
                    InvalidateRect(g_hwnd, &tbRect, FALSE); // Cập nhật lại chỉ số FPS trên Toolbar
                }
                RECT clientRect;
                GetClientRect(g_hwnd, &clientRect);
                clientRect.top = tbHeight;
                InvalidateRect(g_hwnd, &clientRect, FALSE); // Cập nhật lại vùng hiển thị màn hình từ xa
            }
        }
        if (img) delete img; // Giải phóng đối tượng ảnh tạm
    }

    // ========================================================================
    // GIẢI PHÓNG TÀI NGUYÊN REMOTEWINDOW
    // ========================================================================
    void Cleanup() {
        if (g_decodeStream) {
            g_decodeStream->Release(); // Giải phóng đối tượng COM IStream
            g_decodeStream = nullptr;
        }
        if (g_frame) {
            delete g_frame;           // Giải phóng khung đệm Bitmap GDI+
            g_frame = nullptr;
        }
        DeleteCriticalSection(&g_cs); // Hủy khóa Critical Section
    }
}
