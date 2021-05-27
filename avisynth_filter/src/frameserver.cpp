// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "frameserver.h"
#include "api.h"
#include "constants.h"
#include "source_clip.h"


const AVS_Linkage *AVS_linkage = nullptr;

namespace SynthFilter {

auto __cdecl Create_AvsFilterSource(AVSValue args, void *user_data, IScriptEnvironment *env) -> AVSValue {
    return static_cast<IClip *>(user_data);
}

auto __cdecl Create_AvsFilterDisconnect(AVSValue args, void *user_data, IScriptEnvironment *env) -> AVSValue {
    // the void type is internal in AviSynth and cannot be instantiated by user script, ideal for disconnect heuristic
    return AVSValue();
}

FrameServerCommon::FrameServerCommon() {
    Environment::GetInstance().Log(L"FrameServerCommon()");

    IScriptEnvironment *env = CreateEnv();
    AVS_linkage = env->GetAVSLinkage();
    _versionString = env->Invoke("Eval", AVSValue("VersionString()")).AsString();
    Environment::GetInstance().Log(L"AviSynth version: %S", GetVersionString());
    env->DeleteScriptEnvironment();

    _sourceClip = new SourceClip(_sourceVideoInfo);
}

FrameServerCommon::~FrameServerCommon() {
    Environment::GetInstance().Log(L"~FrameServerCommon()");

    _sourceClip = nullptr;
    AVS_linkage = nullptr;
}

auto FrameServerCommon::SetScriptPath(const std::filesystem::path &scriptPath) -> void {
    _scriptPath = scriptPath;
}

auto FrameServerCommon::LinkFrameHandler(FrameHandler *frameHandler) const -> void {
    reinterpret_cast<SourceClip *>(_sourceClip.operator->())->SetFrameHandler(frameHandler);
}

auto FrameServerCommon::CreateEnv() const -> IScriptEnvironment * {
    /*
    use CreateScriptEnvironment() instead of CreateScriptEnvironment2().
    CreateScriptEnvironment() is exported from their .def file, which guarantees a stable exported name.
    CreateScriptEnvironment2() was not exported that way, thus has different names between x64 and x86 builds.
    We don't use any new feature from IScriptEnvironment2 anyway.
    */
    IScriptEnvironment *env = CreateScriptEnvironment(MINIMUM_AVISYNTH_PLUS_INTERFACE_VERSION);
    if (env == nullptr) {
        const WCHAR *errorMessage = L"CreateScriptEnvironment() returns nullptr";
        Environment::GetInstance().Log(errorMessage);
        MessageBoxW(nullptr, errorMessage, FILTER_NAME_FULL, MB_ICONERROR);
        throw;
    }

    env->AddFunction("AvsFilterSource", "", Create_AvsFilterSource, _sourceClip);
    env->AddFunction("AvsFilterDisconnect", "", Create_AvsFilterDisconnect, nullptr);

    return env;
}

FrameServerBase::~FrameServerBase() {
    StopScript();
    _env->DeleteScriptEnvironment();
}

/**
 * Create new script clip with specified media type.
 */
auto FrameServerBase::ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool {
    StopScript();

    FrameServerCommon::GetInstance()._sourceVideoInfo = Format::GetVideoFormat(mediaType, this).videoInfo;

    _errorString.clear();
    AVSValue invokeResult;

    if (!FrameServerCommon::GetInstance()._scriptPath.empty()) {
        const std::string utf8Filename = ConvertWideToUtf8(FrameServerCommon::GetInstance()._scriptPath.native());
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

            invokeResult = FrameServerCommon::GetInstance()._sourceClip;
        } else if (!invokeResult.IsClip()) {
            _errorString = "Error: Script does not return a clip.";
        }
    }

    if (!_errorString.empty()) {
        const std::string errorScript = std::format("Subtitle(AvsFilterSource(), \"{}\", lsp=0)", ReplaceSubstr(_errorString, "\n", "\\n"));
        invokeResult = _env->Invoke("Eval", AVSValue(errorScript.c_str()));
    }

    _scriptClip = invokeResult.AsClip();
    Environment::GetInstance().Log(L"New script clip: %p", _scriptClip);
    _scriptVideoInfo = _scriptClip->GetVideoInfo();
    _scriptAvgFrameDuration = llMulDiv(_scriptVideoInfo.fps_denominator, UNITS, _scriptVideoInfo.fps_numerator, 0);

    return true;
}

auto FrameServerBase::StopScript() -> void {
    if (_scriptClip != nullptr) {
        Environment::GetInstance().Log(L"Release script clip: %p", _scriptClip);
        _scriptClip = nullptr;
    }
}

MainFrameServer::~MainFrameServer() {
    _sourceDrainFrame = nullptr;
}

auto MainFrameServer::ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool {
    Environment::GetInstance().Log(L"ReloadScript from main instance");

    if (__super::ReloadScript(mediaType, ignoreDisconnect)) {
        _sourceDrainFrame = _env->NewVideoFrame(FrameServerCommon::GetInstance()._sourceVideoInfo);
        _sourceAvgFrameRate = static_cast<int>(llMulDiv(FrameServerCommon::GetInstance()._sourceVideoInfo.fps_numerator, FRAME_RATE_SCALE_FACTOR, _scriptVideoInfo.fps_denominator, 0));
        _sourceAvgFrameDuration = llMulDiv(FrameServerCommon::GetInstance()._sourceVideoInfo.fps_denominator, UNITS, FrameServerCommon::GetInstance()._sourceVideoInfo.fps_numerator, 0);

        return true;
    }

    return false;
}

auto MainFrameServer::GetFrame(int frameNb) const -> PVideoFrame {
    return _scriptClip->GetFrame(frameNb, _env);
}

auto MainFrameServer::GetErrorString() const -> std::optional<std::string> {
    return _errorString.empty() ? std::nullopt : std::make_optional(_errorString);
}

auto AuxFrameServer::ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool {
    Environment::GetInstance().Log(L"ReloadScript from checking instance");

    if (__super::ReloadScript(mediaType, ignoreDisconnect)) {
        StopScript();

        // AviSynth+ prefetchers are only destroyed when the environment is deleted
        // just stopping the script clip is not enough
        _env->DeleteScriptEnvironment();
        _env = FrameServerCommon::GetInstance().CreateEnv();

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
auto AuxFrameServer::GenerateMediaType(const Format::PixelFormat &pixelFormat, const AM_MEDIA_TYPE *templateMediaType) const -> CMediaType {
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

}