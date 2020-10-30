#include "pch.h"
#include "environment.h"
#include "constants.h"
#include "format.h"
#include "util.h"


namespace AvsFilter {

Environment::Environment()
    : _avsFile(_registry.ReadString(REGISTRY_VALUE_NAME_AVS_FILE))
    , _inputFormatBits(_registry.ReadNumber(REGISTRY_VALUE_NAME_FORMATS, (1 << Format::DEFINITIONS.size()) - 1))
    , _outputThreads(_registry.ReadNumber(REGISTRY_VALUE_NAME_OUTPUT_THREADS, DEFAULT_OUTPUT_SAMPLE_WORKER_THREAD_COUNT))
    , _isRemoteControlEnabled(_registry.ReadNumber(REGISTRY_VALUE_NAME_REMOTE_CONTROL, 0) != 0)
    , _logFile(nullptr)
    , _logStartTime(0) {
    const std::wstring logFilePath = _registry.ReadString(REGISTRY_VALUE_NAME_LOG_FILE);
    if (!logFilePath.empty()) {
        _logFile = _wfsopen(logFilePath.c_str(), L"w", _SH_DENYNO);
        if (_logFile != nullptr) {
            _logStartTime = timeGetTime();

            setlocale(LC_CTYPE, ".utf8");

            Log("Configured script file: %S", ExtractBasename(_avsFile.c_str()).c_str());
            Log("Configured input formats: %lu", _inputFormatBits);
            Log("Configured output threads: %i", _outputThreads);

            wchar_t processPath[MAX_PATH];
            if (GetModuleFileNameW(nullptr, processPath, MAX_PATH) != 0) {
                Log("Loading process: %S", ExtractBasename(processPath).c_str());
            }
        }
    }
}

Environment::~Environment() {
    if (_logFile != nullptr) {
        fclose(_logFile);
    }
}

auto Environment::SaveConfig() const -> void {
    _registry.WriteString(REGISTRY_VALUE_NAME_AVS_FILE, _avsFile);
    _registry.WriteNumber(REGISTRY_VALUE_NAME_FORMATS, _inputFormatBits);
}

auto Environment::Log(const char *format, ...) -> void {
    if (_logFile == nullptr) {
        return;
    }

    std::unique_lock lock(_logMutex);

    fprintf_s(_logFile, "T %6lu @ %8lu: ", GetCurrentThreadId(), timeGetTime() - _logStartTime);

    va_list args;
    va_start(args, format);
    vfprintf_s(_logFile, format, args);
    va_end(args);

    fputc('\n', _logFile);

    fflush(_logFile);
}

auto Environment::GetAvsFile() const -> const std::wstring & {
    return _avsFile;
}

auto Environment::SetAvsFile(const std::wstring &avsFile) -> void {
    _avsFile = avsFile;
}

auto Environment::GetInputFormatBits() const -> DWORD {
    return _inputFormatBits;
}

auto Environment::SetInputFormatBits(DWORD formatBits) -> void {
    _inputFormatBits = formatBits;
}

auto Environment::GetOutputThreads() const -> int {
    return _outputThreads;
}

auto Environment::IsRemoteControlEnabled() const -> bool {
    return _isRemoteControlEnabled;
}

}
