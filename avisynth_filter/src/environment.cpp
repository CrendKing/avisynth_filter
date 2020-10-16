#include "pch.h"
#include "environment.h"
#include "constants.h"
#include "format.h"


namespace AvsFilter {

Environment::Environment()
    : _refcount(0)
    , _avsModule(nullptr)
    , _avsEnv(nullptr)
    , _inputFormatBits(0)
    , _outputThreads(DEFAULT_OUTPUT_SAMPLE_WORKER_THREAD_COUNT)
    , _isRemoteControlEnabled(false)
    , _logFile(nullptr)
    , _logStartTime(0) {
}

auto Environment::Initialize(HRESULT *phr) -> bool {
    if (_refcount == 0) {
        _avsModule = LoadLibrary(L"AviSynth.dll");
        if (_avsModule == nullptr) {
            ShowFatalError(L"Failed to load AviSynth.dll", phr);
            return false;
        }

        /*
        use CreateScriptEnvironment() instead of CreateScriptEnvironment2().
        CreateScriptEnvironment() is exported from their .def file, which guarantees a stable exported name.
        CreateScriptEnvironment2() was not exported that way, thus has different names between x64 and x86 builds.
        We don't use any new feature from IScriptEnvironment2 anyway.
        */
        typedef IScriptEnvironment *(AVSC_CC *CreateScriptEnvironment_Func)(int version);
        const CreateScriptEnvironment_Func CreateScriptEnvironment = reinterpret_cast<CreateScriptEnvironment_Func>(GetProcAddress(_avsModule, "CreateScriptEnvironment"));
        if (CreateScriptEnvironment == nullptr) {
            ShowFatalError(L"Unable to locate CreateScriptEnvironment()", phr);
            return false;
        }

        // interface version 7 = AviSynth+ 3.5
        _avsEnv = CreateScriptEnvironment(7);
        if (_avsEnv == nullptr) {
            ShowFatalError(L"CreateScriptEnvironment() returns nullptr", phr);
            return false;
        }

        AVS_linkage = _avsEnv->GetAVSLinkage();

        _avsFile = _registry.ReadString(REGISTRY_VALUE_NAME_AVS_FILE);
        _inputFormatBits = _registry.ReadNumber(REGISTRY_VALUE_NAME_FORMATS, (1 << Format::DEFINITIONS.size()) - 1);
        _outputThreads = _registry.ReadNumber(REGISTRY_VALUE_NAME_OUTPUT_THREADS, DEFAULT_OUTPUT_SAMPLE_WORKER_THREAD_COUNT);
        _isRemoteControlEnabled = _registry.ReadNumber(REGISTRY_VALUE_NAME_REMOTE_CONTROL, 0) != 0;

        const std::wstring logFilePath = _registry.ReadString(REGISTRY_VALUE_NAME_LOG_FILE);
        if (!logFilePath.empty()) {
            _logFile = _wfsopen(logFilePath.c_str(), L"w", _SH_DENYNO);
            if (_logFile != nullptr) {
                _logStartTime = timeGetTime();

                setlocale(LC_CTYPE, ".utf8");

                Log("Configured script file: %S", _avsFile.c_str());
                Log("Configured input formats: %i", _inputFormatBits);
                Log("Configured output threads: %i", _outputThreads);

                wchar_t processPath[MAX_PATH];
                if (GetModuleFileName(nullptr, processPath, MAX_PATH) != 0) {
                    Log("Loading process: %S", processPath);
                }
            }
        }
    }

    _refcount += 1;
    return true;
}

auto Environment::Release() -> void {
    _refcount -= 1;

    if (_refcount == 0) {
        _avsEnv->DeleteScriptEnvironment();
        FreeLibrary(_avsModule);

        if (_logFile != nullptr) {
            fclose(_logFile);
        }
    }
}

auto Environment::ShowFatalError(const wchar_t *errorMessage, HRESULT *phr) -> void {
    *phr = E_FAIL;
    Log("%S", errorMessage);
    MessageBox(nullptr, errorMessage, FILTER_NAME_WIDE, MB_ICONERROR);
    FreeLibrary(_avsModule);
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

    fprintf_s(_logFile, "T %6i @ %8i: ", GetCurrentThreadId(), timeGetTime() - _logStartTime);

    va_list args;
    va_start(args, format);
    vfprintf_s(_logFile, format, args);
    va_end(args);

    fputc('\n', _logFile);

    fflush(_logFile);
}

auto Environment::GetAvsEnv() const -> IScriptEnvironment * {
    return _avsEnv;
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
