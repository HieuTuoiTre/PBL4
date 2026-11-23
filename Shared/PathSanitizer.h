#pragma once

#include <string>
#include <filesystem>

namespace PathSanitizer {
    /**
     * @brief Kiểm tra xem một chuỗi relative path có an toàn hay không:
     * - Không được rỗng
     * - Không chứa thành phần '..' hoặc '.'
     * - Không được là absolute path (không bắt đầu bằng /, \, hoặc C:)
     * - Không chứa ký tự điều khiển nguy hiểm (như \0)
     */
    bool IsSafeRelativePath(const std::string& relativePath);

    /**
     * @brief Kiểm tra xem candidatePath có nằm gọn hoàn toàn bên trong baseDirectory hay không.
     * Sử dụng weakly_canonical và so sánh lexical prefix để chống hoàn toàn Path Traversal.
     */
    bool IsPathContained(const std::filesystem::path& baseDirectory, const std::filesystem::path& candidatePath);

    /**
     * @brief Chuẩn hóa và ghép an toàn baseDirectory + relativePath thành đường dẫn tuyệt đối.
     * @param baseDir Thư mục gốc được phép (ví dụ: "downloads" hoặc uploadTargetDir)
     * @param relativePath Đường dẫn tương đối từ mạng (ví dụ: "docs/readme.txt")
     * @param outSafePath Biến nhận đường dẫn UTF-8 chuẩn hóa an toàn
     * @return true nếu an toàn và nằm gọn trong baseDir, false nếu phát hiện path traversal hoặc đường dẫn không hợp lệ.
     */
    bool ResolveSafePath(const std::string& baseDir, const std::string& relativePath, std::string& outSafePath);

    /**
     * @brief Lấy tên file an toàn từ một chuỗi (chỉ giữ lại filename, loại bỏ mọi thư mục/traversal).
     */
    std::string SanitizeFileName(const std::string& rawFileName);
}
