#pragma once

#include "pch.h"
#include "constants.h"


class BufferHandler {
public:
    BufferHandler();

    auto Reset(const CLSID &inFormat, const VideoInfo *videoInfo) -> void;
    auto GetNearestFrame(REFERENCE_TIME frameTime) -> PVideoFrame;
    auto CreateFrame(REFERENCE_TIME frameTime, const BYTE *srcBuffer, long srcUnitStride, long height, IScriptEnvironment *avsEnv) -> void;
    auto WriteSample(const PVideoFrame srcFrame, BYTE *dstBuffer, long dstUnitStride, long height, IScriptEnvironment *avsEnv) const -> void;
    auto GarbageCollect(REFERENCE_TIME min, REFERENCE_TIME max) -> void;
    auto Flush() -> void;

private:
    struct TimedFrame {
        PVideoFrame frame;
        REFERENCE_TIME time;
    };

    std::deque<TimedFrame> _frameBuffer;
    std::shared_mutex _bufferMutex;

    int _formatIndex;
    const VideoInfo *_videoInfo;
};
