#pragma once

#include "pch.h"
#include "format.h"


class FrameHandler {
public:
    static auto WriteSample(const Format::VideoFormat &format, const PVideoFrame srcFrame, BYTE *dstBuffer, IScriptEnvironment *avsEnv) -> void;

    auto GetNearestFrame(REFERENCE_TIME frameTime) -> PVideoFrame;
    auto CreateFrame(const Format::VideoFormat &format, REFERENCE_TIME frameTime, const BYTE *srcBuffer, IScriptEnvironment *avsEnv) -> void;
    auto GarbageCollect(REFERENCE_TIME min, REFERENCE_TIME max) -> void;
    auto Flush() -> void;

private:
    struct TimedFrame {
        PVideoFrame frame;
        REFERENCE_TIME time;
    };

    std::deque<TimedFrame> _buffer;
    std::shared_mutex _bufferMutex;
};
