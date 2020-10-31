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
