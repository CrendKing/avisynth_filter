// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "avs_handler.h"
#include "api.h"
#include "constants.h"
#include "environment.h"
#include "util.h"


const AVS_Linkage *AVS_linkage = nullptr;

namespace AvsFilter {

auto __cdecl Create_AvsFilterSource(AVSValue args, void *user_data, IScriptEnvironment *env) -> AVSValue {
    return static_cast<IClip *>(user_data);
}

auto __cdecl Create_AvsFilterDisconnect(AVSValue args, void *user_data, IScriptEnvironment *env) -> AVSValue {
    // the void type is internal in AviSynth and cannot be instantiated by user script, ideal for disconnect heuristic
    return AVSValue();
}

AvsHandler::ScriptInstance::ScriptInstance(AvsHandler &handler)
    : _handler(handler)
    , _env(handler.CreateEnv()) {
}

/**
 * Create new AviSynth script clip with specified media type.
 */
auto AvsHandler::ScriptInstance::ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool {
    StopScript();

    _handler._sourceVideoInfo = Format::GetVideoFormat(mediaType).videoInfo;

    /*
     * When reloading AviSynth, there are two alternative approaches:
     *     Reload everything (the environment, the scripts, which also flushes avs frame cache).
     *     Only reload the scripts (which does not flush frame cache).
     * And for seeking, we could either reload or not reload.
     *
     * Recreating the AviSynth environment guarantees a clean start, free of picture artifacts or bugs,
     * at the cost of noticable lag.
     *
     * Usually we do not recreate to favor performance. There are cases where recreating is necessary:
     *
     * 1) Dynamic format change. This happens after playback has started, thus there will be cached frames in
     * the avs environment. After format change, reusing the cached frames may either cause artifacts or outright crash
     * (due to buffer size change).
     *
     * 2) Certain AviSynth filters and functions are not compatible, such as SVP's SVSmoothFps_NVOF().
     */

    _errorString.clear();
    AVSValue invokeResult;

    if (!_handler._scriptPath.empty()) {
        const std::string utf8Filename = ConvertWideToUtf8(_handler._scriptPath);
        const std::array<AVSValue, 2> args = { utf8Filename.c_str(), true };
        const std::array<char *const, args.size()> argNames = { nullptr, "utf8" };

        try {
            invokeResult = _env->Invoke("Import", AVSValue(args.data(), static_cast<int>(args.size())), argNames.data());
        } catch (AvisynthError &err) {
            _errorString = err.msg;
        }
    }

    if (_errorString.empty()) {
        if (!invokeResult.Defined()) {
            if (!ignoreDisconnect) {
                return false;
            }

            invokeResult = _handler._sourceClip;
        } else if (!invokeResult.IsClip()) {
            _errorString = "Error: Script does not return a clip.";
        }
    }

    if (!_errorString.empty()) {
        // must use Prefetch to match the number of threads accessing GetFrame() simultaneously
        // add trailing '\n' to pad size because snprintf() does not count the terminating null
        const char *errorFormat =
            "Subtitle(AvsFilterSource(), ReplaceStr(\"\"\"%s\"\"\", \"\n\", \"\\n\"), lsp=0)\n";

        const size_t errorScriptSize = snprintf(nullptr, 0, errorFormat, _errorString.c_str());
        const std::unique_ptr<char []> errorScript(new char[errorScriptSize]);
        snprintf(errorScript.get(), errorScriptSize, errorFormat, _errorString.c_str());
        invokeResult = _env->Invoke("Eval", AVSValue(errorScript.get()));
    }

    _scriptClip = invokeResult.AsClip();
    _scriptVideoInfo = _scriptClip->GetVideoInfo();
    _scriptAvgFrameDuration = llMulDiv(_scriptVideoInfo.fps_denominator, UNITS, _scriptVideoInfo.fps_numerator, 0);

    return true;
}

auto AvsHandler::ScriptInstance::StopScript() -> void {
    if (_scriptClip != nullptr) {
        _scriptClip = nullptr;
    }
}

auto AvsHandler::ScriptInstance::Init() const -> void {
    _env->AddFunction("AvsFilterSource", "", Create_AvsFilterSource, _handler._sourceClip);
    _env->AddFunction("AvsFilterDisconnect", "", Create_AvsFilterDisconnect, nullptr);
}

auto AvsHandler::ScriptInstance::Destroy() -> void {
    _scriptClip = nullptr;
    _env->DeleteScriptEnvironment();
}

AvsHandler::MainScriptInstance::MainScriptInstance(AvsHandler &handler)
    : ScriptInstance(handler) {
}

auto AvsHandler::MainScriptInstance::ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool {
    g_env.Log(L"ReloadAviSynthScript from main instance");

    if (__super::ReloadScript(mediaType, ignoreDisconnect)) {
        _handler._sourceAvgFrameRate = static_cast<int>(llMulDiv(_handler._sourceVideoInfo.fps_numerator, FRAME_RATE_SCALE_FACTOR, _scriptVideoInfo.fps_denominator, 0));
        _handler._sourceAvgFrameDuration = llMulDiv(_handler._sourceVideoInfo.fps_denominator, UNITS, _handler._sourceVideoInfo.fps_numerator, 0);
        _handler._sourceDrainFrame = _env->NewVideoFrame(_handler._sourceVideoInfo);

        return true;
    }

    return false;
}

auto AvsHandler::MainScriptInstance::GetErrorString() const -> std::optional<std::string> {
    return _errorString.empty() ? std::nullopt : std::make_optional(_errorString);
}

AvsHandler::CheckingScriptInstance::CheckingScriptInstance(AvsHandler &handler)
    : ScriptInstance(handler) {
}

auto AvsHandler::CheckingScriptInstance::ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool {
    g_env.Log(L"ReloadAviSynthScript from checking instance");

    if (__super::ReloadScript(mediaType, ignoreDisconnect)) {
        StopScript();

        return true;
    }

    return false;
}

/**
 * Create media type based on a template while changing its subtype. Also change fields in format if necessary.
 *
 * For example, when the original subtype has 8-bit samples and new subtype has 16-bit,
 * all "size" and FourCC values will be adjusted.
 */
auto AvsHandler::CheckingScriptInstance::GenerateMediaType(const Format::PixelFormat &pixelFormat, const AM_MEDIA_TYPE *templateMediaType) const -> CMediaType {
    FOURCCMap fourCC(&pixelFormat.mediaSubtype);

    CMediaType newMediaType(*templateMediaType);
    newMediaType.SetSubtype(&pixelFormat.mediaSubtype);

    VIDEOINFOHEADER *newVih = reinterpret_cast<VIDEOINFOHEADER *>(newMediaType.Format());
    BITMAPINFOHEADER *newBmi;

    if (SUCCEEDED(CheckVideoInfo2Type(&newMediaType))) {
        VIDEOINFOHEADER2 *newVih2 = reinterpret_cast<VIDEOINFOHEADER2 *>(newMediaType.Format());
        newBmi = &newVih2->bmiHeader;

        // generate new DAR if the new SAR differs from the old one
        // because AviSynth does not tell us anything about DAR, scaled the DAR wit the ratio between new SAR and old SAR
        if (_scriptVideoInfo.width * abs(newBmi->biHeight) != _scriptVideoInfo.height * newBmi->biWidth) {
            const long long ax = static_cast<long long>(newVih2->dwPictAspectRatioX) * _scriptVideoInfo.width * std::abs(newBmi->biHeight);
            const long long ay = static_cast<long long>(newVih2->dwPictAspectRatioY) * _scriptVideoInfo.height * newBmi->biWidth;
            const long long gcd = std::gcd(ax, ay);
            newVih2->dwPictAspectRatioX = static_cast<DWORD>(ax / gcd);
            newVih2->dwPictAspectRatioY = static_cast<DWORD>(ay / gcd);
        }
    } else {
        newBmi = &newVih->bmiHeader;
    }

    newVih->rcSource = { .left = 0, .top = 0, .right = _scriptVideoInfo.width, .bottom = _scriptVideoInfo.height };
    newVih->rcTarget = newVih->rcSource;
    newVih->AvgTimePerFrame = llMulDiv(_scriptVideoInfo.fps_denominator, UNITS, _scriptVideoInfo.fps_numerator, 0);

    newBmi->biWidth = _scriptVideoInfo.width;
    newBmi->biHeight = _scriptVideoInfo.height;
    newBmi->biBitCount = pixelFormat.bitCount;
    newBmi->biSizeImage = GetBitmapSize(newBmi);
    newMediaType.SetSampleSize(newBmi->biSizeImage);

    if (fourCC == pixelFormat.mediaSubtype) {
        // uncompressed formats (such as RGB32) have different GUIDs
        newBmi->biCompression = fourCC.GetFOURCC();
    } else {
        newBmi->biCompression = BI_RGB;
    }

    return newMediaType;
}

AvsHandler::AvsHandler()
    : _avsModule(LoadAvsModule())
    , _mainScriptInstance(*this)
    , _checkingScriptInstance(*this)
    , _versionString(_mainScriptInstance._env->Invoke("Eval", AVSValue("VersionString()")).AsString()) {
    g_env.Log(L"AvsHandler()");
    g_env.Log(L"Filter version: %S", FILTER_VERSION_STRING);
    g_env.Log(L"AviSynth version: %S", GetVersionString());

    _mainScriptInstance.Init();
    _checkingScriptInstance.Init();
}

AvsHandler::~AvsHandler() {
    g_env.Log(L"~AvsHandler()");

    _sourceClip = nullptr;
    _sourceDrainFrame = nullptr;

    _checkingScriptInstance.Destroy();
    _mainScriptInstance.Destroy();

    AVS_linkage = nullptr;
    FreeLibrary(_avsModule);
}

auto AvsHandler::LinkFrameHandler(FrameHandler &frameHandler) const -> void {
    GetSourceClip()->SetFrameHandler(frameHandler);
}

auto AvsHandler::SetScriptPath(const std::filesystem::path &scriptPath) -> void {
    _scriptPath = scriptPath;
}

auto AvsHandler::LoadAvsModule() const -> HMODULE {
    const HMODULE avsModule = LoadLibraryW(L"AviSynth.dll");
    if (avsModule == nullptr) {
        ShowFatalError(L"Failed to load AviSynth.dll");
    }
    return avsModule;
}

auto AvsHandler::CreateEnv() const -> IScriptEnvironment * {
    /*
    use CreateScriptEnvironment() instead of CreateScriptEnvironment2().
    CreateScriptEnvironment() is exported from their .def file, which guarantees a stable exported name.
    CreateScriptEnvironment2() was not exported that way, thus has different names between x64 and x86 builds.
    We don't use any new feature from IScriptEnvironment2 anyway.
    */
    using CreateScriptEnvironment_Func = auto (AVSC_CC *) (int version) -> IScriptEnvironment *;
    const CreateScriptEnvironment_Func CreateScriptEnvironment = reinterpret_cast<CreateScriptEnvironment_Func>(GetProcAddress(_avsModule, "CreateScriptEnvironment"));
    if (CreateScriptEnvironment == nullptr) {
        ShowFatalError(L"Unable to locate CreateScriptEnvironment()");
    }

    IScriptEnvironment *env = CreateScriptEnvironment(MINIMUM_AVISYNTH_PLUS_INTERFACE_VERSION);
    if (env == nullptr) {
        ShowFatalError(L"CreateScriptEnvironment() returns nullptr");
    }

    AVS_linkage = env->GetAVSLinkage();

    return env;
}

[[ noreturn ]] auto AvsHandler::ShowFatalError(const WCHAR *errorMessage) const -> void {
    g_env.Log(L"%s", errorMessage);
    MessageBoxW(nullptr, errorMessage, FILTER_NAME_FULL, MB_ICONERROR);
    FreeLibrary(_avsModule);
    throw;
}

auto AvsHandler::GetSourceClip() const -> SourceClip * {
    return reinterpret_cast<SourceClip *>(_sourceClip.operator->());
}

}
