// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"
#include "format.h"
#include "frame_handler.h"
#include "rc_ptr.h"


namespace AvsFilter {

class AvsHandler {
public:
    AvsHandler();
    virtual ~AvsHandler();

    auto LinkFrameHandler(FrameHandler *frameHandler) const -> void;
    auto GenerateMediaType(const Format::PixelFormat &pixelFormat, const AM_MEDIA_TYPE *templateMediaType) const -> CMediaType;
    auto ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool;
    auto SetScriptPath(const std::filesystem::path &scriptPath) -> void;
    auto StopScript() -> void;

    auto GetEnv() const -> IScriptEnvironment *;
    auto GetVersionString() const -> const char *;
    auto GetScriptPath() const -> const std::filesystem::path &;
    auto GetScriptPixelType() const -> int;
    auto GetScriptClip() -> PClip &;
    auto GetSourceDrainFrame() -> PVideoFrame &;
    auto GetSourceAvgFrameDuration() const -> REFERENCE_TIME;
    auto GetScriptAvgFrameDuration() const -> REFERENCE_TIME;
    auto GetSourceAvgFrameRate() const -> int;
    auto GetErrorString() const -> std::optional<std::string>;

private:
    auto LoadAvsModule() const -> HMODULE;
    auto CreateEnv() const -> IScriptEnvironment *;
    [[ noreturn ]] auto ShowFatalError(const WCHAR *errorMessage) const -> void ;

    HMODULE _avsModule;
    IScriptEnvironment *_env;
    const char *_versionString;
    std::filesystem::path _scriptPath;
    VideoInfo _sourceVideoInfo;
    VideoInfo _scriptVideoInfo;
    PClip _sourceClip;
    PClip _scriptClip;
    PVideoFrame _sourceDrainFrame;
    REFERENCE_TIME _sourceAvgFrameDuration;
    REFERENCE_TIME _scriptAvgFrameDuration;
    int _sourceAvgFrameRate;
    std::string _errorString;
};

extern ReferenceCountPointer<AvsHandler> g_avs;

}
