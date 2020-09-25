#pragma once

#include "pch.h"
#include "registry.h"


namespace AvsFilter {

class Config {
public:
    Config();
    ~Config();

    auto Save() const -> void;
    auto Log(const char *format, ...) -> void;

    auto GetAvsFile() const -> const std::wstring &;
    auto SetAvsFile(const std::wstring &avsFile) -> void;
    auto GetInputFormatBits() const -> DWORD;
    auto SetInputFormatBits(DWORD formatBits) -> void;
    auto GetOutputThreads() const -> int;
    auto IsRemoteControlEnabled() const -> bool;

private:
    Registry _registry;
    std::wstring _avsFile;
    DWORD _inputFormatBits;
    int _outputThreads;
    bool _isRemoteControlEnabled;

    FILE *_logFile;
    DWORD _logStartTime;
    std::mutex _logMutex;
};

extern Config g_config;

}
