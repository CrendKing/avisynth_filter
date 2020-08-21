#include "pch.h"
#include "frame_handler.h"
#include "logging.h"


auto FrameHandler::WriteSample(const Format::VideoFormat &format, const PVideoFrame srcFrame, BYTE *dstBuffer, IScriptEnvironment *avsEnv) -> void {
    const BYTE *srcSlices[] = { srcFrame->GetReadPtr(), srcFrame->GetReadPtr(PLANAR_U), srcFrame->GetReadPtr(PLANAR_V) };
    const int srcStrides[] = { srcFrame->GetPitch(), srcFrame->GetPitch(PLANAR_U), srcFrame->GetPitch(PLANAR_V) };

    Format::CopyToOutput(format, srcSlices, srcStrides, dstBuffer, srcFrame->GetRowSize(), srcFrame->GetHeight(), avsEnv);
}

FrameHandler::FrameHandler()
    : _flushOnNextFrame(false)
    , _maxAccessedFrameNb(0)
    , _minAccessedFrameNb(INT_MAX) {
}

auto FrameHandler::GetNearestFrame(int frameNb) -> PVideoFrame {
    const std::unique_lock<std::mutex> lock(_bufferMutex);

    auto iter = _buffer.cbegin();
    uint8_t frameCacheType = 1;
    if (frameNb >= iter->first) {
        while (true) {
            if (iter == _buffer.cend()) {
                --iter;
                frameCacheType = 4;
                break;
            } else if (frameNb < iter->first) {
                --iter;
                frameCacheType = 3;
                break;
            } else if (frameNb == iter->first) {
                frameCacheType = 2;
                break;
            }
            ++iter;
        }
    }

    _maxAccessedFrameNb = max(frameNb, _maxAccessedFrameNb);
    _minAccessedFrameNb = min(frameNb, _minAccessedFrameNb);

    Log("GetFrame at: %6i Queue size: %2u Back: %6i Front: %6i Served(%u): %6i Max: %6i Min: %6i",
        frameNb, _buffer.size(), _buffer.cbegin()->first, _buffer.crbegin()->first, frameCacheType, iter->first, _maxAccessedFrameNb, _minAccessedFrameNb);

    return iter->second;
}

auto FrameHandler::CreateFrame(const Format::VideoFormat &format, int frameNb, const BYTE *srcBuffer, IScriptEnvironment *avsEnv) -> void {
    const PVideoFrame frame = avsEnv->NewVideoFrame(format.videoInfo, sizeof(__m128i));

    BYTE *dstSlices[] = { frame->GetWritePtr(), frame->GetWritePtr(PLANAR_U), frame->GetWritePtr(PLANAR_V) };
    const int dstStrides[] = { frame->GetPitch(), frame->GetPitch(PLANAR_U), frame->GetPitch(PLANAR_V) };

    Format::CopyFromInput(format, srcBuffer, dstSlices, dstStrides, frame->GetRowSize(), frame->GetHeight(), avsEnv);

    const std::unique_lock<std::mutex> lock(_bufferMutex);

    if (_flushOnNextFrame) {
        _buffer.clear();
        _maxAccessedFrameNb = 0;
        _flushOnNextFrame = false;
    }

    _buffer[frameNb] = frame;
    _minAccessedFrameNb = frameNb;
}

auto FrameHandler::GarbageCollect(int minFrameNb, int maxFrameNb) -> void {
    const std::unique_lock<std::mutex> lock(_bufferMutex);

    const size_t dbgPreSize = _buffer.size();

    // make sure there is always at least one frame in buffer for AviSynth prefetcher to get

    while (_buffer.size() > 1 && _buffer.cbegin()->first < minFrameNb) {
        _buffer.erase(_buffer.cbegin());
    }

    while (_buffer.size() > 1 && _buffer.crbegin()->first > maxFrameNb) {
        _buffer.erase(--_buffer.cend());
    }

    Log("Buffer GC: %6i ~ %6i Pre size: %2u Post size: %2u", minFrameNb, maxFrameNb, dbgPreSize, _buffer.size());
}

auto FrameHandler::Flush() -> void {
    const std::unique_lock<std::mutex> lock(_bufferMutex);

    _buffer.clear();
}

auto FrameHandler::FlushOnNextFrame() -> void {
    const std::unique_lock<std::mutex> lock(_bufferMutex);

    _flushOnNextFrame = true;
}

auto FrameHandler::GetBufferSize() const -> int {
    const std::unique_lock<std::mutex> lock(_bufferMutex);

    return static_cast<int>(_buffer.size());
}

auto FrameHandler::GetMaxAccessedFrameNb() const -> int {
    const std::unique_lock<std::mutex> lock(_bufferMutex);

    return _maxAccessedFrameNb;
}

auto FrameHandler::GetMinAccessedFrameNb() const -> int {
    const std::unique_lock<std::mutex> lock(_bufferMutex);

    return _minAccessedFrameNb;
}