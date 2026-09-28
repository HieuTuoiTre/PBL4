#include "Utils.h"
#include <windows.h>

std::wstring Utils::Utf8ToWide(const std::string& utf8_str){
    if (utf8_str.empty() == true){
        return std::wstring();
    }

    int size = MultiByteToWideChar(CP_UTF8, 0, &utf8_str[0], (int)utf8_str.size(), NULL, 0);
    std::wstring convertedWide(size, 0);

    // Bỏ từ khóa 'int' ở đây để tái sử dụng biến size đã khai báo ở trên
    size = MultiByteToWideChar(CP_UTF8, 0, &utf8_str[0], (int)utf8_str.size(), &convertedWide[0], size);
    return convertedWide;
}

std::string Utils::WideToUtf8(const std::wstring& wide_str){
    if (wide_str.empty() == true){
        return std::string();
    }

    int size = WideCharToMultiByte(CP_UTF8, 0, &wide_str[0], (int)wide_str.size(), NULL, 0, NULL, NULL);
    std::string convertedString(size, 0);

    // Bỏ từ khóa 'int' ở đây để tái sử dụng biến size đã khai báo ở trên
    size = WideCharToMultiByte(CP_UTF8, 0, &wide_str[0], (int)wide_str.size(), &convertedString[0], size, NULL, NULL);
    return convertedString;   
}