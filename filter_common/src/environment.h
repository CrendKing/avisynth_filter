// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "singleton.h"
#include "registry.h"


namespace SynthFilter {

class Environment : public OnDemandSingleton<Environment> {
public:
    Environment();
    ~Environment();

    DISABLE_COPYING(Environment)

    auto SaveSettings() const -> void;

    template <typename... Args>
    constexpr auto Log(std::wstring_view format, Args &&...args) -> void {
        if (_logFile == nullptr) {
            return;
        }

        const std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - _logStartTime;
        const std::wstring logTemplate = std::format(L"T {:6d} @ {:11d}: {}\n", GetCurrentThreadId(), std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count(), format);

        const std::unique_lock logLock(_logMutex);

        fwprintf_s(_logFile, logTemplate.c_str(), std::forward<Args>(args)...);
        fflush(_logFile);
    }

    constexpr auto GetScriptPath() const -> const std::filesystem::path & { return _scriptPath; }
    auto SetScriptPath(const std::filesystem::path &scriptPath) -> void;
    auto IsInputFormatEnabled(std::wstring_view formatName) const -> bool;
    auto SetInputFormatEnabled(std::wstring_view formatName, bool enabled) -> void;
    constexpr auto IsRemoteControlEnabled() const -> bool { return _isRemoteControlEnabled; }
    auto SetRemoteControlEnabled(bool enabled) -> void;
    constexpr auto IsSupportAVX2() const -> bool { return std::__isa_available >= __ISA_AVAILABLE_AVX2; }
    constexpr auto IsSupportSSE4() const -> bool { return std::__isa_available >= __ISA_AVAILABLE_SSE42; }
    constexpr auto GetInitialSrcBuffer() const -> int { return _initialSrcBuffer; }
    constexpr auto GetMinExtraSrcBuffer() const -> int { return _minExtraSrcBuffer; }
    constexpr auto GetMaxExtraSrcBuffer() const -> int { return _maxExtraSrcBuffer; }
    constexpr auto GetExtraSrcBufferDecStep() const -> int { return _extraSrcBufferDecStep; }
    constexpr auto GetExtraSrcBufferIncStep() const -> int { return _extraSrcBufferIncStep; }

private:
    auto LoadSettingsFromIni() -> void;
    auto LoadSettingsFromRegistry() -> void;
    auto ValidateExtraSrcBufferValues() -> void;
    auto SaveSettingsToIni() const -> void;
    auto SaveSettingsToRegistry() const -> void;

    bool _useIni = false;
    CSimpleIniW _ini;
    std::filesystem::path _iniPath;

    Registry _registry;

    std::filesystem::path _scriptPath;
    std::unordered_set<std::wstring_view> _enabledInputFormats;
    bool _isRemoteControlEnabled = false;
    int _initialSrcBuffer;
    int _minExtraSrcBuffer;
    int _maxExtraSrcBuffer;
    int _extraSrcBufferDecStep;
    int _extraSrcBufferIncStep;

    std::filesystem::path _logPath;
    FILE *_logFile = nullptr;
    std::chrono::steady_clock::time_point _logStartTime;
    std::mutex _logMutex;
};

}
