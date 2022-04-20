// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "source_clip.h"
#include "frameserver.h"


namespace SynthFilter {

SourceClip::SourceClip(const VideoInfo &videoInfo)
    : _videoInfo(videoInfo) {}

auto SourceClip::SetFrameHandler(FrameHandler *frameHandler) -> void {
    _frameHandler = frameHandler;
}

auto SourceClip::GetFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame {
    if (_frameHandler == nullptr) {
        return FrameServerCommon::GetInstance().GetSourceDummyFrame();
    }

    return _frameHandler->GetSourceFrame(frameNb);
}

}
