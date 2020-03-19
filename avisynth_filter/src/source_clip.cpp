#include "pch.h"
#include "source_clip.h"


SourceClip::SourceClip(const VideoInfo &videoInfo, BufferHandler &bufferHandler)
    : _videoInfo(videoInfo)
    , _bufferHandler(bufferHandler) {
}

auto SourceClip::GetFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame {
    const REFERENCE_TIME frameTime = frameNb * llMulDiv(_videoInfo.fps_denominator, UNITS, _videoInfo.fps_numerator, 0);
    return _bufferHandler.GetNearestFrame(frameTime);
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