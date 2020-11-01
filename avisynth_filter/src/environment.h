// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"
#include "rc_ptr.h"
#include "registry.h"

namespace AvsFilter {

class Environment {
public:
    Environment();
    virtual ~Environment();

    auto SaveSettings() const -> void;
    auto Log(const char *format, ...) -> void;

    auto GetAvsFile() const -> const std::wstring &;
    auto SetAvsFile(const std::wstring &avsFile) -> void;
    auto GetInputFormatBits() const->DWORD;
    auto SetInputFormatBits(DWORD formatBits) -> void;
    auto GetOutputThreads() const -> int;
    auto IsRemoteControlEnabled() const -> bool;

private:
    auto LoadSettingsFromIni() -> void;
    auto LoadSettingsFromRegistry() -> void;
    auto SaveSettingsToIni() const -> void;
    auto SaveSettingsToRegistry() const -> void;

    bool _useIni;
    CSimpleIniW _ini;
    std::wstring _iniFilePath;

    Registry _registry;

    std::wstring _avsFile;
    DWORD _inputFormatBits;
    int _outputThreads;
    bool _isRemoteControlEnabled;

    std::wstring _logFilePath;
    FILE *_logFile;
    DWORD _logStartTime;
    std::mutex _logMutex;
};

extern ReferenceCountPointer<Environment> g_env;

}
