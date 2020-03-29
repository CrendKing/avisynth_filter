#include "pch.h"
#include "frame_handler.h"


auto FrameHandler::WriteSample(const Format::VideoFormat &format, const PVideoFrame srcFrame, BYTE *dstBuffer, IScriptEnvironment *avsEnv) -> void {
    const BYTE *srcSlices[] = { srcFrame->GetReadPtr(), srcFrame->GetReadPtr(PLANAR_U), srcFrame->GetReadPtr(PLANAR_V) };
    const int srcStrides[] = { srcFrame->GetPitch(), srcFrame->GetPitch(PLANAR_U), srcFrame->GetPitch(PLANAR_V) };

    Format::CopyToOutput(format.definition, srcSlices, srcStrides, dstBuffer, format.bmi.biWidth, srcFrame->GetRowSize(), format.bmi.biHeight, avsEnv);
}

auto FrameHandler::GetNearestFrame(REFERENCE_TIME frameTime) -> PVideoFrame {
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

    const std::shared_lock<std::shared_mutex> lock(_bufferMutex);

    const TimedFrame *ret = &_buffer.back();

    uint8_t dbgFromBack = 1;
    if (frameTime >= _buffer.back().time) {
        for (const TimedFrame &buf : _buffer) {
            if (frameTime >= buf.time) {
                ret = &buf;
                dbgFromBack = 2;
                break;
            }
        }
    }

    DbgLog((LOG_TRACE, 2, "GetFrame at: %10lli Queue size: %2u Back: %10lli Front: %10lli Served(%u): %10lli",
           frameTime, _buffer.size(), _buffer.back().time, _buffer.front().time, dbgFromBack, ret->time));

    return ret->frame;
}

auto FrameHandler::CreateFrame(const Format::VideoFormat &format, REFERENCE_TIME frameTime, const BYTE *srcBuffer, IScriptEnvironment *avsEnv) -> void {
    const PVideoFrame frame = avsEnv->NewVideoFrame(format.videoInfo, sizeof(__m128i));

    BYTE *dstSlices[] = { frame->GetWritePtr(), frame->GetWritePtr(PLANAR_U), frame->GetWritePtr(PLANAR_V) };
    const int dstStrides[] = { frame->GetPitch(), frame->GetPitch(PLANAR_U), frame->GetPitch(PLANAR_V) };

    Format::CopyFromInput(format.definition, srcBuffer, format.bmi.biWidth, dstSlices, dstStrides, frame->GetRowSize(), format.bmi.biHeight, avsEnv);

    const std::unique_lock<std::shared_mutex> lock(_bufferMutex);

    if (_buffer.empty() || _buffer.front().time < frameTime) {
        _buffer.emplace_front(TimedFrame { frame, frameTime });
    } else {
        _buffer.emplace_back(TimedFrame { frame, frameTime });
    }
}

auto FrameHandler::GarbageCollect(REFERENCE_TIME min, REFERENCE_TIME max) -> void {
    const std::unique_lock<std::shared_mutex> lock(_bufferMutex);

    const size_t dbgPreSize = _buffer.size();

    while (_buffer.size() > 1 && _buffer.back().time < min) {
        _buffer.pop_back();
    }

    while (_buffer.size() > 1 && _buffer.front().time > max) {
        _buffer.pop_front();
    }

    DbgLog((LOG_TRACE, 2, "Buffer GC: %10lli ~ %10lli Pre size: %2u Post size: %2u", min, max, dbgPreSize, _buffer.size()));
}

auto FrameHandler::Flush() -> void {
    const std::unique_lock<std::shared_mutex> lock(_bufferMutex);

    _buffer.clear();
}