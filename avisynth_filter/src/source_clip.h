// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "frame_handler.h"


namespace SynthFilter {

class SourceClip : public IClip {
public:
    auto SetFrameHandler(FrameHandler *frameHandler) -> void;

    auto __stdcall GetFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame override;
    auto __stdcall GetVideoInfo() -> const VideoInfo & override;
    constexpr auto __stdcall GetParity(int frameNb) -> bool override { return true; }
    constexpr auto __stdcall GetAudio(void *buf, int64_t start, int64_t count, IScriptEnvironment *env) -> void override {}
    constexpr auto __stdcall SetCacheHints(int cachehints, int frame_range) -> int override { return cachehints == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0; }

private:
    FrameHandler *_frameHandler = nullptr;
};

}
