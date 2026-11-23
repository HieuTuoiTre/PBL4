#include "FileManager.h"
#include "../../Shared/Utils.h"
#include "../../Shared/CRC32.h"
#include <filesystem>

FileManager::FileManager()
    : isFileOpen(false), runningCrc(0), expectedCrc(0), bytesReceived(0), totalBytesExpected(0) {
}

FileManager::~FileManager() {
    if (isFileOpen) {
        AbortAndCleanup();
    }
}

bool FileManager::OpenFileForWrite(const std::string& savePath) {
    return OpenForWrite(savePath, 0, 0);
}

bool FileManager::OpenForWrite(const std::string& finalPath, uint64_t fileSize, uint32_t expectedCrc32) {
    if (isFileOpen) {
        AbortAndCleanup();
    }

    currentFinalPath = finalPath;
    currentPartPath = finalPath + ".part";
    runningCrc = 0;
    expectedCrc = expectedCrc32;
    bytesReceived = 0;
    totalBytesExpected = fileSize;

    try {
        std::filesystem::path fp = std::filesystem::u8path(finalPath);
        if (fp.has_parent_path()) {
            std::filesystem::create_directories(fp.parent_path());
        }
    } catch (...) {}

    std::wstring widePart = Utils::Utf8ToWide(currentPartPath);
    fileStream.open(widePart.c_str(), std::ios::binary | std::ios::trunc);
    isFileOpen = fileStream.is_open();
    return isFileOpen;
}

bool FileManager::WriteChunk(const char* data, int length) {
    if (!isFileOpen || !data || length <= 0) {
        return false;
    }

    fileStream.write(data, length);
    if (!fileStream.good()) {
        return false;
    }

    runningCrc = CRC32::Update(runningCrc, data, static_cast<size_t>(length));
    bytesReceived += static_cast<uint64_t>(length);
    return true;
}

bool FileManager::FinalizeFile(bool* outCrcValid) {
    if (!isFileOpen) {
        if (outCrcValid) *outCrcValid = false;
        return false;
    }

    fileStream.flush();
    fileStream.close();
    isFileOpen = false;

    bool match = (expectedCrc == 0 || runningCrc == expectedCrc);
    if (outCrcValid) {
        *outCrcValid = match;
    }

    if (match) {
        try {
            std::filesystem::path partPath = std::filesystem::u8path(currentPartPath);
            std::filesystem::path finalPath = std::filesystem::u8path(currentFinalPath);
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
        // CRC mismatch: remove .part file immediately to prevent corrupt files
        AbortAndCleanup();
        return false;
    }
}

void FileManager::AbortAndCleanup() {
    if (fileStream.is_open()) {
        fileStream.close();
    }
    isFileOpen = false;

    if (!currentPartPath.empty()) {
        try {
            std::filesystem::path partPath = std::filesystem::u8path(currentPartPath);
            std::error_code ec;
            if (std::filesystem::exists(partPath, ec)) {
                std::filesystem::remove(partPath, ec);
            }
        } catch (...) {}
    }
}

void FileManager::CloseFile() {
    FinalizeFile(nullptr);
}
