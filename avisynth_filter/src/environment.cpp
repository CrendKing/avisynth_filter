// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "environment.h"
#include "constants.h"
#include "format.h"


namespace AvsFilter {

Environment::Environment()
    : _useIni(false)
    , _inputFormatBits(0)
    , _outputThreads(0)
    , _isRemoteControlEnabled(false)
    , _logFile(nullptr)
    , _logStartTime(0) {
    wchar_t processPath[_MAX_PATH] {};
    wchar_t processDrive[_MAX_DRIVE] {};
    wchar_t processDirname[_MAX_DIR] {};
    wchar_t processFilename[_MAX_FNAME] {};
    wchar_t processFileExt[_MAX_EXT] {};
    if (GetModuleFileNameW(nullptr, processPath, _MAX_PATH) != 0 &&
        _wsplitpath_s(processPath, processDrive, _MAX_DRIVE, processDirname, _MAX_DIR, processFilename, _MAX_FNAME, processFileExt, _MAX_EXT) == 0) {
        _iniFilePath = std::wstring(processDrive) + processDirname + Widen(FILTER_FILENAME_BASE) + L".ini";
        _useIni = _ini.LoadFile(_iniFilePath.c_str()) == SI_OK;
    }

    if (_useIni) {
        LoadSettingsFromIni();
    } else if (_registry.Initialize()) {
        LoadSettingsFromRegistry();
    } else {
        const char *errorMessage = "Unload to load settings";
        g_env->Log("%S", errorMessage);
        MessageBoxA(nullptr, errorMessage, FILTER_NAME_FULL, MB_ICONERROR);
    }

    if (!_logFilePath.empty()) {
        _logFile = _wfsopen(_logFilePath.c_str(), L"w", _SH_DENYNO);
        if (_logFile != nullptr) {
            _logStartTime = timeGetTime();

            setlocale(LC_CTYPE, ".utf8");

            wchar_t avsFilename[_MAX_FNAME];
            wchar_t avsExt[_MAX_EXT];
            if (_wsplitpath_s(_avsFile.c_str(), nullptr, 0, nullptr, 0, avsFilename, _MAX_FNAME, avsExt, _MAX_EXT) != 0) {
                wcscpy_s(avsFilename, L"unknown");
            }
            Log("Configured script file: %S%S", avsFilename, avsExt);
            Log("Configured input formats: %lu", _inputFormatBits);
            Log("Configured output threads: %i", _outputThreads);
            Log("Loading process: %S%S", processFilename, processFileExt);
        }
    }
}

Environment::~Environment() {
    if (_logFile != nullptr) {
        fclose(_logFile);
    }
}

auto Environment::SaveSettings() const -> void {
    if (_useIni) {
        SaveSettingsToIni();
    } else if (_registry) {
        SaveSettingsToRegistry();
    }
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

    if (_useIni) {
        _ini.SetValue(L"", SETTING_NAME_AVS_FILE, _avsFile.c_str());
    }
}

auto Environment::GetInputFormatBits() const -> DWORD {
    return _inputFormatBits;
}

auto Environment::SetInputFormatBits(DWORD formatBits) -> void {
    _inputFormatBits = formatBits;

    if (_useIni) {
        _ini.SetLongValue(L"", SETTING_NAME_FORMATS, _inputFormatBits);
    }
}

auto Environment::GetOutputThreads() const -> int {
    return _outputThreads;
}

auto Environment::IsRemoteControlEnabled() const -> bool {
    return _isRemoteControlEnabled;
}

auto Environment::LoadSettingsFromIni() -> void {
    _avsFile = _ini.GetValue(L"", SETTING_NAME_AVS_FILE, L"");
    _inputFormatBits = _ini.GetLongValue(L"", SETTING_NAME_FORMATS, (1 << Format::DEFINITIONS.size()) - 1);
    _outputThreads = _ini.GetLongValue(L"", SETTING_NAME_OUTPUT_THREADS, DEFAULT_OUTPUT_SAMPLE_WORKER_THREAD_COUNT);
    _isRemoteControlEnabled = _ini.GetBoolValue(L"", SETTING_NAME_REMOTE_CONTROL, false);
    _logFilePath = _ini.GetValue(L"", SETTING_NAME_LOG_FILE, L"");
}

auto Environment::LoadSettingsFromRegistry() -> void {
    _avsFile = _registry.ReadString(SETTING_NAME_AVS_FILE);
    _inputFormatBits = _registry.ReadNumber(SETTING_NAME_FORMATS, (1 << Format::DEFINITIONS.size()) - 1);
    _outputThreads = _registry.ReadNumber(SETTING_NAME_OUTPUT_THREADS, DEFAULT_OUTPUT_SAMPLE_WORKER_THREAD_COUNT);
    _isRemoteControlEnabled = _registry.ReadNumber(SETTING_NAME_REMOTE_CONTROL, 0) != 0;
    _logFilePath = _registry.ReadString(SETTING_NAME_LOG_FILE);
}

auto Environment::SaveSettingsToIni() const -> void {
    (void) _ini.SaveFile(_iniFilePath.c_str());
}

auto Environment::SaveSettingsToRegistry() const -> void {
    _registry.WriteString(SETTING_NAME_AVS_FILE, _avsFile);
    _registry.WriteNumber(SETTING_NAME_FORMATS, _inputFormatBits);
}

}
