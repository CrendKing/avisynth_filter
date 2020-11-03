// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"
#include "frame_handler.h"
#include "rc_ptr.h"


namespace AvsFilter {

class AvsHandler {
public:
    AvsHandler();
    virtual ~AvsHandler();

    auto LinkFrameHandler(FrameHandler *frameHandler) const -> void;
    auto GenerateMediaType(int definition, const AM_MEDIA_TYPE *templateMediaType) const->AM_MEDIA_TYPE *;
    auto ReloadScript(const std::wstring &filename, const AM_MEDIA_TYPE &mediaType, bool fallback) -> bool;
    auto StopScript() -> void;

    auto GetEnv() const -> IScriptEnvironment *;
    auto GetVersionString() const -> const char *;
    auto GetScriptPixelType() const -> int;
    auto GetScriptClip() -> PClip &;
    auto GetSourceDrainFrame() -> PVideoFrame &;
    auto GetSourceAvgFrameTime() const -> REFERENCE_TIME;
    auto GetScriptAvgFrameTime() const -> REFERENCE_TIME;
    auto GetSourceAvgFrameRate() const -> int;
    auto GetErrorString() const -> std::optional<std::string>;

private:
    auto LoadModule() const -> HMODULE;
    auto CreateEnv() const -> IScriptEnvironment *;
    [[ noreturn ]] auto ShowFatalError(const char *errorMessage) const -> void ;

    HMODULE _module;
    IScriptEnvironment *_env;
    const char *_versionString;
    VideoInfo _sourceVideoInfo;
    VideoInfo _scriptVideoInfo;
    PClip _sourceClip;
    PClip _scriptClip;
    PVideoFrame _sourceDrainFrame;
    REFERENCE_TIME _sourceAvgFrameTime;
    REFERENCE_TIME _scriptAvgFrameTime;
    int _sourceAvgFrameRate;
    std::string _errorString;
};

extern ReferenceCountPointer<AvsHandler> g_avs;

}
