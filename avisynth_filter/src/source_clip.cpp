// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "source_clip.h"
#include "avs_handler.h"


namespace AvsFilter {

SourceClip::SourceClip(const VideoInfo &videoInfo)
    : _videoInfo(videoInfo) {
}

auto SourceClip::SetFrameHandler(FrameHandler &frameHandler) -> void {
    _frameHandler = &frameHandler;
}

auto SourceClip::GetFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame {
    if (_frameHandler == nullptr) {
        return g_avs->GetSourceDrainFrame();
    }

    return _frameHandler->GetSourceFrame(frameNb, env);
}

auto SourceClip::SetCacheHints(int cachehints, int frame_range) -> int {
    if (cachehints == CACHE_GET_MTMODE) {
        return MT_NICE_FILTER;
    }

    return 0;
}

}
