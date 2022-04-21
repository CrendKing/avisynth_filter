// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "source_clip.h"
#include "frameserver.h"


namespace SynthFilter {

auto SourceClip::SetFrameHandler(FrameHandler *frameHandler) -> void {
    _frameHandler = frameHandler;
}

auto SourceClip::GetFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame {
    if (_frameHandler == nullptr) {
        return env->NewVideoFrameP(GetVideoInfo(), nullptr);
    }

    return _frameHandler->GetSourceFrame(frameNb);
}

auto SourceClip::GetVideoInfo() -> const VideoInfo & {
    return FrameServerCommon::GetInstance().GetSourceVideoInfo();
}

}
