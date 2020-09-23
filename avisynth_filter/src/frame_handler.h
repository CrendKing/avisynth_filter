#pragma once

#include "pch.h"
#include "barrier.h"
#include "side_data.h"


namespace AvsFilter {

class CAviSynthFilter;

class FrameHandler {
public:
    explicit FrameHandler(CAviSynthFilter &filter);
    ~FrameHandler();

    auto AddInputSample(IMediaSample *inSample) -> void;
    auto GetSourceFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame;

    auto BeginFlush() -> void;
    auto EndFlush() -> void;

    auto StartWorkerThreads() -> void;
    auto StopWorkerThreads() -> void;

    auto GetInputBufferSize() const -> int;
    auto GetOutputBufferSize() const -> int;
    auto GetSourceFrameNb() const -> int;
    auto GetOutputFrameNb() const -> int;
    auto GetDeliveryFrameNb() const -> int;
    auto GetCurrentInputFrameRate() const -> int;
    auto GetCurrentOutputFrameRate() const -> int;
    auto GetInputWorkerThreadCount() const -> int;
    auto GetOutputWorkerThreadCount() const -> int;

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

    static auto RefreshInputFrameRatesTemplate(int sampleNb, REFERENCE_TIME startTime,
                                               int &checkpointSampleNb, REFERENCE_TIME &checkpointStartTime,
                                               int &currentFrameRate) -> void;

    auto Reset() -> void;
    auto ProcessInputSamples() -> void;
    auto ProcessOutputSamples() -> void;
    auto GarbageCollect(int srcFrameNb) -> void;
    auto RefreshInputFrameRates(int sampleNb, REFERENCE_TIME startTime) -> void;
    auto RefreshOutputFrameRates(int sampleNb, REFERENCE_TIME startTime) -> void;

    CAviSynthFilter &_filter;

    std::map<int, SourceFrameInfo> _sourceFrames;
    std::deque<OutputFrameInfo> _outputFrames;

    mutable std::mutex _sourceFramesMutex;
    mutable std::mutex _outputFramesMutex;
    std::mutex _deliveryQueueMutex;

    std::condition_variable _addInputSampleCv;
    std::condition_variable _newSourceFrameCv;

    // separate CV from new source CV for more efficient notify_all()
    // threads only wait on this CV when there is a famine from source filter
    std::condition_variable _sourceFrameAvailCv;

    std::condition_variable _outputFramesCv;
    std::condition_variable _deliveryCv;

    int _maxRequestedFrameNb;
    int _nextSourceFrameNb;
    int _processInputFrameNb;
    int _nextOutputFrameNb;
    int _deliveryFrameNb;
    REFERENCE_TIME _nextOutputFrameStartTime;

    std::vector<std::thread> _inputWorkerThreads;
    Barrier _inputFlushBarrier;
    std::vector<std::thread> _outputWorkerThreads;
    Barrier _outputFlushBarrier;
    bool _stopWorkerThreads;
    bool _isFlushing;

    int _frameRateCheckpointInputSampleNb;
    REFERENCE_TIME _frameRateCheckpointInputSampleStartTime;
    int _frameRateCheckpointOutputFrameNb;
    REFERENCE_TIME _frameRateCheckpointOutputFrameStartTime;
    int _currentInputFrameRate;
    int _currentOutputFrameRate;
};

}
