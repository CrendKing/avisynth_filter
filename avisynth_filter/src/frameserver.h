// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "environment.h"
#include "format.h"
#include "frame_handler.h"
#include "rc_singleton.h"
#include "source_clip.h"


namespace SynthFilter {

class ScriptInstance {
protected:
    ScriptInstance();
    ~ScriptInstance();

    DISABLE_COPYING(ScriptInstance)

    auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool;
    auto StopScript() -> void;

    IScriptEnvironment *_env;
    PClip _scriptClip = nullptr;
    VideoInfo _scriptVideoInfo = {};
    REFERENCE_TIME _scriptAvgFrameDuration = 0;
    std::string _errorString;
};

class MainScriptInstance
    : public ScriptInstance
    , public RefCountedSingleton<MainScriptInstance> {
public:
    ~MainScriptInstance();

    CTOR_WITHOUT_COPYING(MainScriptInstance)

    auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool;
    using ScriptInstance::StopScript;
    auto GetFrame(int frameNb) const -> PVideoFrame;
    constexpr auto GetEnv() const -> IScriptEnvironment * { return _env; }
    constexpr auto GetSourceDrainFrame() const -> const PVideoFrame & { return _sourceDrainFrame; }
    constexpr auto GetSourceAvgFrameDuration() const -> REFERENCE_TIME { return _sourceAvgFrameDuration; }
    constexpr auto GetSourceAvgFrameRate() const -> int { return _sourceAvgFrameRate; }
    constexpr auto GetScriptAvgFrameDuration() const -> REFERENCE_TIME { return _scriptAvgFrameDuration; }
    auto GetErrorString() const -> std::optional<std::string>;

private:
    PVideoFrame _sourceDrainFrame = nullptr;
    REFERENCE_TIME _sourceAvgFrameDuration = 0;
    int _sourceAvgFrameRate = 0;
};

class CheckingScriptInstance
    : public ScriptInstance
    , public RefCountedSingleton<CheckingScriptInstance> {
public:
    CTOR_WITHOUT_COPYING(CheckingScriptInstance)

    auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool;
    auto GenerateMediaType(const Format::PixelFormat &pixelFormat, const AM_MEDIA_TYPE *templateMediaType) const -> CMediaType;
    constexpr auto GetScriptPixelType() const -> int { return _scriptVideoInfo.pixel_type; }
};

class FrameServer : public RefCountedSingleton<FrameServer> {
    friend class ScriptInstance;
    friend class MainScriptInstance;
    friend class CheckingScriptInstance;

public:
    FrameServer();
    ~FrameServer();

    DISABLE_COPYING(FrameServer)

    auto SetScriptPath(const std::filesystem::path &scriptPath) -> void;
    auto LinkFrameHandler(FrameHandler *frameHandler) const -> void;
    constexpr auto GetVersionString() const -> const char * { return _versionString == nullptr ? "unknown AviSynth version" : _versionString; }
    constexpr auto GetScriptPath() const -> const std::filesystem::path & { return _scriptPath; }

private:
    auto CreateEnv() const -> IScriptEnvironment *;

    const char *_versionString;
    std::filesystem::path _scriptPath = Environment::GetInstance().GetScriptPath();
    VideoInfo _sourceVideoInfo = {};
    PClip _sourceClip;
};

}
