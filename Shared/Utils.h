#pragma once

#include <string>

class Utils{
    public:
    Utils() = delete;
    //vietnamese chars require widestring (2 bytes) to be read correctly
    //but the internet transfer byte-by-byte
    //conversion is needed to vn chars to be read be presented correctly
    //and because win32 api work on unicode which is widestring
    static std::wstring Utf8ToWide(const std::string& utf8_str);
    static std::string WideToUtf8(const std::wstring& wide_str);
};