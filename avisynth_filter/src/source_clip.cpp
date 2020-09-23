#include "pch.h"
#include "source_clip.h"


namespace AvsFilter {

SourceClip::SourceClip(FrameHandler &frameHandler, const VideoInfo &videoInfo)
    : _frameHandler(frameHandler)
    , _videoInfo(videoInfo) {
}

auto SourceClip::GetFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame {
    return _frameHandler.GetSourceFrame(frameNb, env);
}

auto SourceClip::GetParity(int frameNb) -> bool {
    return false;
}

auto SourceClip::GetAudio(void *buf, int64_t start, int64_t count, IScriptEnvironment *env) -> void {
}

auto SourceClip::SetCacheHints(int cachehints, int frame_range) -> int {
    return 0;
}

auto SourceClip::GetVideoInfo() -> const VideoInfo & {
    return _videoInfo;
}

}
