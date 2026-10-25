// ============================================================================
// File: DataReceiver.cpp
// Mô tả: Hiện thực module DataReceiver - Bộ thu nhận và phân phối dữ liệu mạng.
// Chịu trách nhiệm:
//   - Chạy 3 vòng lặp mạng độc lập trên 3 thread riêng:
//       1. ReceiveControlLoop (Cổng 8080): Nhận dữ liệu Task Manager, File Explorer
//       2. ReceiveVideoLoop   (Cổng 8081): Nhận và giải mã khung hình JPEG
//       3. ReceiveAudioLoop   (Cổng 8082): Nhận âm thanh PCM16 và đưa vào AudioPlayer
// ============================================================================

// Nạp file header khai báo giao diện của DataReceiver
#include "DataReceiver.h"

// Nạp các cấu trúc gói tin (PacketHeader, VideoDeltaHeader, SysInfoPayload, ...)
#include "../../Shared/Protocol.h"

// Nạp module giao diện hiển thị màn hình từ xa (cập nhật khung hình)
#include "../UI/RemoteWindow.h"

// Nạp module giao diện Task Manager (cập nhật CPU/RAM/Disk và danh sách tiến trình)
#include "../UI/TaskManagerWindow.h"

// Nạp module giao diện File Explorer (cập nhật danh sách ổ đĩa, thư mục, tải file)
#include "../UI/FileExplorerWindow.h"

// Nạp module phát âm thanh
#include "../Media/AudioPlayer.h"

// Nạp thư viện đa luồng chuẩn C++11
#include <thread>

// Nạp mảng động std::vector
#include <vector>

// Nạp biến nguyên tử std::atomic hỗ trợ đồng bộ đa luồng không cần khóa mutex
#include <atomic>

// Nạp các hàm thao tác bộ nhớ (memcpy, memset)
#include <cstring>

namespace {
    // Biến cờ nguyên tử kiểm soát vòng lặp của cả 3 luồng nhận dữ liệu
    std::atomic<bool> g_isRunning(false);

    // Luồng nhận dữ liệu điều khiển và quản lý (Task Manager, File Explorer, System Info)
    std::thread g_controlThread;

    // Luồng nhận luồng video liên tục (các khung hình JPEG)
    std::thread g_videoThread;

    // Luồng nhận luồng âm thanh liên tục (các chunk PCM 16-bit)
    std::thread g_audioThread;

    // ========================================================================
    // Hàm: ReceiveControlLoop
    // Mô tả: Vòng lặp nhận dữ liệu kênh Control (Cổng 8080).
    // Tham số: client - Con trỏ tới đối tượng TcpClient của kênh Control.
    // ========================================================================
    void ReceiveControlLoop(TcpClient* client) {
        // Biến lưu phần đầu gói tin (Header: type + size)
        Protocol::PacketHeader hdr;

        // Vòng lặp tiếp nhận liên tục: trước hết nhận đúng 5 bytes phần Header
        while (g_isRunning && client->ReceiveExact((char*)&hdr, sizeof(hdr))) {

            // Trường hợp đặc biệt: Gói tin báo hiệu kết thúc tải một file (kích thước payload = 0)
            if (hdr.type == Protocol::MSG_FILE_END) {
                FileExplorerWindow::OnFileEnd(); // Báo cho cửa sổ File Explorer biết file đã tải xong
                continue; // Chuyển sang đón nhận gói tin tiếp theo
            }

            // Kiểm tra an toàn: Nếu gói tin rỗng hoặc kích thước vượt quá 32MB thì coi là bất thường
            if (hdr.size == 0 || hdr.size > 32 * 1024 * 1024) continue;

            // Cấp phát vùng nhớ đệm vừa đủ chứa phần thân dữ liệu (payload)
            std::vector<char> buf(hdr.size);

            // Đọc chính xác toàn bộ số bytes của phần thân dữ liệu từ socket
            if (!client->ReceiveExact(buf.data(), (int)hdr.size)) break; // Mất kết nối -> thoát vòng lặp

            // Phân loại và xử lý gói tin dựa vào trường type trong Header
            switch (hdr.type) {
                // 1. Nhận thông tin hệ thống Client (Phần trăm CPU, RAM, Ổ đĩa)
                case Protocol::MSG_SYS_INFO_RESPONSE: {
                    // Kiểm tra kích thước dữ liệu nhận được có khớp với cấu trúc SysInfoPayload không
                    if (hdr.size < sizeof(Protocol::SysInfoPayload)) break;
                    Protocol::SysInfoPayload sys;
                    memcpy(&sys, buf.data(), sizeof(sys)); // Sao chép dữ liệu vào cấu trúc
                    TaskManagerWindow::UpdateSysInfo(sys);  // Đẩy sang cập nhật thẻ đo trên TaskManagerWindow
                    break;
                }

                // 2. Nhận danh sách toàn bộ các tiến trình đang chạy bên Client
                case Protocol::MSG_PROCESS_LIST_RESPONSE: {
                    if (hdr.size < sizeof(Protocol::ProcessInfoPayload)) break;
                    // Tính tổng số lượng tiến trình nhận được
                    int count = (int)(hdr.size / sizeof(Protocol::ProcessInfoPayload));
                    // Ép kiểu con trỏ dữ liệu sang mảng cấu trúc ProcessInfoPayload
                    const Protocol::ProcessInfoPayload* pList = (const Protocol::ProcessInfoPayload*)buf.data();
                    TaskManagerWindow::UpdateProcessList(pList, count); // Cập nhật bảng tiến trình
                    break;
                }

                // 3. Nhận danh sách các ổ đĩa của Client (VD: "C:\", "D:\")
                case Protocol::MSG_DRIVE_LIST_RESPONSE: {
                    // Chuyển mảng char thành chuỗi std::string
                    std::string content(buf.begin(), buf.end());
                    FileExplorerWindow::OnDriveListResponse(content); // Hiển thị các ổ đĩa lên bảng File Explorer
                    break;
                }

                // 4. Nhận danh sách các thư mục và tập tin bên trong một thư mục
                case Protocol::MSG_DIR_RESPONSE: {
                    std::string content(buf.begin(), buf.end());
                    FileExplorerWindow::OnDirResponse(content); // Hiển thị nội dung thư mục lên File Explorer
                    break;
                }

                // 5. Nhận thông tin chi tiết của tập tin chuẩn bị tải về (Tên file, kích thước tổng)
                case Protocol::MSG_FILE_INFO: {
                    if (hdr.size < sizeof(Protocol::FileInfoPayload)) break;
                    Protocol::FileInfoPayload info;
                    memcpy(&info, buf.data(), sizeof(info));
                    info.fileName[sizeof(info.fileName) - 1] = '\0'; // Đảm bảo chuỗi kết thúc bằng NULL
                    FileExplorerWindow::OnFileInfo(info);            // Khởi tạo thanh tiến trình tải file
                    break;
                }

                // 6. Nhận một khối dữ liệu nhỏ (chunk) của file đang tải về
                case Protocol::MSG_FILE_CHUNK: {
                    FileExplorerWindow::OnFileChunk(buf.data(), (int)hdr.size); // Ghi chunk vào file trên ổ đĩa
                    break;
                }

                // Bỏ qua các loại gói tin chưa hỗ trợ hoặc không mong muốn
                default:
                    break;
            }
        }
    }

    // ========================================================================
    // Hàm: ReceiveVideoLoop
    // Mô tả: Vòng lặp nhận dữ liệu kênh Video (Cổng 8081).
    // Tham số: client - Con trỏ tới đối tượng TcpClient của kênh Video.
    // ========================================================================
    void ReceiveVideoLoop(TcpClient* client) {
        Protocol::PacketHeader hdr;

        // Vòng lặp liên tục đọc Header của gói tin Video
        while (g_isRunning && client->ReceiveExact((char*)&hdr, sizeof(hdr))) {
            // Kiểm tra an toàn dung lượng gói video (không quá 32MB)
            if (hdr.size == 0 || hdr.size > 32 * 1024 * 1024) continue;

            // Cấp phát bộ đệm nhận toàn bộ gói hình ảnh
            std::vector<char> buf(hdr.size);
            if (!client->ReceiveExact(buf.data(), (int)hdr.size)) break;

            // Kiểm tra mã loại gói tin là gói khung hình video
            if (hdr.type == Protocol::MSG_VIDEO_FRAME) {
                // Đảm bảo kích thước gói lớn hơn kích thước của phần tiêu đề Delta
                if (hdr.size > sizeof(Protocol::VideoDeltaHeader)) {
                    // Trích xuất tiêu đề vùng thay đổi (tọa độ x, y, chiều rộng, chiều cao)
                    Protocol::VideoDeltaHeader delta;
                    memcpy(&delta, buf.data(), sizeof(delta));

                    // Tính kích thước thực của phần dữ liệu nén ảnh JPEG phía sau Delta Header
                    int jpgSize = (int)hdr.size - (int)sizeof(delta);

                    // Đẩy dữ liệu ảnh nén và tọa độ cập nhật sang RemoteWindow để vẽ lên màn hình
                    RemoteWindow::UpdateFrame(delta, buf.data() + sizeof(delta), jpgSize);
                }
            }
        }
    }

    // ========================================================================
    // Hàm: ReceiveAudioLoop
    // Mô tả: Vòng lặp nhận dữ liệu kênh Audio (Cổng 8082).
    // Tham số: client - Con trỏ tới đối tượng TcpClient của kênh Audio.
    // ========================================================================
    void ReceiveAudioLoop(TcpClient* client) {
        Protocol::PacketHeader hdr;

        // Vòng lặp liên tục đọc Header của gói tin Audio
        while (g_isRunning && client->ReceiveExact((char*)&hdr, sizeof(hdr))) {
            // Gói âm thanh thường chỉ khoảng vài KB (chuẩn 3840 bytes), tối đa không quá 1MB
            if (hdr.size == 0 || hdr.size > 1024 * 1024) continue;

            // Cấp phát bộ đệm nhận dữ liệu âm thanh
            std::vector<char> buf(hdr.size);
            if (!client->ReceiveExact(buf.data(), (int)hdr.size)) break;

            // Nếu đúng là gói dữ liệu âm thanh chunk
            if (hdr.type == Protocol::MSG_AUDIO_CHUNK) {
                // Đẩy mảng byte âm thanh vào bộ phát AudioPlayer để phát ra loa
                AudioPlayer::PlayChunk(buf.data(), (int)hdr.size);
            }
        }
    }
}

namespace DataReceiver {
    // ========================================================================
    // Hàm: Start
    // Mô tả: Bắt đầu chạy ngầm 3 luồng thu nhận dữ liệu tương ứng 3 kênh.
    // ========================================================================
    void Start(TcpClient* controlClient, TcpClient* videoClient, TcpClient* audioClient) {
        if (g_isRunning) return; // Nếu đã đang chạy thì không khởi chạy lại
        g_isRunning = true;      // Đặt cờ báo hiệu hệ thống đang hoạt động

        // Tạo và chạy luồng nhận dữ liệu điều khiển
        g_controlThread = std::thread(ReceiveControlLoop, controlClient);

        // Tạo và chạy luồng nhận dữ liệu video
        g_videoThread = std::thread(ReceiveVideoLoop, videoClient);

        // Tạo và chạy luồng nhận dữ liệu âm thanh
        g_audioThread = std::thread(ReceiveAudioLoop, audioClient);
    }

    // ========================================================================
    // Hàm: Stop
    // Mô tả: Dừng toàn bộ các luồng thu nhận dữ liệu và đợi các luồng kết thúc.
    // ========================================================================
    void Stop() {
        if (!g_isRunning) return; // Nếu chưa chạy thì không cần dừng
        g_isRunning = false;     // Bật cờ dừng để các vòng lặp while tự thoát

        // Đợi luồng điều khiển kết thúc hoàn toàn
        if (g_controlThread.joinable()) g_controlThread.join();

        // Đợi luồng video kết thúc hoàn toàn
        if (g_videoThread.joinable()) g_videoThread.join();

        // Đợi luồng âm thanh kết thúc hoàn toàn
        if (g_audioThread.joinable()) g_audioThread.join();
    }
}
