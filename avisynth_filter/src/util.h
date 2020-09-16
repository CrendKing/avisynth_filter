#pragma once

#include "pch.h"


namespace AvsFilter {

auto ReplaceSubstring(std::string &str, const char *target, const char *rep) -> void;
auto ConvertWideToUtf8(const std::wstring &wstr) -> std::string;
auto ConvertUtf8ToWide(const std::string &str) -> std::wstring;
auto DoubleToString(double d, int precision) -> std::string;
auto JoinStrings(const std::vector<std::wstring> &input, wchar_t delimiter) -> std::wstring;
auto FindFirstVideoOutputPin(IBaseFilter *pFilter) -> IPin *;
auto GetModuleProductVersion(HMODULE hModule) -> std::optional<std::wstring>;

}