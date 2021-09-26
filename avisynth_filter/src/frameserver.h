// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "environment.h"
#include "format.h"
#include "frame_handler.h"
#include "rc_singleton.h"


namespace SynthFilter {

class FrameServerCommon : public RefCountedSingleton<FrameServerCommon> {
    friend class FrameServerBase;
    friend class MainFrameServer;
    friend class AuxFrameServer;

public:
    FrameServerCommon();
    ~FrameServerCommon();

    DISABLE_COPYING(FrameServerCommon)

    auto SetScriptPath(const std::filesystem::path &scriptPath) -> void;
    auto LinkFrameHandler(FrameHandler *frameHandler) const -> void;
    constexpr auto GetVersionString() const -> std::string_view { return _versionString == nullptr ? "unknown AviSynth version" : _versionString; }
    constexpr auto IsFramePropsSupported() const -> bool { return _isFramePropsSupported; }
    constexpr auto GetScriptPath() const -> const std::filesystem::path & { return _scriptPath; }

private:
    auto CreateEnv() const -> IScriptEnvironment *;

    const char *_versionString;
    bool _isFramePropsSupported = false;
    std::filesystem::path _scriptPath = Environment::GetInstance().GetScriptPath();
    VideoInfo _sourceVideoInfo {};
    PClip _sourceClip;
};

class FrameServerBase {
protected:
    ~FrameServerBase();

    CTOR_WITHOUT_COPYING(FrameServerBase)

    auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool;
    auto StopScript() -> void;

    IScriptEnvironment *_env = FrameServerCommon::GetInstance().CreateEnv();
    PClip _scriptClip = nullptr;
    VideoInfo _scriptVideoInfo {};
    REFERENCE_TIME _scriptAvgFrameDuration = 0;
    std::string _errorString;
};

class MainFrameServer
    : public FrameServerBase
    , public RefCountedSingleton<MainFrameServer> {
public:
    ~MainFrameServer();

    CTOR_WITHOUT_COPYING(MainFrameServer)

    auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool;
    using FrameServerBase::StopScript;
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

class AuxFrameServer
    : public FrameServerBase
    , public RefCountedSingleton<AuxFrameServer> {
public:
    CTOR_WITHOUT_COPYING(AuxFrameServer)

    auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool;
    auto GenerateMediaType(const Format::PixelFormat &pixelFormat, const AM_MEDIA_TYPE *templateMediaType) const -> CMediaType;
    constexpr auto GetScriptPixelType() const -> int { return _scriptVideoInfo.pixel_type; }
};

#define AVSF_AVS_API MainFrameServer::GetInstance().GetEnv()

}
