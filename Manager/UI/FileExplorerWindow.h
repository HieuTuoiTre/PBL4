// ============================================================================
// File: FileExplorerWindow.h
// Mô tả: Khai báo giao diện cửa sổ FileExplorerWindow (Trình duyệt và tải tệp tin từ xa).
// Chịu trách nhiệm:
//   - Khởi tạo giao diện điều hướng thư mục, hiển thị danh sách ổ đĩa và tệp tin trên máy Client.
//   - Xử lý các phản hồi từ Client: danh sách ổ đĩa (Drive List), danh sách thư mục (Directory List).
//   - Quản lý quá trình tải tệp tin về máy Manager: nhận metadata thông tin file, ghi từng chunk, báo kết thúc.
// ============================================================================

#pragma once // Chỉ thị tiền xử lý ngăn chặn nạp trùng lặp header

// Nạp các lớp kết nối TCP và các định nghĩa gói tin giao thức
#include "../../Shared/Network.h"
#include "../../Shared/Protocol.h"

// Nạp thư viện chuẩn Windows API và chuỗi ký tự C++
#include <windows.h>
#include <string>

namespace FileExplorerWindow {
    // Khởi tạo và hiển thị cửa sổ duyệt tập tin từ xa
    // - hParent: Handle của cửa sổ cha (RemoteWindow)
    // - client: Con trỏ kênh TCP Điều khiển để gửi yêu cầu duyệt ổ đĩa / thư mục / tải file
    void Show(HWND hParent, TcpClient* client);

    // Xử lý gói tin phản hồi danh sách ổ đĩa (MSG_DRIVE_LIST_RESPONSE) từ Client
    void OnDriveListResponse(const std::string& content);

    // Xử lý gói tin phản hồi danh sách thư mục/tập tin (MSG_DIR_RESPONSE) từ Client
    void OnDirResponse(const std::string& content);

    // Bắt đầu quá trình nhận file: khởi tạo tệp tin đích trên đĩa và hiển thị tiến trình tải
    void OnFileInfo(const Protocol::FileInfoPayload& info);

    // Nhận một khối dữ liệu nhị phân (chunk) của file và ghi tiếp vào tệp tin trên đĩa
    void OnFileChunk(const char* data, int size);

    // Báo hiệu quá trình truyền tải file đã hoàn tất và đóng tệp tin an toàn
    void OnFileEnd();
}
