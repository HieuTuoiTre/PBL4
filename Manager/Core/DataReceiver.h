// ============================================================================
// File: DataReceiver.h
// Mô tả: Khai báo giao diện của module DataReceiver (Bộ thu nhận dữ liệu ngầm).
// Chịu trách nhiệm:
//   - Khởi chạy và quản lý 3 luồng mạng ngầm tương ứng với 3 kết nối TCP.
//   - Nhận diện các loại gói tin và chuyển tiếp dữ liệu đến đúng module UI / Media.
// ============================================================================

#pragma once // Chỉ thị tiền xử lý đảm bảo file header chỉ được nạp một lần duy nhất

// Nạp định nghĩa lớp TcpClient từ tầng mạng dùng chung
#include "../../Shared/Network.h"

namespace DataReceiver {
    // Khởi chạy 3 luồng nhận dữ liệu ngầm cho kênh Điều khiển, Video màn hình và Âm thanh
    void Start(TcpClient* controlClient, TcpClient* videoClient, TcpClient* audioClient);

    // Dừng toàn bộ các luồng nhận dữ liệu và đợi các luồng kết thúc an toàn
    void Stop();
}
