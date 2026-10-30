#pragma once

#include <windows.h>
#include <d2d1.h>
#include <cstdint>

namespace D2D1Renderer {
    // Khởi tạo Direct2D Hardware Accelerator gắn vào HWND hiển thị video
    bool Initialize(HWND hVideoWnd);

    // Dọn dẹp tài nguyên Direct2D khi đóng cửa sổ
    void Cleanup();

    // Thông báo thay đổi kích thước vùng hiển thị
    void Resize(int width, int height);

    // Cập nhật vùng thay đổi (Dirty Rectangle) thẳng lên VRAM GPU
    bool UpdateDirtyRect(int deltaX, int deltaY, int deltaW, int deltaH, const uint32_t* pixelData, int frameW, int frameH);

    // Vẽ khung hình bằng GPU với bộ lọc Bilinear Filtering cực nhanh (<0.2ms)
    void Render();

    // Lấy tọa độ và kích thước khung hình sau khi căn đúng tỉ lệ Aspect Ratio (Letterbox/Pillarbox)
    void GetViewport(float& outX, float& outY, float& outW, float& outH);

    // Kiểm tra xem Direct2D đã sẵn sàng hay chưa (để fallback về GDI nếu máy không hỗ trợ)
    bool IsInitialized();
}
