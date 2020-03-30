#pragma once

#include "pch.h"
#include "format.h"


class FrameHandler {
public:
    static auto WriteSample(const Format::VideoFormat &format, const PVideoFrame srcFrame, BYTE *dstBuffer, IScriptEnvironment *avsEnv) -> void;

    FrameHandler();

    auto GetNearestFrame(REFERENCE_TIME frameTime) -> PVideoFrame;
    auto CreateFrame(const Format::VideoFormat &format, REFERENCE_TIME frameTime, const BYTE *srcBuffer, IScriptEnvironment *avsEnv) -> void;
    auto GarbageCollect(REFERENCE_TIME min, REFERENCE_TIME max) -> void;
    auto Flush() -> void;

    auto GetBufferSize() -> int;
    auto GetAheadOvertime()->REFERENCE_TIME;
    auto GetBackOvertime() -> REFERENCE_TIME;

private:
    struct TimedFrame {
        PVideoFrame frame;
        REFERENCE_TIME time;
    };

    std::deque<TimedFrame> _buffer;
    std::mutex _bufferMutex;

    REFERENCE_TIME _aheadOvertime;
    REFERENCE_TIME _backOvertime;
};
