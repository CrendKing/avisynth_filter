#pragma once

#include "pch.h"
#include "registry.h"


namespace AvsFilter {

class Environment {
public:
    Environment();

    auto Initialize(HRESULT *phr) -> bool;
    auto Destroy() -> void;

    auto SaveConfig() const -> void;
    auto Log(const char *format, ...) -> void;

    auto GetAvsEnv() const -> IScriptEnvironment *;
    auto GetAvsFile() const -> const std::wstring &;
    auto SetAvsFile(const std::wstring &avsFile) -> void;
    auto GetInputFormatBits() const->DWORD;
    auto SetInputFormatBits(DWORD formatBits) -> void;
    auto GetOutputThreads() const -> int;
    auto IsRemoteControlEnabled() const -> bool;

private:
    auto ShowFatalError(const wchar_t *errorMessage, HRESULT *phr) -> void;

    int _refcount;

    HMODULE _avsModule;
    IScriptEnvironment *_avsEnv;

    Registry _registry;
    std::wstring _avsFile;
    DWORD _inputFormatBits;
    int _outputThreads;
    bool _isRemoteControlEnabled;

    FILE *_logFile;
    DWORD _logStartTime;
    std::mutex _logMutex;
};

extern Environment g_env;

}
