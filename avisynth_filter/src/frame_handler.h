#pragma once

#include "pch.h"
#include "format.h"


class FrameHandler {
public:
    static auto WriteSample(const Format::VideoFormat &format, const PVideoFrame srcFrame, BYTE *dstBuffer, IScriptEnvironment *avsEnv) -> void;

    FrameHandler();

    auto GetNearestFrame(int frameNb) -> PVideoFrame;
    auto CreateFrame(const Format::VideoFormat &format, int frameNb, const BYTE *srcBuffer, IScriptEnvironment *avsEnv) -> void;
    auto GarbageCollect(int minFrameNb, int maxFrameNb) -> void;
    auto Flush() -> void;
    auto FlushOnNextFrame() -> void;

    auto GetBufferSize() const -> int;
    auto GetMaxAccessedFrameNb() const -> int;
    auto GetMinAccessedFrameNb() const -> int;

private:
    std::map<int, PVideoFrame> _buffer;
    mutable std::mutex _bufferMutex;
    bool _flushOnNextFrame;

    int _maxAccessedFrameNb;
    int _minAccessedFrameNb;
};
