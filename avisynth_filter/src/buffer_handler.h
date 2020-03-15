#pragma once

#include "pch.h"


class BufferHandler {
public:
    auto Reset(const CLSID &inFormat, const AVS_VideoInfo *videoInfo) -> void;
    auto GetNearestFrame(REFERENCE_TIME frameTime) -> AVS_VideoFrame *;
    auto CreateFrame(REFERENCE_TIME frameTime, const BYTE *srcBuffer, long srcUnitStride, AVS_ScriptEnvironment *avsEnv) -> void;
    auto WriteSample(const AVS_VideoFrame *srcFrame, BYTE *dstBuffer, long dstUnitStride, AVS_ScriptEnvironment *avsEnv) const -> void;
    auto GarbageCollect(REFERENCE_TIME streamTime) -> void;

private:
    struct FrameInfo {
        REFERENCE_TIME time;
        AVS_VideoFrame *frame;
    };

    std::deque<FrameInfo> _frameBuffer;
    std::shared_mutex _mutex;

    int _formatIndex;
    const AVS_VideoInfo *_videoInfo;
};
