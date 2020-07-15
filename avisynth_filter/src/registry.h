#pragma once

#include "pch.h"


class Registry {
public:
    Registry();
    ~Registry();

    auto ReadString(const char *valueName) const -> std::string;
    auto ReadNumber(const char *valueName, int defaultValue) const -> DWORD;
    auto WriteString(const char *valueName, const std::string &valueString) const -> void;
    auto WriteNumber(const char *valueName, DWORD valueNumber) const -> void;

private:
    HKEY _registryKey;
};