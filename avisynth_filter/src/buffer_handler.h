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
    auto GarbageCollect(REFERENCE_TIME frameTime) -> void;
    auto StartDraining() -> void;
    auto StopDraining() -> void;
    auto Flush() -> void;

private:
    struct TimedFrame {
        PVideoFrame frame;
        REFERENCE_TIME time;
    };

    std::deque<TimedFrame> _frameBuffer;

    std::mutex _bufferMutex;
    std::condition_variable _bufferCondition;

    std::atomic<bool> _draining;

    int _formatIndex;
    const VideoInfo *_videoInfo;
};
