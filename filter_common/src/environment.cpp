// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "environment.h"

#include "constants.h"
#include "format.h"


namespace SynthFilter {

Environment::Environment()
    : _ini(true) {
    _wsetlocale(LC_ALL, L".UTF8");

    std::array<WCHAR, MAX_PATH> processPathStr {};
    std::filesystem::path processName;

    if (GetModuleFileNameW(nullptr, processPathStr.data(), static_cast<DWORD>(processPathStr.size())) != 0) {
        std::filesystem::path processPath = processPathStr.data();
        processName = processPath.filename();
        _iniPath = processPath.replace_filename(FILTER_FILENAME_BASE).replace_extension(L"ini");
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

            Log(L"Filter version: %S", FILTER_VERSION_STRING);
            Log(L"Configured script file: %s", _scriptPath.filename().c_str());

            std::ranges::for_each(
                Format::PIXEL_FORMATS,
                [this](const WCHAR *name) {
                    Log(L"Configured input format %5s: %d", name, _enabledInputFormats.contains(name));
                },
                &Format::PixelFormat::name);

            Log(L"Loading process: %s", processName.c_str());
        }
    }

    DetectCPUID();
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

    const std::wstring fmt = std::format(L"T {:6d} @ {:8d}: {}\n", GetCurrentThreadId(), timeGetTime() - _logStartTime, format);

    const std::unique_lock logLock(_logMutex);

    va_list args;
    va_start(args, format);
    vfwprintf_s(_logFile, fmt.c_str(), args);
    va_end(args);

    fflush(_logFile);
}

auto Environment::SetScriptPath(const std::filesystem::path &scriptPath) -> void {
    _scriptPath = scriptPath;

    if (_useIni) {
        _ini.SetValue(L"", SETTING_NAME_SCRIPT_FILE, _scriptPath.c_str());
    }
}

auto Environment::SetRemoteControlEnabled(bool enabled) -> void {
    _isRemoteControlEnabled = enabled;

    if (_useIni) {
        _ini.SetBoolValue(L"", SETTING_NAME_REMOTE_CONTROL, enabled);
    }
}

auto Environment::IsInputFormatEnabled(std::wstring_view formatName) const -> bool {
    return _enabledInputFormats.contains(formatName);
}

auto Environment::SetInputFormatEnabled(std::wstring_view formatName, bool enabled) -> void {
    if (enabled) {
        _enabledInputFormats.emplace(formatName);
    } else {
        _enabledInputFormats.erase(formatName);
    }

    if (_useIni) {
        const std::wstring settingName = std::format(L"{}{}", SETTING_NAME_INPUT_FORMAT_PREFIX, formatName);
        _ini.SetBoolValue(L"", settingName.c_str(), enabled);
    }
}

auto Environment::LoadSettingsFromIni() -> void {
    _scriptPath = _ini.GetValue(L"", SETTING_NAME_SCRIPT_FILE, L"");

    std::ranges::for_each(Format::PIXEL_FORMATS, [this](const Format::PixelFormat &format) {
        const std::wstring settingName = std::format(L"{}{}", SETTING_NAME_INPUT_FORMAT_PREFIX, format.name);
        if (_ini.GetBoolValue(L"", settingName.c_str(), true)) {
            _enabledInputFormats.emplace(format.name);
        }
    });

    _isRemoteControlEnabled = _ini.GetBoolValue(L"", SETTING_NAME_REMOTE_CONTROL, false);
    _logPath = _ini.GetValue(L"", SETTING_NAME_LOG_FILE, L"");

    _minExtraSrcBuffer = _ini.GetLongValue(L"", SETTING_NAME_MIN_EXTRA_SRC_BUFFER, MIN_EXTRA_SRC_BUFFER);
    _maxExtraSrcBuffer = _ini.GetLongValue(L"", SETTING_NAME_MAX_EXTRA_SRC_BUFFER, MAX_EXTRA_SRC_BUFFER);
    _extraSrcBufferDecStep = _ini.GetLongValue(L"", SETTING_NAME_EXTRA_SRC_BUFFER_DEC_STEP, EXTRA_SRC_BUFFER_DEC_STEP);
    _extraSrcBufferIncStep = _ini.GetLongValue(L"", SETTING_NAME_EXTRA_SRC_BUFFER_INC_STEP, EXTRA_SRC_BUFFER_INC_STEP);
    ValidateExtraSrcBufferValues();
}

auto Environment::LoadSettingsFromRegistry() -> void {
    _scriptPath = _registry.ReadString(SETTING_NAME_SCRIPT_FILE);

    std::ranges::for_each(Format::PIXEL_FORMATS, [this](const Format::PixelFormat &format) {
        const std::wstring settingName = std::format(L"{}{}", SETTING_NAME_INPUT_FORMAT_PREFIX, format.name);
        if (_registry.ReadNumber(settingName.c_str(), 1) != 0) {
            _enabledInputFormats.emplace(format.name);
        }
    });

    _isRemoteControlEnabled = _registry.ReadNumber(SETTING_NAME_REMOTE_CONTROL, 0) != 0;
    _logPath = _registry.ReadString(SETTING_NAME_LOG_FILE);

    _minExtraSrcBuffer = _registry.ReadNumber(SETTING_NAME_MIN_EXTRA_SRC_BUFFER, MIN_EXTRA_SRC_BUFFER);
    _maxExtraSrcBuffer = _registry.ReadNumber(SETTING_NAME_MAX_EXTRA_SRC_BUFFER, MAX_EXTRA_SRC_BUFFER);
    _extraSrcBufferDecStep = _registry.ReadNumber(SETTING_NAME_EXTRA_SRC_BUFFER_DEC_STEP, EXTRA_SRC_BUFFER_DEC_STEP);
    _extraSrcBufferIncStep = _registry.ReadNumber(SETTING_NAME_EXTRA_SRC_BUFFER_INC_STEP, EXTRA_SRC_BUFFER_INC_STEP);
    ValidateExtraSrcBufferValues();
}

auto Environment::ValidateExtraSrcBufferValues() -> void {
    _minExtraSrcBuffer = std::max(_minExtraSrcBuffer, 0);
    _maxExtraSrcBuffer = std::max(_maxExtraSrcBuffer, _minExtraSrcBuffer);
    _extraSrcBufferDecStep = std::max(_extraSrcBufferDecStep, 0);
    _extraSrcBufferIncStep = std::max(_extraSrcBufferIncStep, 0);
}

auto Environment::SaveSettingsToIni() const -> void {
    static_cast<void>(_ini.SaveFile(_iniPath.c_str()));
}

auto Environment::SaveSettingsToRegistry() const -> void {
    static_cast<void>(_registry.WriteString(SETTING_NAME_SCRIPT_FILE, _scriptPath.c_str()));
    static_cast<void>(_registry.WriteNumber(SETTING_NAME_REMOTE_CONTROL, _isRemoteControlEnabled));

    std::ranges::for_each(
        Format::PIXEL_FORMATS,
        [this](std::wstring_view name) {
            const std::wstring settingName = std::format(L"{}{}", SETTING_NAME_INPUT_FORMAT_PREFIX, name);
            static_cast<void>(_registry.WriteNumber(settingName.c_str(), IsInputFormatEnabled(name)));
        },
        &Format::PixelFormat::name);
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
