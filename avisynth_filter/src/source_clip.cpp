// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "source_clip.h"
#include "avs_handler.h"


namespace AvsFilter {

SourceClip::SourceClip(const VideoInfo &videoInfo)
    : _videoInfo(videoInfo) {
}

auto SourceClip::SetFrameHandler(FrameHandler *frameHandler) -> void {
    _frameHandler = frameHandler;
}

auto SourceClip::GetFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame {
    ASSERT(_frameHandler != nullptr);
    return _frameHandler->GetSourceFrame(frameNb, env);
}

}
