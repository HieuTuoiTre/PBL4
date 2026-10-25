#include "FileManager.h"
#include "../../Shared/Utils.h"

bool FileManager::OpenFileForWrite(const std::string& savePath){
    if (fileStream.is_open()) {
        fileStream.close();
    }
    std::wstring widePath = Utils::Utf8ToWide(savePath);

    fileStream.open(widePath.c_str(), std::ios::binary | std::ios::trunc);

    return fileStream.is_open();
}

bool FileManager::WriteChunk(const char* data, int length){
    if (fileStream.is_open() == false){
        return false;
    }

    fileStream.write(data, length);

    return fileStream.good();
}

void FileManager::CloseFile(){
    if (fileStream.is_open() == true){
        fileStream.close();
    }
}
