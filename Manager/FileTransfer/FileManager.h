#pragma once

#include <string>
#include <fstream>
#include <cstdint>

class FileManager {
private:
    std::ofstream fileStream;
    std::string currentFinalPath;
    std::string currentPartPath;
    uint32_t runningCrc = 0;
    uint32_t expectedCrc = 0;
    uint64_t bytesReceived = 0;
    uint64_t totalBytesExpected = 0;
    bool isFileOpen = false;

public:
    FileManager();
    ~FileManager();

    // Legacy compatibility: OpenFileForWrite opens savePath.part
    bool OpenFileForWrite(const std::string& savePath);

    // Advanced: opens finalPath via temporary .part file, sets expected file size & expected CRC32
    bool OpenForWrite(const std::string& finalPath, uint64_t fileSize, uint32_t expectedCrc32);

    // Writes received chunk to .part file and updates running CRC32
    bool WriteChunk(const char* data, int length);

    // Closes .part file, verifies CRC32 (if expectedCrc != 0), renames .part to finalPath if valid, or removes .part if invalid.
    // Returns true if write succeeded and CRC matched.
    bool FinalizeFile(bool* outCrcValid = nullptr);

    // Closes file stream and deletes .part file immediately (used on cancel or error)
    void AbortAndCleanup();

    // Legacy compatibility: calls FinalizeFile()
    void CloseFile();

    uint32_t GetRunningCrc() const { return runningCrc; }
    uint32_t GetExpectedCrc() const { return expectedCrc; }
    uint64_t GetBytesReceived() const { return bytesReceived; }
    uint64_t GetTotalBytesExpected() const { return totalBytesExpected; }
    bool IsOpen() const { return isFileOpen; }
    const std::string& GetFinalPath() const { return currentFinalPath; }
};
