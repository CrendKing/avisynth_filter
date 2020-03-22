#pragma once

#include "pch.h"
#include "format.h"


class BufferHandler {
public:
    static auto WriteSample(const Format::MediaTypeInfo &format, const PVideoFrame srcFrame, BYTE *dstBuffer, IScriptEnvironment *avsEnv) -> void;

    auto GetNearestFrame(REFERENCE_TIME frameTime) -> PVideoFrame;
    auto CreateFrame(const Format::MediaTypeInfo &format, REFERENCE_TIME frameTime, const BYTE *srcBuffer, IScriptEnvironment *avsEnv) -> void;
    auto GarbageCollect(REFERENCE_TIME min, REFERENCE_TIME max) -> void;
    auto Flush() -> void;

private:
    struct TimedFrame {
        PVideoFrame frame;
        REFERENCE_TIME time;
    };

    std::deque<TimedFrame> _frameBuffer;
    std::shared_mutex _bufferMutex;
};
