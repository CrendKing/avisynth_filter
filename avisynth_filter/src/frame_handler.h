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

    auto AddInputSample(IMediaSample *inSample) -> HRESULT;
    auto GetSourceFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame;

    auto Flush() -> void;

    auto StartWorkerThreads() -> void;
    auto StopWorkerThreads() -> void;

    auto GetInputBufferSize() const -> int;
    auto GetOutputBufferSize() const -> int;
    auto GetSourceFrameNb() const -> int;
    auto GetOutputFrameNb() const -> int;
    auto GetDeliveryFrameNb() const -> int;
    auto GetCurrentInputFrameRate() const -> int;
    auto GetCurrentOutputFrameRate() const -> int;
    auto GetOutputWorkerThreadCount() const -> int;

private:
    struct SourceFrameInfo {
        int frameNb;
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

    static auto RefreshFrameRatesTemplate(int sampleNb, REFERENCE_TIME startTime,
                                          int &checkpointSampleNb, REFERENCE_TIME &checkpointStartTime,
                                          int &currentFrameRate) -> void;

    auto Reset() -> void;
    auto ProcessOutputSamples() -> void;
    auto GarbageCollect(int srcFrameNb) -> void;
    auto RefreshInputFrameRates(const SourceFrameInfo &info) -> void;
    auto RefreshOutputFrameRates(const OutputFrameInfo &info) -> void;

    CAviSynthFilter &_filter;

    std::unordered_map<int, SourceFrameInfo> _sourceFrames;
    std::deque<OutputFrameInfo> _outputFrames;

    mutable std::shared_mutex _sourceFramesMutex;
    mutable std::shared_mutex _outputFramesMutex;
    std::mutex _deliveryMutex;

    std::condition_variable_any _addInputSampleCv;
    std::condition_variable_any _newSourceFrameCv;
    std::condition_variable_any _outputFramesCv;
    std::condition_variable _deliveryCv;

    int _maxRequestedFrameNb;
    int _nextSourceFrameNb;
    int _nextOutputFrameNb;
    int _nextDeliverFrameNb;
    REFERENCE_TIME _nextOutputFrameStartTime;

    std::vector<std::thread> _outputThreads;

    Barrier _flushBarrier;
    bool _stopThreads;
    bool _isFlushing;

    int _frameRateCheckpointInputSampleNb;
    REFERENCE_TIME _frameRateCheckpointInputSampleStartTime;
    int _frameRateCheckpointOutputFrameNb;
    REFERENCE_TIME _frameRateCheckpointOutputFrameStartTime;
    int _currentInputFrameRate;
    int _currentOutputFrameRate;
};

}
