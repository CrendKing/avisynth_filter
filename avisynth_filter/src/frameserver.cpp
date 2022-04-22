// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "frameserver.h"

#include "api.h"
#include "constants.h"


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
    Environment::GetInstance().Log(L"AviSynth version: %hs", GetVersionString().data());

    try {
        // AVS+ 3.6 is interface version 8
        env->CheckVersion(8);
        _isFramePropsSupported = true;
        Environment::GetInstance().Log(L"AviSynth supports frame properties");
    } catch (...) {
    }

    env->DeleteScriptEnvironment();
}

FrameServerCommon::~FrameServerCommon() {
    Environment::GetInstance().Log(L"~FrameServerCommon()");

    AVS_linkage = nullptr;
}

auto FrameServerCommon::CreateEnv() -> IScriptEnvironment * {
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

    return env;
}

auto FrameServerBase::CreateAndSetupEnv() -> void {
    _sourceClip = new SourceClip();
    _env = FrameServerCommon::CreateEnv();
    _env->AddFunction("AvsFilterSource", "", Create_AvsFilterSource, _sourceClip);
    _env->AddFunction("AvsFilterDisconnect", "", Create_AvsFilterDisconnect, nullptr);
}

/**
 * Create new script clip with specified media type.
 */
auto FrameServerBase::ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool {
    StopScript();

    const VideoInfo &sourceVideoInfo = Format::GetVideoFormat(mediaType, this).videoInfo;
    FrameServerCommon::GetInstance()._sourceVideoInfo = sourceVideoInfo;

    _errorString.clear();
    AVSValue invokeResult;

    if (!FrameServerCommon::GetInstance()._scriptPath.empty()) {
        const std::string utf8Filename = ConvertWideToUtf8(FrameServerCommon::GetInstance()._scriptPath.native());
        const std::array<AVSValue, 2> args { utf8Filename.c_str(), true };
        const std::array<char *const, args.size()> argNames { nullptr, "utf8" };

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

            invokeResult = _sourceClip;
        } else if (!invokeResult.IsClip()) {
            _errorString = "Error: Script does not return a clip.";
        }
    }

    if (!_errorString.empty()) {
        _errorString = std::regex_replace(_errorString, std::regex("\n"), "\\n");
        _errorString = std::regex_replace(_errorString, std::regex("\""), "'");
        const std::string errorScript = std::format("Subtitle(AvsFilterSource(), \"{}\", lsp=0)", _errorString);
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

MainFrameServer::MainFrameServer() {
    CreateAndSetupEnv();
}

MainFrameServer::~MainFrameServer() {
    StopScript();
    _env->DeleteScriptEnvironment();
}

auto MainFrameServer::ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool {
    Environment::GetInstance().Log(L"ReloadScript from main frameserver");

    if (__super::ReloadScript(mediaType, ignoreDisconnect)) {
        const VideoInfo &sourceVideoInfo = FrameServerCommon::GetInstance()._sourceVideoInfo;
        _sourceAvgFrameRate = static_cast<int>(llMulDiv(sourceVideoInfo.fps_numerator, FRAME_RATE_SCALE_FACTOR, sourceVideoInfo.fps_denominator, 0));
        _sourceAvgFrameDuration = llMulDiv(sourceVideoInfo.fps_denominator, UNITS, sourceVideoInfo.fps_numerator, 0);

        return true;
    }

    return false;
}

auto MainFrameServer::GetFrame(int frameNb) const -> PVideoFrame {
    return _scriptClip->GetFrame(frameNb, _env);
}

auto MainFrameServer::CreateSourceDummyFrame() const -> PVideoFrame {
    return _env->NewVideoFrameP(FrameServerCommon::GetInstance().GetSourceVideoInfo(), nullptr);
}

auto MainFrameServer::LinkFrameHandler(FrameHandler *frameHandler) const -> void {
    reinterpret_cast<SourceClip *>(static_cast<void *>(_sourceClip))->SetFrameHandler(frameHandler);
}

auto AuxFrameServer::ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool {
    Environment::GetInstance().Log(L"ReloadScript from auxiliary frameserver");

    CreateAndSetupEnv();

    if (__super::ReloadScript(mediaType, ignoreDisconnect)) {
        StopScript();

        // AviSynth+ prefetchers are only destroyed when the environment is deleted
        // just stopping the script clip is not enough
        _env->DeleteScriptEnvironment();

        return true;
    }

    return false;
}

}
