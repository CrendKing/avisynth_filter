#pragma once

#include "pch.h"
#include "barrier.h"
#include "side_data.h"


namespace AvsFilter {

class CAviSynthFilter;

class FrameHandler {
public:
    explicit FrameHandler(CAviSynthFilter &filter);

    auto AddInputSample(IMediaSample *inSample) -> void;
    auto GetSourceFrame(int frameNb) -> PVideoFrame;

    auto BeginFlush() -> void;
    auto EndFlush() -> void;

    auto StartWorkerThreads() -> void;
    auto StopWorkerThreads() -> void;

    auto GetInputBufferSize() const -> int;
    auto GetOutputBufferSize() const -> int;
    auto GetSourceFrameNb() const -> int;
    auto GetOutputFrameNb() const -> int;
    auto GetDeliveryFrameNb() const -> int;

private:
    struct SourceFrameInfo {
        int frameNb;
        IMediaSample *sample;
        PVideoFrame avsFrame;
        REFERENCE_TIME startTime;
        HDRSideData hdrSideData;
        int refCount;
    };

    struct OutputFrameInfo {
        int frameNb;
        REFERENCE_TIME startTime;
        REFERENCE_TIME stopTime;
        SourceFrameInfo *srcFrameInfo;
    };

    struct DeliverySampleInfo {
        int sampleNb;
        IMediaSample *sample;

        bool operator<(const DeliverySampleInfo &rhs) const {
            return sampleNb > rhs.sampleNb;
        }
    };

    auto ProcessInputSamples() -> void;
    auto ProcessOutputSamples() -> void;
    auto GarbageCollect(int srcFrameNb) -> void;

    CAviSynthFilter &_filter;

    std::map<int, SourceFrameInfo> _sourceFrames;
    std::deque<OutputFrameInfo> _outputFrames;

    mutable std::mutex _sourceFramesMutex;
    mutable std::mutex _outputFramesMutex;
    std::mutex _deliveryQueueMutex;

    std::condition_variable _addInputSampleCv;
    std::condition_variable _newSourceFrameCv;
    std::condition_variable _sourceFrameAvailCv;
    std::condition_variable _outputFramesCv;
    std::condition_variable _deliveryCv;

    int _maxRequestedFrameNb;
    int _nextSourceFrameNb;
    int _processInputFrameNb;
    int _nextOutputFrameNb;
    REFERENCE_TIME _nextOutputFrameStartTime;
    int _deliveryFrameNb;

    std::vector<std::thread> _inputWorkerThreads;
    Barrier _inputFlushBarrier;
    std::vector<std::thread> _outputWorkerThreads;
    Barrier _outputFlushBarrier;
    bool _stopWorkerThreads;
    bool _isFlushing;
};

}