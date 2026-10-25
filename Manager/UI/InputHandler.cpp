// ============================================================================
// File: InputHandler.cpp
// Mô tả: Hiện thực các phương thức của module InputHandler.
// ============================================================================

// Nạp file header khai báo các hàm trong namespace InputHandler
#include "InputHandler.h"

// Nạp thư viện macro tiện ích mở rộng của Windows (cung cấp macro GET_X_LPARAM, GET_Y_LPARAM)
#include <windowsx.h>

namespace {
    // Con trỏ lưu kết nối mạng kênh Điều khiển (Control Channel 8080)
    TcpClient* g_client = nullptr;

    // Cờ trạng thái cho phép tương tác chuột/bàn phím (mặc định ban đầu là true: cho phép điều khiển)
    bool g_isControlling = true;
}

namespace InputHandler {
    // Khởi tạo kênh mạng truyền tin và đặt lại chế độ điều khiển về trạng thái kích hoạt
    void Initialize(TcpClient* client) {
        g_client = client;        // Lưu lại kết nối mạng điều khiển
        g_isControlling = true;    // Mặc định luôn bật quyền điều khiển khi mới kết nối
    }

    // Xử lý và gửi sự kiện chuột (di chuyển, click, cuộn)
    void SendMouse(uint8_t action, uint8_t btn, LPARAM lp, HWND hWnd, int frameW, int frameH, int toolbarH, int16_t wheel) {
        // Kiểm tra an toàn: nếu mất mạng, đang ở chế độ Chỉ xem, hoặc độ phân giải ảnh Client không hợp lệ thì bỏ qua
        if (!g_client || !g_isControlling || frameW <= 0 || frameH <= 0) return;

        // Trích xuất tọa độ X và Y cục bộ trên cửa sổ hiển thị từ tham số LPARAM của hệ điều hành
        int mouseX = GET_X_LPARAM(lp);
        int mouseY = GET_Y_LPARAM(lp);

        // Nếu vị trí chuột nằm trong phạm vi thanh công cụ Toolbar phía trên thì bỏ qua để tránh click nhầm vào màn hình remote
        if (mouseY < toolbarH) return;

        // Trừ đi chiều cao thanh Toolbar để tính ra tọa độ Y thực tế trong vùng hiển thị hình ảnh màn hình Client
        mouseY -= toolbarH;

        // Lấy kích thước vùng hiển thị hiện tại của cửa sổ RemoteWindow
        RECT r;
        GetClientRect(hWnd, &r);
        int drawHeight = r.bottom - toolbarH; // Chiều cao thực tế của khung ảnh sau khi trừ Toolbar

        // Kiểm tra tránh lỗi chia cho 0 khi cửa sổ bị thu nhỏ về 0
        if (drawHeight <= 0 || r.right <= 0) return;

        // Quy đổi tỷ lệ tọa độ từ vùng hiển thị của Manager về độ phân giải gốc của máy Client:
        // ClientX = ManagerX * ClientWidth / ManagerWidth
        // ClientY = ManagerY * ClientHeight / ManagerHeight
        Protocol::MousePayload mp = {
            action,                                                // Hành vi chuột (di chuyển, nhấn, thả, cuộn)
            btn,                                                   // Nút chuột liên quan (trái, phải, giữa)
            (int16_t)(mouseX * frameW / r.right),                  // Tọa độ X gốc trên Client
            (int16_t)(mouseY * frameH / drawHeight),               // Tọa độ Y gốc trên Client
            wheel                                                  // Độ dịch chuyển bánh xe cuộn (Wheel Delta)
        };

        // Đóng gói và gửi gói tin điều khiển chuột MSG_MOUSE_EVENT sang Client
        g_client->SendPacket(Protocol::MSG_MOUSE_EVENT, (char*)&mp, sizeof(mp));
    }

    // Xử lý và gửi sự kiện bàn phím (nhấn phím hoặc nhả phím)
    void SendKey(WPARAM wp, uint8_t isDown) {
        // Kiểm tra an toàn: chỉ gửi phím khi đã kết nối và đang ở chế độ cho phép điều khiển
        if (!g_client || !g_isControlling) return;

        // Đóng gói mã phím ảo Virtual Key Code (WPARAM) và trạng thái (nhấn=1, nhả=0)
        Protocol::KeyboardPayload kp = { (uint32_t)wp, isDown };

        // Gửi gói tin điều khiển bàn phím MSG_KEY_EVENT sang Client
        g_client->SendPacket(Protocol::MSG_KEY_EVENT, (char*)&kp, sizeof(kp));
    }

    // Đảo ngược chế độ điều khiển giữa "Đang điều khiển" và "Chỉ xem"
    void ToggleControlling() {
        g_isControlling = !g_isControlling;
    }

    // Trả về true nếu đang ở chế độ cho phép điều khiển, false nếu ở chế độ chỉ xem
    bool IsControlling() {
        return g_isControlling;
    }
}
