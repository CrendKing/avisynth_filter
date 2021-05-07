// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "util.h"


namespace SynthFilter {

auto ConvertWideToUtf8(std::wstring_view wideString) -> std::string {
    const int count = WideCharToMultiByte(CP_UTF8, 0, wideString.data(), static_cast<int>(wideString.size()), nullptr, 0, nullptr, nullptr);
    std::string ret(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wideString.data(), -1, ret.data(), count, nullptr, nullptr);
    return ret;
}

auto ConvertUtf8ToWide(std::string_view utf8String) -> std::wstring {
    const int count = MultiByteToWideChar(CP_UTF8, 0, utf8String.data(), static_cast<int>(utf8String.size()), nullptr, 0);
    std::wstring ret(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8String.data(), -1, ret.data(), count);
    return ret;
}

auto DoubleToString(double d, int precision) -> std::wstring {
    const std::wstring str = std::to_wstring(d);
    return str.substr(0, str.find(L'.') + 1 + precision);
}

// TODO: Replace with std::format()
auto JoinStrings(const std::vector<std::wstring> &input, std::wstring_view delimiter) -> std::wstring {
    return std::accumulate(input.begin(), input.end(), std::wstring(),
                           [&delimiter](std::wstring a, std::wstring_view b) -> std::wstring {
                               return a.append(a.empty() ? L"" : delimiter).append(b);
                           });
}

}
