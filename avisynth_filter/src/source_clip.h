#pragma once

#include "pch.h"
#include "format.h"


class SourceClip : public IClip {
public:
    struct FrameInfo {
        PVideoFrame frame;
        REFERENCE_TIME startTime;
        REFERENCE_TIME stopTime;
    };

    explicit SourceClip(const VideoInfo &videoInfo);
    ~SourceClip() {
        int a = 0;
    }

    auto __stdcall GetFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame override;
    auto __stdcall GetParity(int frameNb) -> bool override;
    auto __stdcall GetAudio(void *buf, int64_t start, int64_t count, IScriptEnvironment *env) -> void override;
    auto __stdcall SetCacheHints(int cachehints, int frame_range) -> int override;
    auto __stdcall GetVideoInfo() -> const VideoInfo & override;

    auto PushBackFrame(PVideoFrame frame, REFERENCE_TIME startTime, REFERENCE_TIME stopTime) -> int;
    auto GetFrontFrame() const -> std::optional<FrameInfo>;
    auto PopFrontFrame() -> void;
    auto FlushOnNextInput() -> void;

    auto GetBufferSize() const -> int;
    auto GetMaxAccessedFrameNb() const -> int;

private:
    const VideoInfo &_videoInfo;
    mutable std::mutex _bufferMutex;
    std::map<int, FrameInfo> _frameBuffer;
    bool _flushOnNextInput;
    int _maxRequestedFrameNb;
};
