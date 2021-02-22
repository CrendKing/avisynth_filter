// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"


namespace AvsFilter {

class Registry {
public:
    Registry();
    virtual ~Registry();

    auto Initialize() -> bool;
    explicit operator bool() const;

    auto ReadString(const WCHAR *valueName) const -> std::wstring;
    auto ReadNumber(const WCHAR *valueName, int defaultValue) const -> DWORD;
    auto WriteString(const WCHAR *valueName, const std::wstring &valueString) const -> void;
    auto WriteNumber(const WCHAR *valueName, DWORD valueNumber) const -> void;

private:
    HKEY _registryKey;
};

}
