#include "PathSanitizer.h"
#include <algorithm>
#include <cctype>

namespace {
    // Kiểm tra tên thiết bị đặc biệt của Windows (CON, PRN, AUX, NUL, COM1..9, LPT1..9)
    bool IsWindowsReservedName(const std::string& component) {
        if (component.empty()) return false;
        size_t dotPos = component.find('.');
        std::string stem = (dotPos == std::string::npos) ? component : component.substr(0, dotPos);
        for (char& ch : stem) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") return true;
        if (stem.size() == 4 && (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0)) {
            if (stem[3] >= '1' && stem[3] <= '9') return true;
        }
        return false;
    }

    // Helper so sánh chuỗi path component không phân biệt hoa thường trên Windows
    bool EqualPathComponent(const std::filesystem::path& a, const std::filesystem::path& b) {
#if defined(_WIN32)
        std::string s1 = a.u8string();
        std::string s2 = b.u8string();
        if (s1.size() != s2.size()) return false;
        for (size_t i = 0; i < s1.size(); ++i) {
            if (std::tolower((unsigned char)s1[i]) != std::tolower((unsigned char)s2[i])) {
                return false;
            }
        }
        return true;
#else
        return a == b;
#endif
    }
}

namespace PathSanitizer {

    bool IsSafeRelativePath(const std::string& relativePath) {
        if (relativePath.empty()) return false;

        // Từ chối ký tự null hoặc ký tự không in được nguy hiểm
        for (char c : relativePath) {
            if (c == '\0' || c == '\r' || c == '\n') return false;
        }

        // Thay thế ký tự phân cách ngược '\' thành '/'
        std::string normalized = relativePath;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');

        // Không được bắt đầu bằng '/' hoặc chứa '//'
        if (normalized.front() == '/') return false;

        // Không được chứa drive letter như "C:"
        if (normalized.size() >= 2 && normalized[1] == ':') return false;

        try {
            std::filesystem::path p = std::filesystem::u8path(normalized);

            // Không được là absolute path
            if (p.is_absolute() || p.has_root_name() || p.has_root_directory()) {
                return false;
            }

            // Kiểm tra từng thành phần path
            bool hasValidComponent = false;
            for (const auto& part : p) {
                std::string partStr = part.u8string();
                if (partStr.empty()) continue;

                // Cấm tuyệt đối '..' (traversal lên thư mục cha)
                if (partStr == ".." || partStr == ".") {
                    return false;
                }

                // Cấm phần tử kết thúc bằng dấu hai chấm (NTFS Alternate Data Streams nguy hiểm)
                if (partStr.find(':') != std::string::npos) {
                    return false;
                }

                // Cấm khoảng trắng hoặc dấu chấm ở cuối thành phần và tên thiết bị đặc biệt của Windows
                if (partStr.back() == ' ' || partStr.back() == '.') {
                    return false;
                }
                if (IsWindowsReservedName(partStr)) {
                    return false;
                }

                hasValidComponent = true;
            }

            return hasValidComponent;
        } catch (...) {
            return false;
        }
    }

    bool IsPathContained(const std::filesystem::path& baseDirectory, const std::filesystem::path& candidatePath) {
        try {
            std::error_code ec;
            std::filesystem::path canonicalBase = std::filesystem::weakly_canonical(baseDirectory, ec);
            if (ec) {
                canonicalBase = std::filesystem::absolute(baseDirectory, ec);
            }

            std::filesystem::path canonicalCand = std::filesystem::weakly_canonical(candidatePath, ec);
            if (ec) {
                canonicalCand = std::filesystem::absolute(candidatePath, ec);
            }

            auto itBase = canonicalBase.begin();
            auto itCand = canonicalCand.begin();

            while (itBase != canonicalBase.end() && itCand != canonicalCand.end()) {
                if (!EqualPathComponent(*itBase, *itCand)) {
                    return false;
                }
                ++itBase;
                ++itCand;
            }

            // candidate phải nằm bên dưới thư mục base (không cho phép trùng chính base)
            return (itBase == canonicalBase.end() && itCand != canonicalCand.end());
        } catch (...) {
            return false;
        }
    }

    bool ResolveSafePath(const std::string& baseDir, const std::string& relativePath, std::string& outSafePath) {
        outSafePath.clear();

        if (baseDir.empty() || !IsSafeRelativePath(relativePath)) {
            return false;
        }

        try {
            std::error_code ec;
            std::filesystem::path basePath = std::filesystem::u8path(baseDir);

            // Đảm bảo thư mục base được tạo nếu chưa có
            std::filesystem::create_directories(basePath, ec);

            std::filesystem::path normRel = std::filesystem::u8path(relativePath);
            std::filesystem::path candidate = basePath / normRel;

            // Kiểm tra containment tuyệt đối
            if (!IsPathContained(basePath, candidate)) {
                return false;
            }

            std::filesystem::path canonicalCand = std::filesystem::weakly_canonical(candidate, ec);
            if (ec) {
                canonicalCand = std::filesystem::absolute(candidate, ec);
            }

            outSafePath = canonicalCand.u8string();
            return true;
        } catch (...) {
            return false;
        }
    }

    std::string SanitizeFileName(const std::string& rawFileName) {
        if (rawFileName.empty()) return "unnamed_file";

        std::string name = rawFileName;
        size_t slashPos = name.find_last_of("\\/");
        if (slashPos != std::string::npos) {
            name = name.substr(slashPos + 1);
        }

        // Bỏ ký tự ':' nếu có (chống NTFS stream)
        size_t colonPos = name.find(':');
        if (colonPos != std::string::npos) {
            name = name.substr(0, colonPos);
        }

        if (name.empty() || name == "." || name == "..") {
            return "unnamed_file";
        }
        return name;
    }
}
