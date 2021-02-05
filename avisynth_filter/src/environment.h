// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"
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
    auto IsInputFormatEnabled(const std::wstring &formatName) const -> bool;
    auto SetInputFormatEnabled(const std::wstring &formatName, bool enabled) -> void;
    auto GetOutputThreads() const -> int;
    auto IsRemoteControlEnabled() const -> bool;
    auto IsSupportAVXx() const -> bool;
    auto IsSupportSSSE3() const -> bool;

private:
    auto LoadSettingsFromIni() -> void;
    auto LoadSettingsFromRegistry() -> void;
    auto SaveSettingsToIni() const -> void;
    auto SaveSettingsToRegistry() const -> void;
    auto DetectCPUID() -> void;

    bool _useIni;
    CSimpleIniW _ini;
    std::wstring _iniFilePath;

    Registry _registry;

    std::wstring _avsFile;
    std::unordered_map<std::wstring, bool> _inputFormats;
    int _outputThreads;
    bool _isRemoteControlEnabled;

    std::wstring _logFilePath;
    FILE *_logFile;
    DWORD _logStartTime;
    std::mutex _logMutex;

    bool _isSupportAVXx;
    bool _isSupportSSSE3;
};

extern Environment g_env;

}
