// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "environment.h"
#include "format.h"
#include "frame_handler.h"
#include "rc_singleton.h"


namespace SynthFilter {

class ScriptInstance {
public:
    auto StopScript() -> void;
    constexpr auto GetVsScript() const -> VSScript * { return _vsScript; }

protected:
    ScriptInstance();
    virtual ~ScriptInstance();

    DISABLE_COPYING(ScriptInstance)

    virtual auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool;

    VSScript *_vsScript = nullptr;
    VSNodeRef *_scriptClip = nullptr;
    VSVideoInfo _scriptVideoInfo = {};
    REFERENCE_TIME _scriptAvgFrameDuration = 0;
    std::string _errorString;
};

class MainScriptInstance
    : public ScriptInstance
    , public RefCountedSingleton<MainScriptInstance> {
public:
    ~MainScriptInstance() override;

    CTOR_WITHOUT_COPYING(MainScriptInstance)

    auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool override;
    auto LinkFrameHandler(FrameHandler *frameHandler) -> void;
    constexpr auto GetFrameHandler() const -> FrameHandler & { return *_frameHandler; }
    constexpr auto GetScriptClip() const -> VSNodeRef * { return _scriptClip; }
    constexpr auto GetSourceDrainFrame() -> const VSFrameRef * { return _sourceDrainFrame; }
    constexpr auto GetSourceAvgFrameDuration() const -> REFERENCE_TIME { return _sourceAvgFrameDuration; }
    constexpr auto GetSourceAvgFrameRate() const -> int { return _sourceAvgFrameRate; }
    constexpr auto GetScriptAvgFrameDuration() const -> REFERENCE_TIME { return _scriptAvgFrameDuration; }
    auto GetErrorString() const -> std::optional<std::string>;

private:
    FrameHandler *_frameHandler = nullptr;
    const VSFrameRef *_sourceDrainFrame = nullptr;
    REFERENCE_TIME _sourceAvgFrameDuration = 0;
    int _sourceAvgFrameRate = 0;
};

class CheckingScriptInstance
    : public ScriptInstance
    , public RefCountedSingleton<CheckingScriptInstance> {
public:
    CTOR_WITHOUT_COPYING(CheckingScriptInstance)

    auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool override;
    auto GenerateMediaType(const Format::PixelFormat &pixelFormat, const AM_MEDIA_TYPE *templateMediaType) const -> CMediaType;
    constexpr auto GetScriptPixelType() const -> int { return _scriptVideoInfo.format->id; }
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
    auto GetVersionString() const -> const char *;
    constexpr auto GetScriptPath() const -> const std::filesystem::path & { return _scriptPath; }
    constexpr auto GetSourceVideoInfo() const -> const VSVideoInfo & { return _sourceVideoInfo; }
    constexpr auto GetVsApi() const -> const VSAPI * { return _vsApi; }

private:
    std::string _versionString = "VapourSynth";
    std::filesystem::path _scriptPath = Environment::GetInstance().GetScriptPath();
    VSVideoInfo _sourceVideoInfo = {};
    const VSAPI *_vsApi;
};

#define AVSF_VS_API FrameServer::GetInstance().GetVsApi()

}
