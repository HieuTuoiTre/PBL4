// ============================================================================
// File: ManagerMain.cpp
// Mô tả: Điểm khởi nhập (Entry Point) của ứng dụng Manager (phía máy điều khiển).
// Chịu trách nhiệm:
//   1. Khởi tạo hạ tầng Windows (DPI Aware, Common Controls, Winsock2, GDI+).
//   2. Vòng lặp hiển thị cửa sổ kết nối (ConnectWindow) và xử lý kết nối 3 kênh TCP.
//   3. Khởi chạy phiên điều khiển từ xa (RemoteWindow, DataReceiver, AudioPlayer).
//   4. Vòng lặp thông điệp Windows (Message Loop) và dọn dẹp tài nguyên khi thoát.
// ============================================================================

// Nạp thư viện giao tiếp mạng dùng chung giữa Client và Manager
#include "../../Shared/Network.h"

// Nạp module tiếp nhận và phân phối dữ liệu (Video, Audio, Control) từ Client
#include "DataReceiver.h"

// Nạp module giao diện hiển thị màn hình điều khiển từ xa
#include "../UI/RemoteWindow.h"

// Nạp module bắt sự kiện chuột/bàn phím từ Manager để gửi sang Client
#include "../UI/InputHandler.h"

// Nạp module phát âm thanh nhận được từ máy Client
#include "../Media/AudioPlayer.h"

// Nạp module giao diện cửa sổ tìm kiếm và kết nối máy Client
#include "../UI/ConnectWindow.h"

// Nạp thư viện Windows API chuẩn
#include <windows.h>

// Nạp thư viện đồ họa GDI+ (dùng để giải nén ảnh JPEG và vẽ giao diện)
#include <gdiplus.h>

// Nạp các thư viện chuỗi chuẩn C/C++
#include <cstring>
#include <string>

// Chỉ thị cho trình liên kết (Linker) tự động liên kết với thư viện GDI+ của Windows
#pragma comment(lib, "gdiplus.lib")

// Khai báo 3 đối tượng TcpClient toàn cục đại diện cho 3 kênh kết nối TCP độc lập:
// 1. Kênh Control (Cổng 8080): Gửi sự kiện chuột/phím, lệnh Task Manager, File Explorer
TcpClient g_controlClient;

// 2. Kênh Video (Cổng 8081): Tiếp nhận liên tục các khung hình màn hình (JPEG) từ Client
TcpClient g_videoClient;

// 3. Kênh Audio (Cổng 8082): Tiếp nhận liên tục luồng âm thanh PCM trực tiếp từ Client
TcpClient g_audioClient;

// ============================================================================
// Hàm: ConnectAll
// Mục đích: Thiết lập đồng thời cả 3 kết nối TCP tới máy Client theo thứ tự.
// Tham số:
//   - ip: Địa chỉ IP của máy Client cần kết nối (ví dụ: "192.168.1.10" hoặc "127.0.0.1")
// Trả về: true nếu cả 3 kênh đều kết nối thành công; false nếu có bất kỳ kênh nào thất bại.
// ============================================================================
bool ConnectAll(const std::string& ip) {
    // 1. Kết nối tới cổng Control (8080)
    if (!g_controlClient.Connect(ip, Protocol::PORT_CONTROL)) {
        return false; // Thất bại ngay từ kênh Control thì dừng lại
    }

    // 2. Kết nối tới cổng Video (8081)
    if (!g_videoClient.Connect(ip, Protocol::PORT_VIDEO)) {
        g_controlClient.Close(); // Đóng kênh Control đã mở trước đó để tránh rò rỉ tài nguyên
        return false;
    }

    // 3. Kết nối tới cổng Audio (8082)
    if (!g_audioClient.Connect(ip, Protocol::PORT_AUDIO)) {
        g_controlClient.Close(); // Đóng kênh Control
        g_videoClient.Close();   // Đóng kênh Video
        return false;
    }

    // Cả 3 kênh mạng TCP đều đã được kết nối thành công
    return true;
}

// ============================================================================
// Hàm: CloseAll
// Mục đích: Đóng an toàn cả 3 socket kết nối TCP để giải phóng tài nguyên.
// ============================================================================
void CloseAll() {
    g_controlClient.Close(); // Đóng socket kênh Control
    g_videoClient.Close();   // Đóng socket kênh Video
    g_audioClient.Close();   // Đóng socket kênh Audio
}

// ============================================================================
// Hàm: WinMain
// Mục đích: Điểm bắt đầu thực thi của chương trình ứng dụng Windows GUI (không hiện console đen).
// Tham số:
//   - hInst: Handle định danh phiên bản (instance) hiện tại của ứng dụng.
//   - (hPrevInstance): Không còn dùng trong Windows hiện đại (luôn là NULL).
//   - (lpCmdLine): Chuỗi tham số dòng lệnh truyền vào chương trình.
//   - (nCmdShow): Trạng thái hiển thị cửa sổ ban đầu (phóng to, thu nhỏ, bình thường).
// ============================================================================
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // Bật chế độ nhận diện DPI màn hình (High-DPI Aware) để giao diện không bị mờ nhòe trên màn hình 2K/4K
    SetProcessDPIAware();

    // Nạp thư viện điều khiển giao diện người dùng mở rộng của Windows (Common Controls v6)
    HMODULE hComCtl = LoadLibraryA("comctl32.dll");
    if (hComCtl) {
        // Lấy con trỏ hàm InitCommonControls để khởi tạo các control như ListView, ProgressBar
        typedef void (WINAPI *INITPROC)(void);
        INITPROC pInit = (INITPROC)GetProcAddress(hComCtl, "InitCommonControls");
        if (pInit) pInit(); // Gọi hàm khởi tạo
    }

    // Khởi tạo thư viện Winsock2 (WSAStartup) để sẵn sàng truyền nhận qua mạng
    Network::Initialize();

    // Khởi tạo hệ thống đồ họa GDI+ của Windows
    ULONG_PTR gdiToken;
    Gdiplus::GdiplusStartupInput gdiInput;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, NULL);

    // Vòng lặp chính của ứng dụng: cho phép sau khi ngắt kết nối vẫn có thể quay lại cửa sổ Connect để kết nối máy khác
    while (true) {
        // Biến lưu địa chỉ IP đích mà người dùng lựa chọn hoặc nhập vào
        std::string targetIP = "";

        // Hiển thị cửa sổ quét LAN và chọn máy Client (hàm này chặn luồng cho đến khi bấm Kết nối hoặc Thoát)
        bool ok = ConnectWindow::Show(hInst, targetIP, [](const std::string& ip, std::string& outError) {
            // Callback được gọi khi người dùng bấm nút "Kết nối" trên giao diện ConnectWindow
            if (ConnectAll(ip)) {
                return true; // Kết nối thành công
            } else {
                // Gán thông báo lỗi chi tiết để hiển thị lên hộp thông báo cho người dùng
                outError = "Không thể kết nối tới " + ip + "!\nHãy kiểm tra lại địa chỉ IP và đảm bảo Client đang chạy.";
                return false; // Kết nối thất bại
            }
        });

        // Nếu người dùng đóng cửa sổ kết nối hoặc bấm nút "Thoát" thì thoát hẳn chương trình
        if (!ok || targetIP.empty()) {
            break;
        }

        // --- BẮT ĐẦU PHIÊN ĐIỀU KHIỂN TỪ XA ---

        // 1. Khởi tạo bộ thu phát sự kiện chuột và phím (gán socket Control cho InputHandler)
        InputHandler::Initialize(&g_controlClient);

        // 2. Khởi tạo hệ thống phát âm thanh (tự động tắt tiếng nếu kết nối tới chính máy mình 127.0.0.1 để tránh vọng tiếng)
        AudioPlayer::Initialize(targetIP == "127.0.0.1");

        // 3. Tạo và hiển thị cửa sổ điều khiển từ xa (RemoteWindow)
        RemoteWindow::Create(hInst, &g_controlClient);

        // 4. Khởi chạy 3 luồng nhận dữ liệu chạy ngầm (Control, Video, Audio)
        DataReceiver::Start(&g_controlClient, &g_videoClient, &g_audioClient);

        // 5. Vòng lặp thông điệp Windows (Message Loop) cho cửa sổ RemoteWindow
        MSG msg;
        // GetMessageW sẽ đón nhận và xử lý các sự kiện chuột, phím, vẽ lại cửa sổ
        while (GetMessageW(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg); // Dịch các thông điệp bàn phím (WM_KEYDOWN -> WM_CHAR)
            DispatchMessageW(&msg);  // Phân phối thông điệp tới hàm WindowProc tương ứng
        }

        // --- DỌN DẸP PHIÊN KẾT NỐI KHI ĐÓNG CỬA SỔ ĐIỀU KHIỂN ---

        // 1. Đóng toàn bộ các socket TCP để ngắt kết nối mạng ngay lập tức
        CloseAll();

        // 2. Dừng các luồng nhận dữ liệu ngầm và đợi các luồng kết thúc an toàn
        DataReceiver::Stop();

        // 3. Giải phóng tài nguyên phát âm thanh (WaveOut device)
        AudioPlayer::Cleanup();

        // 4. Dọn dẹp tài nguyên bộ nhớ đệm khung hình của RemoteWindow
        RemoteWindow::Cleanup();

        // Sau khi dọn dẹp xong, vòng lặp while(true) sẽ quay lại đầu để mở lại ConnectWindow
    }

    // Tắt hệ thống đồ họa GDI+ trước khi kết thúc tiến trình
    Gdiplus::GdiplusShutdown(gdiToken);

    // Dọn dẹp Winsock2 (WSACleanup)
    Network::Cleanup();

    // Giải phóng thư viện comctl32.dll nếu đã nạp
    if (hComCtl) FreeLibrary(hComCtl);

    // Trả về mã thoát 0 (kết thúc chương trình thành công)
    return 0;
}

