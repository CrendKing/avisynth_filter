// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "environment.h"
#include "format.h"
#include "singleton.h"


namespace SynthFilter {

class AutoReleaseVSFrame {
public:
    AutoReleaseVSFrame() = default;
    AutoReleaseVSFrame(VSFrame *newFrame);
    ~AutoReleaseVSFrame();

    auto operator=(VSFrame *other) -> const AutoReleaseVSFrame &;

    VSFrame *frame = nullptr;

private:
    auto Destroy() -> void;
};

class FrameHandler;

class FrameServerCommon : public OnDemandSingleton<FrameServerCommon> {
    friend class FrameServerBase;
    friend class MainFrameServer;
    friend class AuxFrameServer;

public:
    FrameServerCommon();

    DISABLE_COPYING(FrameServerCommon)

    auto SetScriptPath(const std::filesystem::path &scriptPath) -> void;
    constexpr auto GetVersionString() const -> std::string_view { return _versionString; }
    constexpr auto GetScriptPath() const -> const std::filesystem::path & { return _scriptPath; }
    constexpr auto GetVsApi() const -> const VSAPI * { return _vsApi; }
    constexpr auto GetVsScriptApi() const -> const VSSCRIPTAPI * { return _vsScriptApi; }

private:
    std::filesystem::path _scriptPath = Environment::GetInstance().GetScriptPath();
    std::string _versionString;
    const VSAPI *_vsApi;
    const VSSCRIPTAPI *_vsScriptApi;
};

#define AVSF_VPS_API        FrameServerCommon::GetInstance().GetVsApi()
#define AVSF_VPS_SCRIPT_API FrameServerCommon::GetInstance().GetVsScriptApi()

class FrameServerBase {
public:
    constexpr auto GetVsScript() const -> VSScript * { return _vsScript; }
    constexpr auto GetVsCore() const -> VSCore * { return _vsCore; }

protected:
    FrameServerBase();
    ~FrameServerBase();

    DISABLE_COPYING(FrameServerBase)

    auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool;
    auto StopScript() -> void;

    VSScript *_vsScript = nullptr;
    VSCore *_vsCore = nullptr;
    VSNode *_sourceClip = nullptr;
    VSNode *_scriptClip = nullptr;
    FrameHandler *_frameHandler = nullptr;
    REFERENCE_TIME _scriptAvgFrameDuration = 0;
    std::string _errorString;
};

class MainFrameServer
    : public FrameServerBase
    , public OnDemandSingleton<MainFrameServer> {
public:
    CTOR_WITHOUT_COPYING(MainFrameServer)

    auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool;
    using FrameServerBase::StopScript;
    auto CreateSourceDummyFrame() const -> const VSFrame *;
    auto LinkFrameHandler(FrameHandler *frameHandler) -> void;
    constexpr auto GetScriptClip() const -> VSNode * { return _scriptClip; }
    constexpr auto GetSourceAvgFrameDuration() const -> REFERENCE_TIME { return _sourceAvgFrameDuration; }
    constexpr auto GetSourceAvgFrameRate() const -> int { return _sourceAvgFrameRate; }
    constexpr auto GetScriptAvgFrameDuration() const -> REFERENCE_TIME { return _scriptAvgFrameDuration; }
    auto GetErrorString() const -> std::optional<std::string>;

private:
    REFERENCE_TIME _sourceAvgFrameDuration = 0;
    int _sourceAvgFrameRate = 0;
};

class AuxFrameServer
    : public FrameServerBase
    , public OnDemandSingleton<AuxFrameServer> {
public:
    CTOR_WITHOUT_COPYING(AuxFrameServer)

    auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool;
    auto GenerateMediaType(const Format::PixelFormat &pixelFormat, const AM_MEDIA_TYPE *templateMediaType) const -> CMediaType;
    auto GetScriptPixelType() const -> uint32_t;

private:
    VSVideoInfo _scriptVideoInfo;
};

}
