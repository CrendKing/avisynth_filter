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

auto Registry::ReadValue() const -> std::string {
    std::string avsFile;

    if (_registryKey) {
        char buf[MAX_PATH];
        DWORD len = MAX_PATH;

        const LSTATUS registryStatus = RegGetValue(_registryKey, nullptr, REGISTRY_AVS_FILE_VALUE_NAME, RRF_RT_REG_SZ, nullptr, buf, &len);
        if (registryStatus == ERROR_SUCCESS) {
            avsFile = std::string(buf, len).c_str();
        }
    }

    return avsFile;
}

auto Registry::WriteValue(const std::string &avsFile) const -> void {
    if (_registryKey) {
        RegSetValueEx(_registryKey, REGISTRY_AVS_FILE_VALUE_NAME, 0, REG_SZ, reinterpret_cast<const BYTE *>(avsFile.c_str()), avsFile.size());
    }
}
