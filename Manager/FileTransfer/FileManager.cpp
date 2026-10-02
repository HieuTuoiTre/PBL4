#include "FileManager.h"
#include "../../Shared/Utils.h"

FileManager::~FileManager() {
    CloseFile();
}

bool FileManager::OpenFileForWrite(const std::string& savePath) {
    CloseFile();
    std::wstring widePath = Utils::Utf8ToWide(savePath);
    fileStream.open(widePath.c_str(), std::ios::binary | std::ios::ate | std::ios::trunc);
    if (!fileStream.is_open()) {
        isOpen = false;
        return false;
    }
    currentPath = savePath;
    isOpen = true;
    return true;
}

bool FileManager::WriteChunk(const char* data, int length) {
    if (!isOpen || !data || length <= 0) {
        return false;
    }
    fileStream.write(data, length);
    return fileStream.good();
}

void FileManager::CloseFile() {
    if (isOpen) {
        fileStream.flush();
        fileStream.close();
        isOpen = false;
        currentPath.clear();
    }
}

