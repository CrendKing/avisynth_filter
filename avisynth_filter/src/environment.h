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
    auto Log(const WCHAR *format, ...) -> void;

    auto GetAvsPath() const -> const std::filesystem::path &;
    auto SetAvsPath(const std::filesystem::path &avsPath) -> void;
    auto IsInputFormatEnabled(const std::wstring &formatName) const -> bool;
    auto SetInputFormatEnabled(const std::wstring &formatName, bool enabled) -> void;
    auto GetOutputThreads() const -> int;
    auto IsRemoteControlEnabled() const -> bool;
    auto GetExtraSourceBuffer() const -> int;
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
    std::filesystem::path _iniPath;

    Registry _registry;

    std::filesystem::path _avsPath;
    std::unordered_set<std::wstring> _enabledInputFormats;
    int _outputThreads;
    bool _isRemoteControlEnabled;
    int _extraSourceBuffer;

    std::filesystem::path _logPath;
    FILE *_logFile;
    DWORD _logStartTime;
    std::mutex _logMutex;

    bool _isSupportAVXx;
    bool _isSupportSSSE3;
};

extern Environment g_env;

}
