// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"
#include "hdr.h"
#include "util.h"


namespace AvsFilter {

class CAviSynthFilter;

class FrameHandler {
public:
    explicit FrameHandler(CAviSynthFilter &filter);

    auto AddInputSample(IMediaSample *inSample) -> HRESULT;
    auto GetSourceFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame;
    auto Flush(const std::function<void ()> &interim) -> void;
    auto StartWorkerThread() -> void;
    auto StopWorkerThreads() -> void;
    auto GetInputBufferSize() const -> int;
    auto GetSourceFrameNb() const -> int;
    auto GetOutputFrameNb() const -> int;
    auto GetCurrentInputFrameRate() const -> int;
    auto GetCurrentOutputFrameRate() const -> int;

private:
    template <typename ...Mutexes>
    class MultiLock {
    public:
        explicit MultiLock(Mutexes &...mutexes)
            : _mutexes(mutexes...) {
        }

        MultiLock(const MultiLock &) = delete;
        MultiLock &operator=(const MultiLock &) = delete;

        auto lock() const noexcept -> void {
            std::apply([](Mutexes &...mutexes) {
                std::lock(mutexes...);
            }, _mutexes);
        }

        auto unlock() const noexcept -> void {
            std::apply([](Mutexes &...mutexes) {
                (..., mutexes.unlock());
            }, _mutexes);
        }

    private:
        std::tuple<Mutexes &...> _mutexes;
    };

    struct SourceFrameInfo {
        int frameNb;
        PVideoFrame avsFrame;
        REFERENCE_TIME startTime;
        UniqueMediaTypePtr mediaTypePtr;
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

    auto ResetInputProperties() -> void;
    auto ResetOutputProperties() -> void;
    auto PrepareForDelivery(PVideoFrame scriptFrame, ATL::CComPtr<IMediaSample> &outSample, REFERENCE_TIME outputStartTime, REFERENCE_TIME outputStopTime) const -> bool;
    auto ProcessOutputSamples() -> void;
    auto GarbageCollect(int srcFrameNb) -> void;
    auto RefreshInputFrameRates(int frameNb, REFERENCE_TIME startTime) -> void;
    auto RefreshOutputFrameRates(int frameNb, REFERENCE_TIME startTime) -> void;

    static constexpr const REFERENCE_TIME INVALID_REF_TIME = -1;
    static constexpr const int NUM_SRC_FRAMES_PER_PROCESSING = 3;

    CAviSynthFilter &_filter;

    std::map<int, SourceFrameInfo> _sourceFrames;

    mutable std::shared_mutex _sourceFramesMutex;
    std::shared_mutex _flushMutex;

    std::condition_variable_any _addInputSampleCv;
    std::condition_variable_any _newSourceFrameCv;
    std::condition_variable_any _flushMasterCv;

    std::atomic<int> _maxRequestedFrameNb;
    int _nextSourceFrameNb;
    int _nextProcessSrcFrameNb;
    int _nextOutputFrameNb;
    REFERENCE_TIME _nextOutputFrameStartTime;

    std::thread _outputThread;

    std::atomic<bool> _isFlushing;
    std::atomic<bool> _isStopping;
    std::atomic<bool> _isOutputThreadPaused;

    int _frameRateCheckpointInputSampleNb;
    REFERENCE_TIME _frameRateCheckpointInputSampleStartTime;
    int _frameRateCheckpointOutputFrameNb;
    REFERENCE_TIME _frameRateCheckpointOutputFrameStartTime;
    int _currentInputFrameRate;
    int _currentOutputFrameRate;
};

}
