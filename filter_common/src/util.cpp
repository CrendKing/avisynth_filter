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

auto DoubleToString(double num, int precision) -> std::wstring {
    const std::wstring str = std::to_wstring(num);
    return str.substr(0, str.find(L'.') + 1 + precision);
}

auto JoinStrings(const std::vector<std::wstring> &inputs, std::wstring_view delimiter) -> std::wstring {
    std::wstring ret;

    for (size_t i = 0; i < inputs.size(); ++i) {
        ret.append(i == 0 ? L"" : delimiter).append(inputs[i]);
    }

    return ret;
}

auto ReplaceSubstr(std::string &str, std::string_view from, std::string_view to) -> std::string & {
    if (!from.empty()) {
        size_t startPos = 0;
        while ((startPos = str.find(from, startPos)) != str.npos) {
            str.replace(startPos, from.length(), to);
            startPos += to.length();
        }
    }

    return str;
}

auto CoprimeIntegers(int64_t &a, int64_t &b) -> void {
    if (const int64_t gcd = std::gcd(a, b); gcd > 1) {
        a /= gcd;
        b /= gcd;
    }
}

}
