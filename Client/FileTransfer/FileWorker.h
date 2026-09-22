#pragma once

#include <fstream>
#include <string>
#include <cstdint>

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
};