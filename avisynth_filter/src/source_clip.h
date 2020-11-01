// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"
#include "frame_handler.h"


namespace AvsFilter {

class CAviSynthFilter;

class SourceClip : public IClip {
public:
    explicit SourceClip(const VideoInfo &videoInfo);

    auto SetFrameHandler(FrameHandler *frameHandler) -> void;

    auto __stdcall GetFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame override;
    auto __stdcall GetParity(int frameNb) -> bool override;
    auto __stdcall GetAudio(void *buf, int64_t start, int64_t count, IScriptEnvironment *env) -> void override;
    auto __stdcall SetCacheHints(int cachehints, int frame_range) -> int override;
    auto __stdcall GetVideoInfo() -> const VideoInfo & override;

private:
    const VideoInfo &_videoInfo;
    FrameHandler *_frameHandler;
};

}
