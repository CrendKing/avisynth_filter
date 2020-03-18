#include "pch.h"
#include "buffer_handler.h"
#include "format.h"


BufferHandler::BufferHandler()
    : _draining(false)
    , _formatIndex(-1)
    , _videoInfo(nullptr) {
}

auto BufferHandler::Reset(const CLSID &inFormat, const VideoInfo *videoInfo) -> void {
    _formatIndex = Format::LookupInput(inFormat);
    _videoInfo = videoInfo;
    Flush();
}

auto BufferHandler::GetNearestFrame(REFERENCE_TIME frameTime) -> PVideoFrame {
    /*
    Instead of searching for a frame that's closest to the requested time, we look for the earliest frame after.
    For example, suppose we have 3 frames in buffer, whose time are 0, 10 and 20.
    When requested at time
        * 10 -> frame 2
        * 6  -> frame 1
        * 12 -> frame 2
        * 19 -> frame 2
        * 21 -> frame 3
    This design is meant to diversify the frames returned when AviSynth's timestamp is off from the filter graph,
    even though they still share the same fps. Because AviSynth works on frame number while DirectShow works on reference time.

    For instance, suppose the average time per frame is 1000. The first frame arrived from upstream at 500.
    These will be frame times in buffer: 500, 1500, 2500, etc.
    These will be the requested times: 0, 1000, 2000, etc.
    */

    std::unique_lock<std::mutex> lock(_bufferMutex);

    // block until the latest frame can cover the requested frame time
    _bufferCondition.wait(lock, [&] {
        if (_draining) {
            return true;
        }

        return !_frameBuffer.empty() && frameTime <= _frameBuffer.front().time;
    });

#ifdef LOGGING
    printf("Queue size : %2lli GetFrame at : %10lli Back: %10lli Front: %10lli Served", _frameBuffer.size(), frameTime, _frameBuffer.back().time, _frameBuffer.front().time);
#endif

    const TimedFrame *ret = nullptr;
    for (const TimedFrame &buf : _frameBuffer) {
        if (frameTime >= buf.time) {
#ifdef LOGGING
            printf("(1):");
#endif
            ret = &buf;
            break;
        }
    }

    if (ret == nullptr) {
#ifdef LOGGING
        printf("(2):");
#endif
        ret = &_frameBuffer.back();
    }

#ifdef LOGGING
    printf(" %10lli\n", ret->time);
    fflush(stdout);
#endif

    return ret->frame;
}

/*
"unit stride" means the stride in unit of the size for each pixel. For example, for 8-bit char-sized buffers,
x unit stride means the stride has x * 1 bytes. For word-sized buffers (10-bit, 16-bit, etc), x unit stride means x * 2 bytes.
*/

auto BufferHandler::CreateFrame(REFERENCE_TIME frameTime, const BYTE *srcBuffer, long srcUnitStride, long height, IScriptEnvironment *avsEnv) -> void {
    PVideoFrame frame = avsEnv->NewVideoFrame(*_videoInfo);

    BYTE *dstSlices[] = { frame->GetWritePtr(), frame->GetWritePtr(PLANAR_U), frame->GetWritePtr(PLANAR_V) };
    const int dstStrides[] = { frame->GetPitch(), frame->GetPitch(PLANAR_U) };

    Format::CopyFromInput(_formatIndex, srcBuffer, srcUnitStride, dstSlices, dstStrides, _videoInfo->width, height, avsEnv);

    {
        std::lock_guard<std::mutex> lock(_bufferMutex);
        _frameBuffer.emplace_front(TimedFrame { frame, frameTime });
    }
    _bufferCondition.notify_one();
}

auto BufferHandler::WriteSample(const PVideoFrame srcFrame, BYTE *dstBuffer, long dstUnitStride, long height, IScriptEnvironment *avsEnv) const -> void {
    const BYTE *srcSlices[] = { srcFrame->GetReadPtr(), srcFrame->GetReadPtr(PLANAR_U), srcFrame->GetReadPtr(PLANAR_V) };
    const int srcStrides[] = { srcFrame->GetPitch(), srcFrame->GetPitch(PLANAR_U) };

    Format::CopyToOutput(_formatIndex, srcSlices, srcStrides, dstBuffer, dstUnitStride, _videoInfo->width, height, avsEnv);
}

auto BufferHandler::GarbageCollect(REFERENCE_TIME frameTime) -> void {
    std::lock_guard<std::mutex> lock(_bufferMutex);

    while (_frameBuffer.size() > 1 && _frameBuffer.back().time < frameTime) {
        _frameBuffer.pop_back();
    }

#ifdef LOGGING
    printf("Buffer GC: %10lli Post size: %lli\n", frameTime, _frameBuffer.size());
    fflush(stdout);
#endif
}

auto BufferHandler::StartDraining() -> void {
    _draining = true;
    _bufferCondition.notify_one();
}

auto BufferHandler::StopDraining() -> void {
    _draining = false;
    Flush();
}

auto BufferHandler::Flush() -> void {
    std::lock_guard<std::mutex> lock(_bufferMutex);
    _frameBuffer.clear();
}