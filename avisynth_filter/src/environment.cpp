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
    if (GetModuleFileNameW(nullptr, processPath, _MAX_PATH) != 0) {
        if (_wsplitpath_s(processPath, processDrive, _MAX_DRIVE, processDirname, _MAX_DIR, processFilename, _MAX_FNAME, processFileExt, _MAX_EXT) == 0) {
            _iniFilePath = std::wstring(processDrive) + processDirname + Widen(FILTER_FILENAME_BASE) + L".ini";
        }
    }

    if (!_iniFilePath.empty()) {
        _useIni = LoadConfigFromIni();
    }
    if (!_useIni) {
        LoadConfigFromRegistry();
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

auto Environment::SaveConfig() const -> void {
    if (!_useIni || !SaveConfigToIni()) {
        SaveConfigToRegistry();
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

auto Environment::LoadConfigFromIni() -> bool {
    CSimpleIniW ini;
    if (ini.LoadFile(_iniFilePath.c_str()) != SI_OK) {
        return false;
    }

    _avsFile = ini.GetValue(L"", SETTING_NAME_AVS_FILE, L"");
    _inputFormatBits = ini.GetLongValue(L"", SETTING_NAME_FORMATS, (1 << Format::DEFINITIONS.size()) - 1);
    _outputThreads = ini.GetLongValue(L"", SETTING_NAME_OUTPUT_THREADS, DEFAULT_OUTPUT_SAMPLE_WORKER_THREAD_COUNT);
    _isRemoteControlEnabled = ini.GetBoolValue(L"", SETTING_NAME_REMOTE_CONTROL, false);
    _logFilePath = ini.GetValue(L"", SETTING_NAME_LOG_FILE, L"");

    return true;
}

auto Environment::LoadConfigFromRegistry() -> void {
    _avsFile = _registry.ReadString(SETTING_NAME_AVS_FILE);
    _inputFormatBits = _registry.ReadNumber(SETTING_NAME_FORMATS, (1 << Format::DEFINITIONS.size()) - 1);
    _outputThreads = _registry.ReadNumber(SETTING_NAME_OUTPUT_THREADS, DEFAULT_OUTPUT_SAMPLE_WORKER_THREAD_COUNT);
    _isRemoteControlEnabled = _registry.ReadNumber(SETTING_NAME_REMOTE_CONTROL, 0) != 0;
    _logFilePath = _registry.ReadString(SETTING_NAME_LOG_FILE);
}

auto Environment::SaveConfigToIni() const -> bool {
    CSimpleIniW ini;

    ini.SetValue(L"", SETTING_NAME_AVS_FILE, _avsFile.c_str());
    ini.SetLongValue(L"", SETTING_NAME_FORMATS, _inputFormatBits);
    ini.SetLongValue(L"", SETTING_NAME_OUTPUT_THREADS, _outputThreads);
    ini.SetBoolValue(L"", SETTING_NAME_REMOTE_CONTROL, _isRemoteControlEnabled);
    ini.SetValue(L"", SETTING_NAME_LOG_FILE, _logFilePath.c_str());

    return ini.SaveFile(_iniFilePath.c_str()) == SI_OK;
}

auto Environment::SaveConfigToRegistry() const -> void {
    _registry.WriteString(SETTING_NAME_AVS_FILE, _avsFile);
    _registry.WriteNumber(SETTING_NAME_FORMATS, _inputFormatBits);
    _registry.WriteNumber(SETTING_NAME_OUTPUT_THREADS, _outputThreads);
    _registry.WriteNumber(SETTING_NAME_REMOTE_CONTROL, _isRemoteControlEnabled);
    _registry.WriteString(SETTING_NAME_LOG_FILE, _logFilePath);
}

}
