// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"
#include "gatekeeper.h"
#include "hdr.h"


namespace AvsFilter {

class CAviSynthFilter;

class FrameHandler {
public:
    explicit FrameHandler(CAviSynthFilter &filter);

    auto AddInputSample(IMediaSample *inSample) -> HRESULT;
    auto GetSourceFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame;
    auto Flush(bool isStopping, const std::function<void ()> &interim) -> void;
    auto StartWorkerThread() -> void;
    auto StopWorkerThreads() -> void;
    auto GetInputBufferSize() const -> int;
    auto GetSourceFrameNb() const -> int;
    auto GetOutputFrameNb() const -> int;
    auto GetCurrentInputFrameRate() const -> int;
    auto GetCurrentOutputFrameRate() const -> int;

private:
    struct SourceFrameInfo {
        int frameNb;
        PVideoFrame avsFrame;
        REFERENCE_TIME startTime;
        HDRSideData hdrSideData;
    };

    enum class State {
        Normal,
        Flushing,
        Stopping
    };

    static auto RefreshFrameRatesTemplate(int sampleNb, REFERENCE_TIME startTime,
                                          int &checkpointSampleNb, REFERENCE_TIME &checkpointStartTime,
                                          int &currentFrameRate) -> void;

    auto Reset() -> void;
    auto PrepareForDelivery(ATL::CComPtr<IMediaSample> &outSample, REFERENCE_TIME outputStartTime, REFERENCE_TIME outputStopTime) const -> bool;
    auto ProcessOutputSamples() -> void;
    auto GarbageCollect(int srcFrameNb) -> void;
    auto RefreshInputFrameRates(int frameNb, REFERENCE_TIME startTime) -> void;
    auto RefreshOutputFrameRates(int frameNb, REFERENCE_TIME startTime) -> void;

    static constexpr const int NUM_SRC_FRAMES_PER_PROCESSING = 3;

    CAviSynthFilter &_filter;

    std::map<int, SourceFrameInfo> _sourceFrames;

    mutable std::shared_mutex _sourceFramesMutex;
    std::mutex _flushMutex;

    std::condition_variable_any _addInputSampleCv;
    std::condition_variable_any _newSourceFrameCv;

    int _maxRequestedFrameNb;
    int _nextSourceFrameNb;
    int _nextProcessSrcFrameNb;
    int _nextOutputFrameNb;
    REFERENCE_TIME _nextOutputFrameStartTime;

    std::thread _outputThread;

    Gatekeeper _flushGatekeeper;
    std::atomic<State> _state;

    int _frameRateCheckpointInputSampleNb;
    REFERENCE_TIME _frameRateCheckpointInputSampleStartTime;
    int _frameRateCheckpointOutputFrameNb;
    REFERENCE_TIME _frameRateCheckpointOutputFrameStartTime;
    int _currentInputFrameRate;
    int _currentOutputFrameRate;
};

}
