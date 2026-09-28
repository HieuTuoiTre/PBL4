#include "FileWorker.h"
#include "../../Shared/Utils.h"

bool FileWorker::OpenFileForRead(const std::string& filePath){
    std::wstring widePath = Utils::Utf8ToWide(filePath);
    //read file without modifying it + move cursor to end of file to know its size
    fileStream.open(widePath.c_str(), std::ios::binary | std::ios::ate);

    if (fileStream.is_open() == false){
        return false;
    }

    //tellg() point at current cursor position, which is the end of the file 
    //-> file size
    fileSize = fileStream.tellg();
    //move cursor back to beginning
    fileStream.seekg(0, std::ios::beg);

    return true;
}

//return how many bytes have been read from a file
int FileWorker::ReadNextChunk(char* buffer, int maxChunkSize){
    if (fileStream.is_open() == false){
        return 0;   
    }

    fileStream.read(buffer, maxChunkSize);

    return (int)fileStream.gcount();
}

bool FileWorker::IsEOF(){
    return FileWorker::fileStream.eof();
}

void FileWorker::CloseFile(){
    if (fileStream.is_open() == true){
        fileStream.close();
    }
    fileSize = 0;
}
