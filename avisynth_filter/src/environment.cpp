// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "environment.h"
#include "constants.h"
#include "format.h"


namespace AvsFilter {

Environment::Environment()
    : _useIni(false)
    , _outputThreads(0)
    , _isRemoteControlEnabled(false)
    , _logFile(nullptr)
    , _logStartTime(0)
    , _isSupportAVXx(false)
    , _isSupportSSSE3(false) {
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
        MessageBoxA(nullptr, "Unload to load settings", FILTER_NAME_FULL, MB_ICONERROR);
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

            for (const auto &[formatName, enabled] : _inputFormats) {
                Log("Configured input format %S: %i", formatName.c_str(), enabled);
            }

            Log("Configured output threads: %i", _outputThreads);
            Log("Loading process: %S%S", processFilename, processFileExt);
        }
    }

    DetectCPUID();
    Format::Init();
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

auto Environment::IsInputFormatEnabled(const std::wstring &formatName) const -> bool {
    return _inputFormats.at(formatName);
}

auto Environment::SetInputFormatEnabled(const std::wstring &formatName, bool enabled) -> void {
    _inputFormats[formatName] = enabled;

    if (_useIni) {
        const std::wstring settingName = SETTING_NAME_INPUT_FORMAT_PREFIX + formatName;
        _ini.SetBoolValue(L"", settingName.c_str(), enabled);
    }
}

auto Environment::GetOutputThreads() const -> int {
    return _outputThreads;
}

auto Environment::IsRemoteControlEnabled() const -> bool {
    return _isRemoteControlEnabled;
}

auto Environment::IsSupportAVXx() const -> bool {
    return _isSupportAVXx;
}

auto Environment::IsSupportSSSE3() const -> bool {
    return _isSupportSSSE3;
}

auto Environment::LoadSettingsFromIni() -> void {
    _avsFile = _ini.GetValue(L"", SETTING_NAME_AVS_FILE, L"");

    for (const auto &[formatName, definition] : Format::FORMATS) {
        const std::wstring settingName = SETTING_NAME_INPUT_FORMAT_PREFIX + formatName;
        _inputFormats[formatName] = _ini.GetBoolValue(L"", settingName.c_str(), true);
    }

    _outputThreads = _ini.GetLongValue(L"", SETTING_NAME_OUTPUT_THREADS, DEFAULT_OUTPUT_SAMPLE_WORKER_THREAD_COUNT);
    _isRemoteControlEnabled = _ini.GetBoolValue(L"", SETTING_NAME_REMOTE_CONTROL, false);
    _logFilePath = _ini.GetValue(L"", SETTING_NAME_LOG_FILE, L"");
}

auto Environment::LoadSettingsFromRegistry() -> void {
    _avsFile = _registry.ReadString(SETTING_NAME_AVS_FILE);

    for (const auto &[formatName, definition] : Format::FORMATS) {
        const std::wstring settingName = SETTING_NAME_INPUT_FORMAT_PREFIX + formatName;
        _inputFormats[formatName] = _registry.ReadNumber(settingName.c_str(), 1) != 0;
    }

    _outputThreads = _registry.ReadNumber(SETTING_NAME_OUTPUT_THREADS, DEFAULT_OUTPUT_SAMPLE_WORKER_THREAD_COUNT);
    _isRemoteControlEnabled = _registry.ReadNumber(SETTING_NAME_REMOTE_CONTROL, 0) != 0;
    _logFilePath = _registry.ReadString(SETTING_NAME_LOG_FILE);
}

auto Environment::SaveSettingsToIni() const -> void {
    (void) _ini.SaveFile(_iniFilePath.c_str());
}

auto Environment::SaveSettingsToRegistry() const -> void {
    _registry.WriteString(SETTING_NAME_AVS_FILE, _avsFile);

    for (const auto &[formatName, enabled] : _inputFormats) {
        const std::wstring settingName = SETTING_NAME_INPUT_FORMAT_PREFIX + formatName;
        _registry.WriteNumber(settingName.c_str(), enabled);
    }
}

auto Environment::DetectCPUID() -> void {
    struct CpuInfo {
        int eax;
        int ebx;
        int ecx;
        int edx;
    } cpuInfo = {};

    __cpuid(reinterpret_cast<int *>(&cpuInfo), 1);
    _isSupportAVXx = cpuInfo.ecx & (1 << 28);  // AVX
    _isSupportSSSE3 = cpuInfo.ecx & (1 << 9);

    __cpuid(reinterpret_cast<int *>(&cpuInfo), 7);
    _isSupportAVXx &= static_cast<bool>(cpuInfo.ebx & (1 << 5));  // AVX2
}

}
