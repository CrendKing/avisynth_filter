#include "pch.h"
#include "util.h"


namespace AvsFilter {

auto ConvertWideToUtf8(const std::wstring &wideString) -> std::string {
    const int count = WideCharToMultiByte(CP_UTF8, 0, wideString.c_str(), static_cast<int>(wideString.length()), nullptr, 0, nullptr, nullptr);
    std::string ret(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wideString.c_str(), -1, ret.data(), count, nullptr, nullptr);
    return ret;
}

auto ConvertUtf8ToWide(const std::string &utf8String) -> std::wstring {
    const int count = MultiByteToWideChar(CP_UTF8, 0, utf8String.c_str(), static_cast<int>(utf8String.length()), nullptr, 0);
    std::wstring ret(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8String.c_str(), -1, ret.data(), count);
    return ret;
}

auto DoubleToString(double d, int precision) -> std::string {
    const std::string str = std::to_string(d);
    return str.substr(0, str.find('.') + 1 + precision);
}

auto JoinStrings(const std::vector<std::wstring> &input, wchar_t delimiter) -> std::wstring {
    if (input.empty()) {
        return L"";
    }

    return std::accumulate(std::next(input.begin()),
                           input.end(),
                           input[0],
                           [delimiter](const std::wstring &a, const std::wstring &b) -> std::wstring {
                               return a + delimiter + b;
                           });
}

}
