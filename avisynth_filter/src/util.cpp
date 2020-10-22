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

auto FindFirstVideoOutputPin(IBaseFilter *pFilter) -> std::optional<IPin *> {
    ATL::CComPtr<IEnumPins> enumPins;
    if (FAILED(pFilter->EnumPins(&enumPins))) {
        return std::nullopt;
    }

    while (true) {
        ATL::CComPtr<IPin> currPin;
        if (enumPins->Next(1, &currPin, nullptr) != S_OK) {
            break;
        }

        PIN_DIRECTION dir;
        if (FAILED(currPin->QueryDirection(&dir))) {
            break;
        }

        if (dir == PINDIR_OUTPUT) {
            AM_MEDIA_TYPE mediaType;
            if (SUCCEEDED(currPin->ConnectionMediaType(&mediaType))) {
                const bool found = (mediaType.majortype == MEDIATYPE_Video || mediaType.majortype == MEDIATYPE_Stream);
                FreeMediaType(mediaType);

                if (found) {
                    return currPin;
                }
            }
        }
    }

    return std::nullopt;
}

auto ExtractBasename(const wchar_t *path) -> std::wstring {
    std::wstring ret;

    wchar_t filename[_MAX_FNAME];
    wchar_t ext[_MAX_EXT];
    if (_wsplitpath_s(path, nullptr, 0, nullptr, 0, filename, _MAX_FNAME, ext, _MAX_EXT) == 0) {
        ret = filename;
        ret += ext;
    }

    return ret;
}

}
