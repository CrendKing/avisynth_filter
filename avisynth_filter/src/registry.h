#pragma once

#include "pch.h"


class Registry {
public:
    Registry();
    ~Registry();

    auto ReadValue() const -> std::string;
    auto WriteValue(const std::string &avsFile) const -> void;

private:
    HKEY _registryKey;
};