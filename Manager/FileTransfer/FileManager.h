#pragma once

#include <string>
#include <fstream>

class FileManager{
    private:
        std::ofstream fileStream;
    
    public:
        bool OpenFileForWrite(const std::string& savePath);
        bool WriteChunk(const char* data, int length);
        void CloseFile();
};