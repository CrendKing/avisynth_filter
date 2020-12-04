// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "avs_handler.h"
#include "api.h"
#include "constants.h"
#include "environment.h"
#include "format.h"
#include "source_clip.h"
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

AvsHandler::AvsHandler()
    : _module(LoadModule())
    , _env(CreateEnv())
    , _versionString(_env->Invoke("Eval", AVSValue("VersionString()")).AsString())
    , _scriptFile(g_env.GetAvsFile())
    , _sourceVideoInfo()
    , _scriptVideoInfo()
    , _sourceClip(new SourceClip(_sourceVideoInfo))
    , _scriptClip(nullptr)
    , _sourceDrainFrame(nullptr)
    , _sourceAvgFrameDuration(0)
    , _scriptAvgFrameDuration(0)
    , _sourceAvgFrameRate(0) {
    g_env.Log("AvsHandler()");
    g_env.Log("Filter version: %s", FILTER_VERSION_STRING);
    g_env.Log("AviSynth version: %s", GetVersionString());

    _env->AddFunction("AvsFilterSource", "", Create_AvsFilterSource, _sourceClip);
    _env->AddFunction("AvsFilterDisconnect", "", Create_AvsFilterDisconnect, nullptr);
}

AvsHandler::~AvsHandler() {
    g_env.Log("~AvsHandler()");

    _env->DeleteScriptEnvironment();
    AVS_linkage = nullptr;
    FreeLibrary(_module);
}

auto AvsHandler::LinkFrameHandler(FrameHandler *frameHandler) const -> void {
    reinterpret_cast<SourceClip *>(_sourceClip.operator->())->SetFrameHandler(frameHandler);
}

/**
 * Create media type based on a template while changing its subtype. Also change fields in definition if necessary.
 *
 * For example, when the original subtype has 8-bit samples and new subtype has 16-bit,
 * all "size" and FourCC values will be adjusted.
 */
auto AvsHandler::GenerateMediaType(int definition, const AM_MEDIA_TYPE *templateMediaType) const -> AM_MEDIA_TYPE * {
    const Format::Definition &def = Format::DEFINITIONS[definition];
    FOURCCMap fourCC(&def.mediaSubtype);

    AM_MEDIA_TYPE *newMediaType = CreateMediaType(templateMediaType);
    newMediaType->subtype = def.mediaSubtype;

    VIDEOINFOHEADER *newVih = reinterpret_cast<VIDEOINFOHEADER *>(newMediaType->pbFormat);
    BITMAPINFOHEADER *newBmi;

    if (SUCCEEDED(CheckVideoInfo2Type(newMediaType))) {
        VIDEOINFOHEADER2 *newVih2 = reinterpret_cast<VIDEOINFOHEADER2 *>(newMediaType->pbFormat);
        newBmi = &newVih2->bmiHeader;

        // generate new DAR if the new SAR differs from the old one
        // because AviSynth does not tell us anything about DAR, use SAR as the DAR
        if (newBmi->biWidth * _scriptVideoInfo.height != _scriptVideoInfo.width * std::abs(newBmi->biHeight)) {
            const int gcd = std::gcd(_scriptVideoInfo.width, _scriptVideoInfo.height);
            newVih2->dwPictAspectRatioX = _scriptVideoInfo.width / gcd;
            newVih2->dwPictAspectRatioY = _scriptVideoInfo.height / gcd;
        }
    } else {
        newBmi = &newVih->bmiHeader;
    }

    newVih->rcSource = { 0, 0, _scriptVideoInfo.width, _scriptVideoInfo.height };
    newVih->rcTarget = newVih->rcSource;
    newVih->AvgTimePerFrame = llMulDiv(_scriptVideoInfo.fps_denominator, UNITS, _scriptVideoInfo.fps_numerator, 0);

    newBmi->biWidth = _scriptVideoInfo.width;
    newBmi->biHeight = _scriptVideoInfo.height;
    newBmi->biBitCount = def.bitCount;
    newBmi->biSizeImage = GetBitmapSize(newBmi);
    newMediaType->lSampleSize = newBmi->biSizeImage;

    if (IsEqualGUID(fourCC, def.mediaSubtype)) {
        // uncompressed formats (such as RGB32) have different GUIDs
        newBmi->biCompression = fourCC.GetFOURCC();
    } else {
        newBmi->biCompression = BI_RGB;
    }

    return newMediaType;
}

/**
 * Create new AviSynth script clip with specified media type.
 */
auto AvsHandler::ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool {
    g_env.Log("ReloadAviSynthScript");

    _sourceVideoInfo = Format::GetVideoFormat(mediaType).videoInfo;
    _sourceDrainFrame = _env->NewVideoFrame(_sourceVideoInfo);

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

    if (!_scriptFile.empty()) {
        const std::string utf8Filename = ConvertWideToUtf8(_scriptFile);
        const AVSValue args[2] = { utf8Filename.c_str(), true };
        const char *const argNames[2] = { nullptr, "utf8" };

        try {
            invokeResult = _env->Invoke("Import", AVSValue(args, 2), argNames);
        } catch (AvisynthError &err) {
            _errorString = err.msg;
        }
    }

    if (_errorString.empty()) {
        if (!invokeResult.Defined()) {
            if (!ignoreDisconnect) {
                return false;
            }

            invokeResult = _sourceClip;
        } else if (!invokeResult.IsClip()) {
            _errorString = "Error: Script does not return a clip.";
        }
    }

    if (!_errorString.empty()) {
        // must use Prefetch to match the number of threads accessing GetFrame() simultaneously
        const char *errorFormat = 
            "Subtitle(AvsFilterSource(), ReplaceStr(\"\"\"%s\"\"\", \"\n\", \"\\n\"), lsp=0)\n"
            "if (%i > 1) { Prefetch(%i) }\n";  // add trailing '\n' to pad size because snprintf() does not count the terminating null
            
        const size_t errorScriptSize = snprintf(nullptr, 0, errorFormat, _errorString.c_str(), g_env.GetOutputThreads(), g_env.GetOutputThreads());
        const std::unique_ptr<char []> errorScript(new char[errorScriptSize]);
        snprintf(errorScript.get(), errorScriptSize, errorFormat, _errorString.c_str(), g_env.GetOutputThreads(), g_env.GetOutputThreads());
        invokeResult = _env->Invoke("Eval", AVSValue(errorScript.get()));
    }

    _scriptClip = invokeResult.AsClip();
    _scriptVideoInfo = _scriptClip->GetVideoInfo();
    _sourceAvgFrameRate = static_cast<int>(llMulDiv(_sourceVideoInfo.fps_numerator, FRAME_RATE_SCALE_FACTOR, _scriptVideoInfo.fps_denominator, 0));
    _sourceAvgFrameDuration = llMulDiv(_sourceVideoInfo.fps_denominator, UNITS, _sourceVideoInfo.fps_numerator, 0);
    _scriptAvgFrameDuration = llMulDiv(_scriptVideoInfo.fps_denominator, UNITS, _scriptVideoInfo.fps_numerator, 0);

    return true;
}

auto AvsHandler::SetScriptFile(const std::wstring &scriptFile) -> void {
    _scriptFile = scriptFile;
}

auto AvsHandler::StopScript() -> void {
    if (_scriptClip != nullptr) {
        _scriptClip = nullptr;
    }
}

auto AvsHandler::GetEnv() const -> IScriptEnvironment * {
    return _env;
}

auto AvsHandler::GetVersionString() const -> const char * {
    return _versionString == nullptr ? "unknown AviSynth version" : _versionString;
}

auto AvsHandler::GetScriptFile() const -> std::wstring {
    return _scriptFile;
}

auto AvsHandler::GetScriptPixelType() const -> int {
    return _scriptVideoInfo.pixel_type;
}

auto AvsHandler::GetScriptClip() -> PClip & {
    return _scriptClip;
}

auto AvsHandler::GetSourceDrainFrame() -> PVideoFrame & {
    return _sourceDrainFrame;
}

auto AvsHandler::GetSourceAvgFrameDuration() const -> REFERENCE_TIME {
    return _sourceAvgFrameDuration;
}

auto AvsHandler::GetScriptAvgFrameDuration() const -> REFERENCE_TIME {
    return _scriptAvgFrameDuration;
}

auto AvsHandler::GetSourceAvgFrameRate() const -> int {
    return _sourceAvgFrameRate;
}

auto AvsHandler::GetErrorString() const -> std::optional<std::string> {
    if (_errorString.empty()) {
        return std::nullopt;
    }

    return _errorString;
}

auto AvsHandler::LoadModule() const -> HMODULE {
    const HMODULE module = LoadLibraryA("AviSynth.dll");
    if (module == nullptr) {
        ShowFatalError("Failed to load AviSynth.dll");
    }
    return module;
}

auto AvsHandler::CreateEnv() const -> IScriptEnvironment * {
    /*
    use CreateScriptEnvironment() instead of CreateScriptEnvironment2().
    CreateScriptEnvironment() is exported from their .def file, which guarantees a stable exported name.
    CreateScriptEnvironment2() was not exported that way, thus has different names between x64 and x86 builds.
    We don't use any new feature from IScriptEnvironment2 anyway.
    */
    using CreateScriptEnvironment_Func = auto (AVSC_CC *) (int version)->IScriptEnvironment *;
    const CreateScriptEnvironment_Func CreateScriptEnvironment = reinterpret_cast<CreateScriptEnvironment_Func>(GetProcAddress(_module, "CreateScriptEnvironment"));
    if (CreateScriptEnvironment == nullptr) {
        ShowFatalError("Unable to locate CreateScriptEnvironment()");
    }

    IScriptEnvironment *env = CreateScriptEnvironment(MINIMUM_AVISYNTH_PLUS_INTERFACE_VERSION);
    if (env == nullptr) {
        ShowFatalError("CreateScriptEnvironment() returns nullptr");
    }

    AVS_linkage = env->GetAVSLinkage();

    return env;
}

[[ noreturn ]] auto AvsHandler::ShowFatalError(const char *errorMessage) const -> void {
    g_env.Log("%S", errorMessage);
    MessageBoxA(nullptr, errorMessage, FILTER_NAME_FULL, MB_ICONERROR);
    FreeLibrary(_module);
    throw std::runtime_error(errorMessage);
}

}
