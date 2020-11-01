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

    auto ReadString(const wchar_t *valueName) const -> std::wstring;
    auto ReadNumber(const wchar_t *valueName, int defaultValue) const -> DWORD;
    auto WriteString(const wchar_t *valueName, const std::wstring &valueString) const -> void;
    auto WriteNumber(const wchar_t *valueName, DWORD valueNumber) const -> void;

private:
    HKEY _registryKey;
};

}
