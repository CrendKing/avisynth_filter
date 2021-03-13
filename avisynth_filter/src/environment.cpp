// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "environment.h"
#include "constants.h"
#include "format.h"


namespace AvsFilter {

Environment::Environment()
    : _useIni(false)
    , _ini(true)
    , _outputThreads(0)
    , _isRemoteControlEnabled(false)
    , _logFile(nullptr)
    , _logStartTime(0)
    , _isSupportAVXx(false)
    , _isSupportSSSE3(false) {
    std::array<WCHAR, MAX_PATH> processPathStr {};
    std::filesystem::path processName;

    if (GetModuleFileNameW(nullptr, processPathStr.data(), static_cast<DWORD>(processPathStr.size())) != 0) {
        _iniPath = std::filesystem::path(processPathStr.data());
        processName = _iniPath.filename();
        _iniPath.replace_filename(FILTER_FILENAME_BASE).replace_extension(L"ini");
        _useIni = _ini.LoadFile(_iniPath.c_str()) == SI_OK;
    }

    if (_useIni) {
        LoadSettingsFromIni();
    } else if (_registry.Initialize()) {
        LoadSettingsFromRegistry();
    } else {
        MessageBoxW(nullptr, L"Unload to load settings", FILTER_NAME_FULL, MB_ICONERROR);
    }

    if (!_logPath.empty()) {
        _logFile = _wfsopen(_logPath.c_str(), L"w", _SH_DENYNO);
        if (_logFile != nullptr) {
            _logStartTime = timeGetTime();

            _wsetlocale(LC_CTYPE, L".utf8");

            Log(L"Configured script file: %s", _avsPath.filename().c_str());

            for (const Format::PixelFormat &pixelFormat : Format::PIXEL_FORMATS) {
                Log(L"Configured input format %s: %i", pixelFormat.name.c_str(), _enabledInputFormats.contains(pixelFormat.name));
            }

            Log(L"Configured output threads: %i", _outputThreads);
            Log(L"Loading process: %s", processName.c_str());
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

auto Environment::Log(const WCHAR *format, ...) -> void {
    if (_logFile == nullptr) {
        return;
    }

    std::unique_lock lock(_logMutex);

    fwprintf_s(_logFile, L"T %6lu @ %8lu: ", GetCurrentThreadId(), timeGetTime() - _logStartTime);

    va_list args;
    va_start(args, format);
    vfwprintf_s(_logFile, format, args);
    va_end(args);

    fputwc(L'\n', _logFile);

    fflush(_logFile);
}

auto Environment::GetAvsPath() const -> const std::filesystem::path & {
    return _avsPath;
}

auto Environment::SetAvsPath(const std::filesystem::path &avsPath) -> void {
    _avsPath = avsPath;

    if (_useIni) {
        _ini.SetValue(L"", SETTING_NAME_AVS_FILE, _avsPath.c_str());
    }
}

auto Environment::IsInputFormatEnabled(const std::wstring &formatName) const -> bool {
    return _enabledInputFormats.contains(formatName);
}

auto Environment::SetInputFormatEnabled(const std::wstring &formatName, bool enabled) -> void {
    _enabledInputFormats.emplace(formatName);

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

auto Environment::GetExtraSourceBuffer() const -> int {
    return _extraSourceBuffer;
}

auto Environment::IsSupportAVXx() const -> bool {
    return _isSupportAVXx;
}

auto Environment::IsSupportSSSE3() const -> bool {
    return _isSupportSSSE3;
}

auto Environment::LoadSettingsFromIni() -> void {
    _avsPath = _ini.GetValue(L"", SETTING_NAME_AVS_FILE, L"");

    for (const Format::PixelFormat &pixelFormat : Format::PIXEL_FORMATS) {
        const std::wstring settingName = SETTING_NAME_INPUT_FORMAT_PREFIX + pixelFormat.name;
        if (_ini.GetBoolValue(L"", settingName.c_str(), true)) {
            _enabledInputFormats.emplace(pixelFormat.name);
        }
    }

    _outputThreads = _ini.GetLongValue(L"", SETTING_NAME_OUTPUT_THREADS, DEFAULT_OUTPUT_SAMPLE_WORKER_THREAD_COUNT);
    _isRemoteControlEnabled = _ini.GetBoolValue(L"", SETTING_NAME_REMOTE_CONTROL, false);
    _extraSourceBuffer = _ini.GetLongValue(L"", SETTING_NAME_EXTRA_SOURCE_BUFFER, EXTRA_SOURCE_FRAMES_AHEAD_OF_DELIVERY);
    _logPath = _ini.GetValue(L"", SETTING_NAME_LOG_FILE, L"");
}

auto Environment::LoadSettingsFromRegistry() -> void {
    _avsPath = _registry.ReadString(SETTING_NAME_AVS_FILE);

    for (const Format::PixelFormat &pixelFormat : Format::PIXEL_FORMATS) {
        const std::wstring settingName = SETTING_NAME_INPUT_FORMAT_PREFIX + pixelFormat.name;
        if (_registry.ReadNumber(settingName.c_str(), 1) != 0) {
            _enabledInputFormats.emplace(pixelFormat.name);
        }
    }

    _outputThreads = _registry.ReadNumber(SETTING_NAME_OUTPUT_THREADS, DEFAULT_OUTPUT_SAMPLE_WORKER_THREAD_COUNT);
    _isRemoteControlEnabled = _registry.ReadNumber(SETTING_NAME_REMOTE_CONTROL, 0) != 0;
    _extraSourceBuffer = _registry.ReadNumber(SETTING_NAME_EXTRA_SOURCE_BUFFER, EXTRA_SOURCE_FRAMES_AHEAD_OF_DELIVERY);
    _logPath = _registry.ReadString(SETTING_NAME_LOG_FILE);
}

auto Environment::SaveSettingsToIni() const -> void {
    (void) _ini.SaveFile(_iniPath.c_str());
}

auto Environment::SaveSettingsToRegistry() const -> void {
    _registry.WriteString(SETTING_NAME_AVS_FILE, _avsPath);

    for (const Format::PixelFormat &pixelFormat : Format::PIXEL_FORMATS) {
        const std::wstring settingName = SETTING_NAME_INPUT_FORMAT_PREFIX + pixelFormat.name;
        _registry.WriteNumber(settingName.c_str(), _enabledInputFormats.contains(pixelFormat.name));
    }
}

auto Environment::DetectCPUID() -> void {
    struct CpuInfo {
        int eax;
        int ebx;
        int ecx;
        int edx;
    } cpuInfo;

    __cpuid(reinterpret_cast<int *>(&cpuInfo), 1);
    _isSupportAVXx = cpuInfo.ecx & (1 << 28);  // AVX
    _isSupportSSSE3 = cpuInfo.ecx & (1 << 9);

    __cpuid(reinterpret_cast<int *>(&cpuInfo), 7);
    _isSupportAVXx &= static_cast<bool>(cpuInfo.ebx & (1 << 5));  // AVX2
}

}
