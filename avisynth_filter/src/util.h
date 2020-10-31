#pragma once

#include "pch.h"


namespace AvsFilter {

auto ConvertWideToUtf8(const std::wstring &wideString) -> std::string;
auto ConvertUtf8ToWide(const std::string &utf8String) -> std::wstring;
auto DoubleToString(double d, int precision) -> std::string;
auto JoinStrings(const std::vector<std::wstring> &input, wchar_t delimiter) -> std::wstring;
auto FindFirstVideoOutputPin(IBaseFilter *pFilter) -> std::optional<IPin *>;
auto ExtractBasename(const wchar_t *path) -> std::wstring;

}
