#include "FileWorker.h"
#include "../../Shared/Utils.h"
#include <windows.h>
#include <filesystem>
#include <sstream>

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

// ==========================================
// BỔ SUNG LOGIC DUYỆT Ổ ĐĨA & THƯ MỤC
// ==========================================
std::vector<std::string> FileWorker::GetDrives() {
    std::vector<std::string> drives;
    char driveBuffer[256] = {0};
    
    // Gọi API của Windows lấy chuỗi chứa các ổ đĩa (VD: "C:\<NULL>D:\<NULL>")
    DWORD size = GetLogicalDriveStringsA(sizeof(driveBuffer), driveBuffer);
    
    if (size > 0 && size <= sizeof(driveBuffer)) {
        char* drive = driveBuffer;
        while (*drive) { 
            drives.push_back(std::string(drive));
            drive += strlen(drive) + 1; // Nhảy con trỏ tới ổ đĩa tiếp theo
        }
    }
    return drives;
}

std::string FileWorker::GetDirectoryContent(const std::string& path) {
    std::stringstream ss;
    
    try {
        // Chuyển đường dẫn sang WideString để đọc được file có tiếng Việt
        std::wstring wPath = Utils::Utf8ToWide(path);
        
        if (!std::filesystem::exists(wPath) || !std::filesystem::is_directory(wPath)) {
            return "ERROR: Thu muc khong ton tai hoac sai duong dan!";
        }

        // Duyệt qua từng file/folder bên trong
        for (const auto& entry : std::filesystem::directory_iterator(wPath)) {
            std::wstring wFileName = entry.path().filename().wstring();
            std::string utf8FileName = Utils::WideToUtf8(wFileName);

            if (entry.is_directory()) {
                ss << "[DIR] " << utf8FileName << "\n";
            } else if (entry.is_regular_file()) {
                std::error_code ec;
                uintmax_t fSize = std::filesystem::file_size(entry, ec);
                ss << "[FILE] " << utf8FileName << " | " << fSize << " bytes\n";
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        return "ERROR: Khong co quyen truy cap (Access Denied)!";
    }

    return ss.str();
}
