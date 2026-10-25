// ============================================================================
// File: FileManager.h
// Mô tả: Khai báo lớp FileManager - Phụ trách nhận và lưu các tệp tin tải về máy Manager.
// Chịu trách nhiệm:
//   - Mở tệp tin trên đĩa cục bộ ở chế độ nhị phân (Binary Mode) để chuẩn bị ghi.
//   - Ghi từng khối dữ liệu nhị phân (chunks) nhận được từ mạng vào tệp tin.
//   - Đóng tệp tin an toàn khi quá trình truyền tải hoàn tất.
// ============================================================================

#pragma once

#include <string>  // Thư viện chuỗi ký tự std::string
#include <fstream> // Thư viện luồng nhập xuất tệp tin std::ofstream

class FileManager {
private:
    // Luồng ghi file ra ổ đĩa (output file stream)
    std::ofstream fileStream;

public:
    // Mở file tại đường dẫn savePath để chuẩn bị ghi đè (hỗ trợ đường dẫn Unicode tiếng Việt)
    bool OpenFileForWrite(const std::string& savePath);

    // Ghi một khối dữ liệu (chunk) có độ dài length vào file đang mở
    bool WriteChunk(const char* data, int length);

    // Đóng file và giải phóng tài nguyên hệ điều hành
    void CloseFile();
};
