#include "pch.h"
#include "source_clip.h"


SourceClip::SourceClip(const VideoInfo &videoInfo, FrameHandler &frameHandler)
    : _videoInfo(videoInfo)
    , _frameHandler(frameHandler) {
}

auto SourceClip::GetFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame {
    return _frameHandler.GetNearestFrame(frameNb);
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