#include "pch.h"
#include "config.h"
#include "constants.h"
#include "format.h"


namespace AvsFilter {

Config::Config()
    : _logFile(nullptr) {
    _avsFile = _registry.ReadString(REGISTRY_VALUE_NAME_AVS_FILE);
    _inputFormatBits = _registry.ReadNumber(REGISTRY_VALUE_NAME_FORMATS, (1 << Format::DEFINITIONS.size()) - 1);
    _inputThreads = _registry.ReadNumber(REGISTRY_VALUE_NAME_INPUT_THREADS, DEFAULT_INPUT_SAMPLE_WORKER_THREAD_COUNT);
    _outputThreads = _registry.ReadNumber(REGISTRY_VALUE_NAME_OUTPUT_THREADS, DEFAULT_OUTPUT_SAMPLE_WORKER_THREAD_COUNT);
    _isRemoteControlEnabled = _registry.ReadNumber(REGISTRY_VALUE_NAME_REMOTE_CONTROL, 0) != 0;

    const std::wstring logFilePath = _registry.ReadString(REGISTRY_VALUE_NAME_LOG_FILE);
    if (!logFilePath.empty()) {
        _logFile = _wfsopen(logFilePath.c_str(), L"w", _SH_DENYNO);
        if (_logFile == nullptr) {
            return;
        }

        _logStartTime = timeGetTime();

        setlocale(LC_CTYPE, ".utf8");

        Log("Configured script file: %S", _avsFile.c_str());
        Log("Configured input formats: %i", _inputFormatBits);
        Log("Configured input threads: %i", _inputThreads);
        Log("Configured output threads: %i", _outputThreads);

        wchar_t processPath[MAX_PATH];
        if (GetModuleFileName(nullptr, processPath, MAX_PATH) != 0) {
            g_config.Log("Loading process: %S", processPath);
        }
    }
}

Config::~Config() {
    if (_logFile != nullptr) {
        fclose(_logFile);
    }
}

auto Config::Save() const -> void {
    _registry.WriteString(REGISTRY_VALUE_NAME_AVS_FILE, _avsFile);
    _registry.WriteNumber(REGISTRY_VALUE_NAME_FORMATS, _inputFormatBits);
}

auto Config::Log(const char *format, ...) -> void {
    if (_logFile == nullptr) {
        return;
    }

    std::unique_lock<std::mutex> srcLock(_logMutex);

    fprintf_s(_logFile, "T %6i @ %8i: ", GetCurrentThreadId(), timeGetTime() - _logStartTime);

    va_list args;
    va_start(args, format);
    vfprintf_s(_logFile, format, args);
    va_end(args);

    fputc('\n', _logFile);

    fflush(_logFile);
}

auto Config::GetAvsFile() const -> const std::wstring & {
    return _avsFile;
}

auto Config::SetAvsFile(const std::wstring &avsFile) -> void {
    _avsFile = avsFile;
}

auto Config::GetInputFormatBits() const -> DWORD {
    return _inputFormatBits;
}

auto Config::SetInputFormatBits(DWORD formatBits) -> void {
    _inputFormatBits = formatBits;
}

auto Config::GetInputThreads() const-> int {
    return _inputThreads;
}

auto Config::GetOutputThreads() const -> int {
    return _outputThreads;
}

auto Config::IsRemoteControlEnabled() const -> bool {
    return _isRemoteControlEnabled;
}

}
