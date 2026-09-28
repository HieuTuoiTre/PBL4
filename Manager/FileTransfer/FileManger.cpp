#include "FileManager.h"
#include "../../Shared/Utils.h"

bool FileManager::OpenFileForWrite(const std::string& savePath){
    std::wstring widePath = Utils::Utf8ToWide(savePath);

    fileStream.open(widePath.c_str(), std::ios::binary | std::ios::ate | std::ios::trunc);

    if (fileStream.is_open() == false){
        return false;
    }
    return true;
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