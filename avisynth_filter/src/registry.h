// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"


namespace AvsFilter {

class Registry {
public:
    virtual ~Registry();

    auto Initialize() -> bool;
    constexpr explicit operator bool() const { return _registryKey != nullptr; }

    auto ReadString(const WCHAR *valueName) const -> std::wstring;
    auto ReadNumber(const WCHAR *valueName, int defaultValue) const -> DWORD;
    auto WriteString(const WCHAR *valueName, const std::wstring &valueString) const -> bool;
    auto WriteNumber(const WCHAR *valueName, DWORD valueNumber) const -> bool;

private:
    HKEY _registryKey = nullptr;
};

}
