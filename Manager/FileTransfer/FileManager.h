#pragma once

#include <string>
#include <fstream>

class FileManager {
private:
    std::ofstream fileStream;
    std::string currentPath;
    bool isOpen = false;

public:
    FileManager() = default;
    ~FileManager();

    bool OpenFileForWrite(const std::string& savePath);
    bool WriteChunk(const char* data, int length);
    void CloseFile();
    bool IsOpen() const { return isOpen; }
};