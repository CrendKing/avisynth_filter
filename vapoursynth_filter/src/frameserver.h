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
    auto LinkFrameHandler(FrameHandler *frameHandler) -> void;
    constexpr auto GetVersionString() const -> std::string_view { return _versionString; }
    constexpr auto GetScriptPath() const -> const std::filesystem::path & { return _scriptPath; }
    constexpr auto GetFrameHandler() const -> FrameHandler * { return _frameHandler; }
    constexpr auto GetSourceVideoInfo() const -> const VSVideoInfo & { return _sourceVideoInfo; }
    constexpr auto GetVsApi() const -> const VSAPI * { return _vsApi; }

private:
    std::filesystem::path _scriptPath = Environment::GetInstance().GetScriptPath();
    std::string _versionString;
    FrameHandler *_frameHandler = nullptr;
    VSVideoInfo _sourceVideoInfo {};
    const VSAPI *_vsApi;
};

class FrameServerBase {
public:
    constexpr auto GetVsScript() const -> VSScript * { return _vsScript; }

protected:
    FrameServerBase();
    ~FrameServerBase();

    DISABLE_COPYING(FrameServerBase)

    auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool;
    auto StopScript() -> void;

    VSScript *_vsScript = nullptr;
    VSNodeRef *_scriptClip = nullptr;
    VSVideoInfo _scriptVideoInfo {};
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
    constexpr auto GetScriptClip() const -> VSNodeRef * { return _scriptClip; }
    constexpr auto GetSourceDrainFrame() const -> const VSFrameRef * { return _sourceDrainFrame; }
    constexpr auto GetSourceAvgFrameDuration() const -> REFERENCE_TIME { return _sourceAvgFrameDuration; }
    constexpr auto GetSourceAvgFrameRate() const -> int { return _sourceAvgFrameRate; }
    constexpr auto GetScriptAvgFrameDuration() const -> REFERENCE_TIME { return _scriptAvgFrameDuration; }
    auto GetErrorString() const -> std::optional<std::string>;

private:
    const VSFrameRef *_sourceDrainFrame = nullptr;
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
    constexpr auto GetScriptPixelType() const -> int { return _scriptVideoInfo.format->id; }
};

#define AVSF_VPS_API FrameServerCommon::GetInstance().GetVsApi()

}
