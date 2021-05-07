// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once


namespace SynthFilter {

class Registry {
public:
    ~Registry();

    CTOR_WITHOUT_COPYING(Registry)

    auto Initialize() -> bool;
    constexpr explicit operator bool() const { return _registryKey != nullptr; }

    auto ReadString(const WCHAR *valueName) const -> std::wstring;
    auto ReadNumber(const WCHAR *valueName, int defaultValue) const -> DWORD;
    auto WriteString(const WCHAR *valueName, std::wstring_view valueString) const -> bool;
    auto WriteNumber(const WCHAR *valueName, DWORD valueNumber) const -> bool;

private:
    HKEY _registryKey = nullptr;
};

}
