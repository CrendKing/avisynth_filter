// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "environment.h"
#include "format.h"
#include "singleton.h"
#include "source_clip.h"


namespace SynthFilter {

class FrameServerCommon : public OnDemandSingleton<FrameServerCommon> {
    friend class FrameServerBase;
    friend class MainFrameServer;
    friend class AuxFrameServer;

public:
    FrameServerCommon();
    ~FrameServerCommon();

    DISABLE_COPYING(FrameServerCommon)

    auto SetScriptPath(const std::filesystem::path &scriptPath) -> void;
    constexpr auto GetVersionString() const -> std::string_view { return _versionString == nullptr ? "unknown AviSynth version" : _versionString; }
    constexpr auto IsFramePropsSupported() const -> bool { return _isFramePropsSupported; }
    constexpr auto GetSourceVideoInfo() const -> const VideoInfo & { return _sourceVideoInfo; }
    constexpr auto GetScriptPath() const -> const std::filesystem::path & { return _scriptPath; }

private:
    static auto CreateEnv() -> IScriptEnvironment *;

    const char *_versionString = nullptr;
    bool _isFramePropsSupported = false;
    std::filesystem::path _scriptPath = Environment::GetInstance().GetScriptPath();
    VideoInfo _sourceVideoInfo {};
};

class FrameServerBase {
protected:
    CTOR_WITHOUT_COPYING(FrameServerBase)

    auto CreateAndSetupEnv() -> void;
    auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool;
    auto StopScript() -> void;

    IScriptEnvironment *_env = nullptr;
    PClip _sourceClip = nullptr;
    PClip _scriptClip = nullptr;
    VideoInfo _scriptVideoInfo {};
    REFERENCE_TIME _scriptAvgFrameDuration = 0;
    std::string _errorString;
};

class MainFrameServer
    : public FrameServerBase
    , public OnDemandSingleton<MainFrameServer> {
public:
    MainFrameServer();
    ~MainFrameServer();

    DISABLE_COPYING(MainFrameServer)

    auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool;
    using FrameServerBase::StopScript;
    auto GetFrame(int frameNb) const -> PVideoFrame;
    auto CreateSourceDummyFrame() const -> PVideoFrame;
    auto LinkSynthFilter(const CSynthFilter *filter) -> void;
    constexpr auto GetEnv() const -> IScriptEnvironment * { return _env; }
    constexpr auto GetSourceAvgFrameDuration() const -> REFERENCE_TIME { return _sourceAvgFrameDuration; }
    constexpr auto GetSourceAvgFrameRate() const -> int { return _sourceAvgFrameRate; }
    constexpr auto GetScriptAvgFrameDuration() const -> REFERENCE_TIME { return _scriptAvgFrameDuration; }
    auto GetErrorString() const -> std::optional<std::string>;

private:
    REFERENCE_TIME _sourceAvgFrameDuration = 0;
    int _sourceAvgFrameRate = 0;
    const CSynthFilter *_filter;
};

class AuxFrameServer
    : public FrameServerBase
    , public OnDemandSingleton<AuxFrameServer> {
public:
    CTOR_WITHOUT_COPYING(AuxFrameServer)

    auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool;
    auto GenerateMediaType(const Format::PixelFormat &pixelFormat, const AM_MEDIA_TYPE *templateMediaType) const -> CMediaType;
    constexpr auto GetScriptPixelType() const -> int { return _scriptVideoInfo.pixel_type; }
};

#define AVSF_AVS_API MainFrameServer::GetInstance().GetEnv()

}
