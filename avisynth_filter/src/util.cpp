#include "pch.h"
#include "util.h"


namespace AvsFilter {

auto ReplaceSubstring(std::string &str, const char *target, const char *rep) -> void {
    const size_t repLen = strlen(target);
    size_t index = 0;

    while (true) {
        index = str.find(target, index);
        if (index == std::string::npos) {
            break;
        }

        str.replace(index, repLen, rep);
        index += repLen;
    }
}

auto ConvertWideToUtf8(const std::wstring &wstr) -> std::string {
    const int count = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.length()), nullptr, 0, nullptr, nullptr);
    std::string str(count, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], count, nullptr, nullptr);
    return str;
}

auto ConvertUtf8ToWide(const std::string &str) -> std::wstring {
    const int count = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), nullptr, 0);
    std::wstring wstr(count, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), &wstr[0], count);
    return wstr;
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
                           [delimiter](const std::wstring &a, const std::wstring &b) {
                               return a + delimiter + b;
                           });
}

auto FindFirstVideoOutputPin(IBaseFilter *pFilter) -> IPin * {
    IEnumPins *pEnum = nullptr;
    if (FAILED(pFilter->EnumPins(&pEnum))) {
        return nullptr;
    }

    IPin *pPin = nullptr;
    while (pEnum->Next(1, &pPin, nullptr) == S_OK) {
        PIN_DIRECTION dir;
        if (FAILED(pPin->QueryDirection(&dir))) {
            pPin->Release();
            pEnum->Release();
            return nullptr;
        }

        if (dir == PINDIR_OUTPUT) {
            AM_MEDIA_TYPE mediaType;
            if (SUCCEEDED(pPin->ConnectionMediaType(&mediaType))) {
                const bool found = mediaType.majortype == MEDIATYPE_Video;
                FreeMediaType(mediaType);

                if (found) {
                    pEnum->Release();
                    return pPin;
                }
            }
        }

        pPin->Release();
    }

    pEnum->Release();
    return nullptr;
}

}
