#pragma once

#include <fstream>
#include <string>
#include <cstdint>
#include <vector>

class FileWorker{
    private:
        std::ifstream fileStream;
        uint64_t fileSize = 0;

    public:
        bool OpenFileForRead(const std::string& filePath);
        int ReadNextChunk(char* buffer, int maxChunkSize);
        bool IsEOF();
        void CloseFile();
        uint64_t GetFileSize() const {return fileSize; }

        // BỔ SUNG 2 HÀM MỚI NÀY:
        static std::vector<std::string> GetDrives();
        static std::string GetDirectoryContent(const std::string& path);
};