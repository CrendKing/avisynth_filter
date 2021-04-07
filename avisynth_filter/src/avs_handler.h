// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"
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
        PClip _scriptClip;
        VideoInfo _scriptVideoInfo;
        REFERENCE_TIME _scriptAvgFrameDuration;
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
        auto GetEnv() const -> IScriptEnvironment *;
        auto GetScriptClip() -> PClip &;
        auto GetScriptAvgFrameDuration() const -> REFERENCE_TIME;
        auto GetErrorString() const -> std::optional<std::string>;
    };

    class CheckingScriptInstance : public ScriptInstance {
    public:
        explicit CheckingScriptInstance(AvsHandler &handler);

        auto GenerateMediaType(const Format::PixelFormat &pixelFormat, const AM_MEDIA_TYPE *templateMediaType) const -> CMediaType;
        auto GetScriptPixelType() const -> int;
    };

    AvsHandler();
    virtual ~AvsHandler();

    auto LinkFrameHandler(FrameHandler *frameHandler) const -> void;
    auto SetScriptPath(const std::filesystem::path &scriptPath) -> void;
    auto GetVersionString() const -> const char *;
    auto GetScriptPath() const -> const std::filesystem::path &;
    auto GetSourceDrainFrame() -> PVideoFrame &;
    auto GetSourceAvgFrameDuration() const -> REFERENCE_TIME;
    auto GetSourceAvgFrameRate() const -> int;
    auto GetMainScriptInstance() -> MainScriptInstance &;
    auto GetCheckingScriptInstance() -> CheckingScriptInstance &;

private:
    auto LoadAvsModule() const -> HMODULE;
    auto CreateEnv() const -> IScriptEnvironment *;
    [[ noreturn ]] auto ShowFatalError(const WCHAR *errorMessage) const -> void;
    auto GetSourceClip() const -> SourceClip *;

    HMODULE _avsModule;
    MainScriptInstance _mainScriptInstance;
    CheckingScriptInstance _checkingScriptInstance;
    const char *_versionString;

    std::filesystem::path _scriptPath;
    VideoInfo _sourceVideoInfo;
    PClip _sourceClip;
    PVideoFrame _sourceDrainFrame;
    REFERENCE_TIME _sourceAvgFrameDuration;
    int _sourceAvgFrameRate;
};

extern ReferenceCountPointer<AvsHandler> g_avs;

}
