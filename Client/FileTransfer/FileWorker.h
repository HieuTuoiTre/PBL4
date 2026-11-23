#pragma once

#include <fstream>
#include <string>
#include <cstdint>
#include <vector>

class FileWorker {
private:
    std::ifstream fileReadStream;
    std::ofstream fileWriteStream;
    uint64_t fileSize = 0;

    std::string currentWriteFinal;
    std::string currentWritePart;
    uint32_t writeRunningCrc = 0;
    uint32_t writeExpectedCrc = 0;
    uint64_t writeBytesReceived = 0;
    uint64_t writeTotalExpected = 0;
    bool isWriting = false;

public:
    struct TransferItem {
        std::string relativePath;
        std::string fullPath;
        uint64_t fileSize;
        uint32_t crc32;
        bool isDirectory;
    };

    FileWorker();
    ~FileWorker();

    // Read interface
    bool OpenFileForRead(const std::string& filePath);
    int ReadNextChunk(char* buffer, int maxChunkSize);
    bool IsEOF();
    void CloseFile();
    uint64_t GetFileSize() const { return fileSize; }

    // Write interface (for receiving uploads)
    bool OpenFileForWrite(const std::string& savePath, uint64_t expectedSize = 0, uint32_t expectedCrc = 0);
    bool WriteChunk(const char* data, int length);
    bool FinalizeWrite(bool* outCrcValid = nullptr);
    void AbortWrite();

    // Drive & directory browsing
    static std::vector<std::string> GetDrives();
    static std::string GetDirectoryContent(const std::string& path);

    // CRC32 calculation & recursive directory scan
    static uint32_t CalculateFileCrc32(const std::string& filePath);
    static std::vector<TransferItem> ScanDirectoryTree(const std::string& rootPath);
};