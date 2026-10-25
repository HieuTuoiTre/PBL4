// ============================================================================
// File: FileManger.cpp
// Mô tả: Hiện thực các phương thức của lớp FileManager.
// ============================================================================

// Nạp file header khai báo lớp FileManager
#include "FileManager.h"

// Nạp module tiện ích chuyển đổi bảng mã Utf8 sang WideString
#include "../../Shared/Utils.h"

// ============================================================================
// Phương thức: OpenFileForWrite
// Mục đích: Mở tệp tin tại đường dẫn đích để chuẩn bị ghi dữ liệu tải về.
// Tham số: savePath - Đường dẫn lưu file trên máy tính (chuỗi UTF-8).
// Trả về: true nếu tạo/mở file thành công; false nếu thất bại (VD: Không có quyền ghi).
// ============================================================================
bool FileManager::OpenFileForWrite(const std::string& savePath) {
    // Nếu luồng file trước đó vẫn đang mở thì đóng lại trước để tránh xung đột
    if (fileStream.is_open()) {
        fileStream.close();
    }

    // Chuyển đường dẫn từ chuỗi UTF-8 sang chuỗi Unicode UTF-16 (std::wstring)
    // để mở được các thư mục hoặc tên tệp tin có dấu tiếng Việt trên Windows
    std::wstring widePath = Utils::Utf8ToWide(savePath);

    // Mở file ở chế độ:
    // - std::ios::binary: Chế độ nhị phân (giữ nguyên từng byte, không tự động biến đổi \n thành \r\n)
    // - std::ios::trunc: Nếu file đã tồn tại thì xóa sạch nội dung cũ để ghi lại từ đầu
    fileStream.open(widePath.c_str(), std::ios::binary | std::ios::trunc);

    // Trả về kết quả kiểm tra xem file đã được mở thành công hay chưa
    return fileStream.is_open();
}

// ============================================================================
// Phương thức: WriteChunk
// Mục đích: Ghi một mảng byte dữ liệu nhị phân vào tệp tin đang mở.
// Tham số:
//   - data: Con trỏ trỏ tới vùng nhớ đệm chứa dữ liệu cần ghi
//   - length: Số lượng byte dữ liệu trong khối chunk này
// Trả về: true nếu ghi thành công và trạng thái luồng tốt; false nếu thất bại.
// ============================================================================
bool FileManager::WriteChunk(const char* data, int length) {
    // Nếu tệp tin chưa được mở thì không thể ghi dữ liệu
    if (fileStream.is_open() == false) {
        return false;
    }

    // Ghi chính xác length bytes từ vùng nhớ data vào file
    fileStream.write(data, length);

    // Trả về true nếu quá trình ghi không gặp lỗi I/O (ổ đĩa đầy, mất quyền ghi, v.v.)
    return fileStream.good();
}

// ============================================================================
// Phương thức: CloseFile
// Mục đích: Đóng luồng tệp tin, đẩy toàn bộ dữ liệu từ bộ đệm xuống đĩa vật lý.
// ============================================================================
void FileManager::CloseFile() {
    // Nếu luồng đang mở thì thực hiện đóng file an toàn
    if (fileStream.is_open() == true) {
        fileStream.close();
    }
}
