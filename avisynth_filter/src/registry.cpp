#include "pch.h"
#include "registry.h"
#include "constants.h"


namespace AvsFilter {

Registry::Registry() {
    RegCreateKeyEx(HKEY_CURRENT_USER, REGISTRY_KEY_NAME, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &_registryKey, nullptr);
}

Registry::~Registry() {
    if (_registryKey) {
        RegCloseKey(_registryKey);
    }
}

auto Registry::ReadString(const wchar_t *valueName) const -> std::wstring {
    std::wstring ret;

    if (_registryKey) {
        wchar_t buffer[MAX_PATH];
        DWORD bufferSize = sizeof(buffer);

        const LSTATUS registryStatus = RegGetValue(_registryKey, nullptr, valueName, RRF_RT_REG_SZ, nullptr, buffer, &bufferSize);
        if (registryStatus == ERROR_SUCCESS) {
            ret.assign(buffer, bufferSize / sizeof(wchar_t) - 1);
        }
    }

    return ret;
}

auto Registry::ReadNumber(const wchar_t *valueName, int defaultValue) const -> DWORD {
    DWORD ret = defaultValue;

    if (_registryKey) {
        DWORD valueSize = sizeof(ret);
        RegGetValue(_registryKey, nullptr, valueName, RRF_RT_REG_DWORD, nullptr, &ret, &valueSize);
    }

    return ret;
}

auto Registry::WriteString(const wchar_t *valueName, const std::wstring &valueString) const -> void {
    if (_registryKey) {
        RegSetValueEx(_registryKey, valueName, 0, REG_SZ, reinterpret_cast<const BYTE *>(valueString.c_str()), static_cast<DWORD>(valueString.size()*2+2));
    }
}

auto Registry::WriteNumber(const wchar_t *valueName, DWORD valueNumber) const -> void {
    if (_registryKey) {
        RegSetValueEx(_registryKey, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&valueNumber), sizeof(valueNumber));
    }
}

}
