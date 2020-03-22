#include "pch.h"
#include "registry.h"
#include "constants.h"


Registry::Registry()
    : _registryKey(nullptr) {
    RegCreateKeyEx(HKEY_CURRENT_USER, REGISTRY_KEY_NAME, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &_registryKey, nullptr);
}

Registry::~Registry() {
    if (_registryKey) {
        RegCloseKey(_registryKey);
    }
}

auto Registry::ReadString(const char *valueName) const -> std::string {
    std::string ret;

    if (_registryKey) {
        char buffer[MAX_PATH];
        DWORD bufferSize = MAX_PATH;

        const LSTATUS registryStatus = RegGetValue(_registryKey, nullptr, valueName, RRF_RT_REG_SZ, nullptr, buffer, &bufferSize);
        if (registryStatus == ERROR_SUCCESS) {
            ret = std::string(buffer, bufferSize).c_str();
        }
    }

    return ret;
}

auto Registry::ReadNumber(const char *valueName) const -> DWORD {
    DWORD ret = INVALID_REGISTRY_NUMBER;

    if (_registryKey) {
        DWORD valueSize = sizeof(ret);
        RegGetValue(_registryKey, nullptr, valueName, RRF_RT_REG_DWORD, nullptr, &ret, &valueSize);
    }

    return ret;
}

auto Registry::WriteString(const char *valueName, const std::string &valueString) const -> void {
    if (_registryKey) {
        RegSetValueEx(_registryKey, valueName, 0, REG_SZ, reinterpret_cast<const BYTE *>(valueString.c_str()), static_cast<DWORD>(valueString.size()));
    }
}

auto Registry::WriteNumber(const char *valueName, DWORD valueNumber) const -> void {
    if (_registryKey) {
        RegSetValueEx(_registryKey, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&valueNumber), sizeof(valueNumber));
    }
}