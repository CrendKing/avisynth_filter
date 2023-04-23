// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "source_clip.h"

#include "frameserver.h"


namespace SynthFilter {

auto SourceClip::SetFrameHandler(FrameHandler *frameHandler) -> void {
    _frameHandler = frameHandler;
}

auto SourceClip::GetFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame {
    if (_frameHandler == nullptr) {
        Environment::GetInstance().Log(L"Source frame %6d is requested without the frame handler being linked", frameNb);
        return env->NewVideoFrame(GetVideoInfo());
    }

    return _frameHandler->GetSourceFrame(frameNb);
}

auto SourceClip::GetVideoInfo() -> const VideoInfo & {
    return FrameServerCommon::GetInstance().GetSourceVideoInfo();
}

}
