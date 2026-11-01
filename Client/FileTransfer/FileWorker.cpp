#include "FileWorker.h"
#include "../../Shared/Utils.h"
#include "../../Shared/CRC32.h"
#include <windows.h>
#include <filesystem>
#include <sstream>

FileWorker::FileWorker()
    : fileSize(0), writeRunningCrc(0), writeExpectedCrc(0),
      writeBytesReceived(0), writeTotalExpected(0), isWriting(false) {
}

FileWorker::~FileWorker() {
    CloseFile();
    if (isWriting) {
        AbortWrite();
    }
}

bool FileWorker::OpenFileForRead(const std::string& filePath) {
    CloseFile();
    std::wstring widePath = Utils::Utf8ToWide(filePath);
    fileReadStream.open(widePath.c_str(), std::ios::binary | std::ios::ate);

    if (!fileReadStream.is_open()) {
        return false;
    }

    fileSize = static_cast<uint64_t>(fileReadStream.tellg());
    fileReadStream.seekg(0, std::ios::beg);
    return true;
}

int FileWorker::ReadNextChunk(char* buffer, int maxChunkSize) {
    if (!fileReadStream.is_open() || !buffer || maxChunkSize <= 0) {
        return 0;   
    }

    fileReadStream.read(buffer, maxChunkSize);
    return static_cast<int>(fileReadStream.gcount());
}

bool FileWorker::IsEOF() {
    return !fileReadStream.is_open() || fileReadStream.eof() || fileReadStream.peek() == EOF;
}

void FileWorker::CloseFile() {
    if (fileReadStream.is_open()) {
        fileReadStream.close();
    }
    fileSize = 0;
}

bool FileWorker::OpenFileForWrite(const std::string& savePath, uint64_t expectedSize, uint32_t expectedCrc) {
    if (isWriting) {
        AbortWrite();
    }

    currentWriteFinal = savePath;
    currentWritePart = savePath + ".part";
    writeRunningCrc = 0;
    writeExpectedCrc = expectedCrc;
    writeBytesReceived = 0;
    writeTotalExpected = expectedSize;

    try {
        std::filesystem::path fp = std::filesystem::u8path(savePath);
        if (fp.has_parent_path()) {
            std::filesystem::create_directories(fp.parent_path());
        }
    } catch (...) {}

    std::wstring widePart = Utils::Utf8ToWide(currentWritePart);
    fileWriteStream.open(widePart.c_str(), std::ios::binary | std::ios::trunc);
    isWriting = fileWriteStream.is_open();
    return isWriting;
}

bool FileWorker::WriteChunk(const char* data, int length) {
    if (!isWriting || !data || length <= 0) {
        return false;
    }

    fileWriteStream.write(data, length);
    if (!fileWriteStream.good()) {
        return false;
    }

    writeRunningCrc = CRC32::Update(writeRunningCrc, data, static_cast<size_t>(length));
    writeBytesReceived += static_cast<uint64_t>(length);
    return true;
}

bool FileWorker::FinalizeWrite(bool* outCrcValid) {
    if (!isWriting) {
        if (outCrcValid) *outCrcValid = false;
        return false;
    }

    fileWriteStream.flush();
    fileWriteStream.close();
    isWriting = false;

    bool match = (writeExpectedCrc == 0 || writeRunningCrc == writeExpectedCrc);
    if (outCrcValid) {
        *outCrcValid = match;
    }

    if (match) {
        try {
            std::filesystem::path partPath = std::filesystem::u8path(currentWritePart);
            std::filesystem::path finalPath = std::filesystem::u8path(currentWriteFinal);
            std::error_code ec;
            if (std::filesystem::exists(finalPath, ec)) {
                std::filesystem::remove(finalPath, ec);
            }
            std::filesystem::rename(partPath, finalPath, ec);
            if (ec) {
                std::filesystem::copy_file(partPath, finalPath, std::filesystem::copy_options::overwrite_existing, ec);
                std::filesystem::remove(partPath, ec);
            }
            return !ec;
        } catch (...) {
            return false;
        }
    } else {
        AbortWrite();
        return false;
    }
}

void FileWorker::AbortWrite() {
    if (fileWriteStream.is_open()) {
        fileWriteStream.close();
    }
    isWriting = false;

    if (!currentWritePart.empty()) {
        try {
            std::filesystem::path partPath = std::filesystem::u8path(currentWritePart);
            std::error_code ec;
            if (std::filesystem::exists(partPath, ec)) {
                std::filesystem::remove(partPath, ec);
            }
        } catch (...) {}
    }
}

// ==========================================
// DUYỆT Ổ ĐĨA & THƯ MỤC
// ==========================================
std::vector<std::string> FileWorker::GetDrives() {
    std::vector<std::string> drives;
    char driveBuffer[256] = {0};
    
    DWORD size = GetLogicalDriveStringsA(sizeof(driveBuffer), driveBuffer);
    
    if (size > 0 && size <= sizeof(driveBuffer)) {
        char* drive = driveBuffer;
        while (*drive) { 
            drives.push_back(std::string(drive));
            drive += strlen(drive) + 1;
        }
    }
    return drives;
}

std::string FileWorker::GetDirectoryContent(const std::string& path) {
    std::stringstream ss;
    
    try {
        std::wstring wPath = Utils::Utf8ToWide(path);
        
        if (!std::filesystem::exists(wPath) || !std::filesystem::is_directory(wPath)) {
            return "ERROR: Thu muc khong ton tai hoac sai duong dan!";
        }

        for (const auto& entry : std::filesystem::directory_iterator(wPath)) {
            std::wstring wFileName = entry.path().filename().wstring();
            std::string utf8FileName = Utils::WideToUtf8(wFileName);

            if (entry.is_directory()) {
                ss << "[DIR] " << utf8FileName << "\n";
            } else if (entry.is_regular_file()) {
                std::error_code ec;
                uintmax_t fSize = std::filesystem::file_size(entry, ec);
                ss << "[FILE] " << utf8FileName << " | " << fSize << " bytes\n";
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        return "ERROR: Khong co quyen truy cap (Access Denied)!";
    }

    return ss.str();
}

uint32_t FileWorker::CalculateFileCrc32(const std::string& filePath) {
    std::wstring widePath = Utils::Utf8ToWide(filePath);
    std::ifstream in(widePath.c_str(), std::ios::binary);
    if (!in.is_open()) return 0;

    std::vector<char> buf(64 * 1024);
    uint32_t crc = 0;
    while (in.good()) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        std::streamsize bytes = in.gcount();
        if (bytes > 0) {
            crc = CRC32::Update(crc, buf.data(), static_cast<size_t>(bytes));
        }
    }
    return crc;
}

std::vector<FileWorker::TransferItem> FileWorker::ScanDirectoryTree(const std::string& rootPath) {
    std::vector<TransferItem> items;
    try {
        std::filesystem::path rootP = std::filesystem::u8path(rootPath);
        if (!std::filesystem::exists(rootP)) return items;

        if (std::filesystem::is_regular_file(rootP)) {
            TransferItem item;
            item.relativePath = rootP.filename().u8string();
            item.fullPath = rootP.u8string();
            std::error_code ec;
            item.fileSize = std::filesystem::file_size(rootP, ec);
            item.crc32 = CalculateFileCrc32(item.fullPath);
            item.isDirectory = false;
            items.push_back(item);
            return items;
        }

        std::filesystem::path canonicalRoot = std::filesystem::canonical(rootP);

        for (const auto& entry : std::filesystem::recursive_directory_iterator(canonicalRoot, std::filesystem::directory_options::skip_permission_denied)) {
            try {
                auto rel = std::filesystem::relative(entry.path(), canonicalRoot);
                std::string relStr = rel.u8string();
                for (char& c : relStr) {
                    if (c == '\\') c = '/';
                }

                TransferItem item;
                item.relativePath = relStr;
                item.fullPath = entry.path().u8string();
                if (entry.is_directory()) {
                    item.isDirectory = true;
                    item.fileSize = 0;
                    item.crc32 = 0;
                } else if (entry.is_regular_file()) {
                    item.isDirectory = false;
                    std::error_code ec;
                    item.fileSize = std::filesystem::file_size(entry.path(), ec);
                    item.crc32 = CalculateFileCrc32(item.fullPath);
                } else {
                    continue;
                }
                items.push_back(item);
            } catch (...) {}
        }
    } catch (...) {}
    return items;
}
