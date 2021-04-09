// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"
#include "environment.h"
#include "format.h"
#include "frame_handler.h"
#include "rc_ptr.h"
#include "source_clip.h"


namespace AvsFilter {

class AvsHandler {
private:
    class ScriptInstance {
        friend class AvsHandler;

    public:
        explicit ScriptInstance(AvsHandler &handler);

        auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool;
        auto StopScript() -> void;

    protected:
        AvsHandler &_handler;
        IScriptEnvironment *_env;
        PClip _scriptClip = nullptr;
        VideoInfo _scriptVideoInfo = {};
        REFERENCE_TIME _scriptAvgFrameDuration = 0;
        std::string _errorString;

    private:
        auto Init() const -> void;
        auto Destroy() -> void;
    };

public:
    class MainScriptInstance : public ScriptInstance {
    public:
        explicit MainScriptInstance(AvsHandler &handler);

        auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool;
        constexpr auto GetEnv() const -> IScriptEnvironment * { return _env; }
        constexpr auto GetScriptClip() -> PClip & { return _scriptClip; }
        constexpr auto GetScriptAvgFrameDuration() const -> REFERENCE_TIME { return _scriptAvgFrameDuration;}
        auto GetErrorString() const -> std::optional<std::string>;
    };

    class CheckingScriptInstance : public ScriptInstance {
    public:
        explicit CheckingScriptInstance(AvsHandler &handler);

        auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool;
        auto GenerateMediaType(const Format::PixelFormat &pixelFormat, const AM_MEDIA_TYPE *templateMediaType) const -> CMediaType;
        constexpr auto GetScriptPixelType() const -> int { return _scriptVideoInfo.pixel_type;}
    };

    AvsHandler();
    virtual ~AvsHandler();

    auto LinkFrameHandler(FrameHandler *frameHandler) const -> void;
    auto SetScriptPath(const std::filesystem::path &scriptPath) -> void;
    constexpr auto GetVersionString() const -> const char * { return _versionString == nullptr ? "unknown AviSynth version" : _versionString; }
    constexpr auto GetScriptPath() const -> const std::filesystem::path & { return _scriptPath; }
    constexpr auto GetSourceDrainFrame() -> PVideoFrame &  { return _sourceDrainFrame; }
    constexpr auto GetSourceAvgFrameDuration() const -> REFERENCE_TIME  { return _sourceAvgFrameDuration; }
    constexpr auto GetSourceAvgFrameRate() const -> int  { return _sourceAvgFrameRate; }
    constexpr auto GetMainScriptInstance() -> MainScriptInstance &  { return _mainScriptInstance; }
    constexpr auto GetCheckingScriptInstance() -> CheckingScriptInstance & { return _checkingScriptInstance; }

private:
    auto LoadAvsModule() const -> HMODULE;
    auto CreateEnv() const -> IScriptEnvironment *;
    [[ noreturn ]] auto ShowFatalError(const WCHAR *errorMessage) const -> void;
    auto GetSourceClip() const -> SourceClip *;

    HMODULE _avsModule;
    MainScriptInstance _mainScriptInstance;
    CheckingScriptInstance _checkingScriptInstance;
    const char *_versionString;

    std::filesystem::path _scriptPath = g_env.GetAvsPath();
    VideoInfo _sourceVideoInfo = {};
    PClip _sourceClip = new SourceClip(_sourceVideoInfo);
    PVideoFrame _sourceDrainFrame = nullptr;
    REFERENCE_TIME _sourceAvgFrameDuration = 0;
    int _sourceAvgFrameRate = 0;
};

extern ReferenceCountPointer<AvsHandler> g_avs;

}
