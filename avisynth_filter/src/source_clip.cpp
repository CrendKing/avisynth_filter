// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "source_clip.h"
#include "avs_handler.h"


namespace AvsFilter {

SourceClip::SourceClip(const VideoInfo &videoInfo)
    : _videoInfo(videoInfo)
    , _frameHandler(nullptr) {
}

auto SourceClip::SetFrameHandler(FrameHandler *frameHandler) -> void {
    _frameHandler = frameHandler;
}

auto SourceClip::GetFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame {
    if (_frameHandler == nullptr) {
        return g_avs->GetSourceDrainFrame();
    }

    return _frameHandler->GetSourceFrame(frameNb, env);
}

auto SourceClip::GetParity(int frameNb) -> bool {
    return false;
}

auto SourceClip::GetAudio(void *buf, int64_t start, int64_t count, IScriptEnvironment *env) -> void {
}

auto SourceClip::SetCacheHints(int cachehints, int frame_range) -> int {
    if (cachehints == CACHE_GET_MTMODE) {
        return MT_NICE_FILTER;
    }

    return 0;
}

auto SourceClip::GetVideoInfo() -> const VideoInfo & {
    return _videoInfo;
}

}
