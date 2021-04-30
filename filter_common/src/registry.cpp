// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "registry.h"
#include "constants.h"


namespace SynthFilter {

Registry::~Registry() {
    if (_registryKey) {
        RegCloseKey(_registryKey);
    }
}

auto Registry::Initialize() -> bool {
    std::wstring registryKeyName = REGISTRY_KEY_NAME_PREFIX;
    registryKeyName += FILTER_NAME_WIDE;

    return RegCreateKeyExW(HKEY_CURRENT_USER, registryKeyName.c_str(), 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &_registryKey, nullptr) == ERROR_SUCCESS;
}

auto Registry::ReadString(const WCHAR *valueName) const -> std::wstring {
    std::wstring ret;

    if (_registryKey) {
        std::array<WCHAR, MAX_PATH> buffer;
        DWORD bufferSize = static_cast<DWORD>(buffer.size());

        if (const LSTATUS registryStatus = RegGetValueW(_registryKey, nullptr, valueName, RRF_RT_REG_SZ, nullptr, buffer.data(), &bufferSize);
            registryStatus == ERROR_SUCCESS) {
            ret.assign(buffer.data(), bufferSize / sizeof(WCHAR) - 1);
        }
    }

    return ret;
}

auto Registry::ReadNumber(const WCHAR *valueName, int defaultValue) const -> DWORD {
    DWORD ret = defaultValue;

    if (_registryKey) {
        DWORD valueSize = sizeof(ret);
        RegGetValueW(_registryKey, nullptr, valueName, RRF_RT_REG_DWORD, nullptr, &ret, &valueSize);
    }

    return ret;
}

auto Registry::WriteString(const WCHAR *valueName, const std::wstring &valueString) const -> bool {
    return _registryKey && RegSetValueExW(_registryKey, valueName, 0, REG_SZ, reinterpret_cast<const BYTE *>(valueString.c_str()), static_cast<DWORD>(valueString.size() * 2 + 2)) == ERROR_SUCCESS;
}

auto Registry::WriteNumber(const WCHAR *valueName, DWORD valueNumber) const -> bool {
    return _registryKey && RegSetValueExW(_registryKey, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&valueNumber), sizeof(valueNumber)) == ERROR_SUCCESS;
}

}
