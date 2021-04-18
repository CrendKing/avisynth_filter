// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"
#include "hdr.h"


namespace AvsFilter {

class CAviSynthFilter;

class FrameHandler {
public:
    explicit FrameHandler(CAviSynthFilter &filter);

    DISABLE_COPYING(FrameHandler)

    auto AddInputSample(IMediaSample *inSample) -> HRESULT;
    auto GetSourceFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame;
    auto BeginFlush() -> void;
    auto EndFlush(const std::function<void ()> &interim) -> void;
    auto StartWorkerThread() -> void;
    auto StopWorkerThreads() -> void;
    auto GetInputBufferSize() const -> int;
    constexpr auto GetSourceFrameNb() const -> int { return _nextSourceFrameNb; }
    constexpr auto GetOutputFrameNb() const -> int { return _nextOutputFrameNb; }
    constexpr auto GetCurrentInputFrameRate() const -> int { return _currentInputFrameRate; }
    constexpr auto GetCurrentOutputFrameRate() const -> int { return _currentOutputFrameRate; }

private:
    template <typename... Mutexes>
    class MultiLock {
    public:
        explicit MultiLock(Mutexes&... mutexes)
            : _mutexes(mutexes...) {
        }

        MultiLock(const MultiLock &) = delete;
        auto operator=(const MultiLock &) -> MultiLock & = delete;

        constexpr auto lock() const noexcept -> void {
            std::apply([](Mutexes&... mutexes) {
                std::lock(mutexes...);
            }, _mutexes);
        }

        auto unlock() const noexcept -> void {
            std::apply([](Mutexes&... mutexes) {
                (..., mutexes.unlock());
            }, _mutexes);
        }

    private:
        std::tuple<Mutexes&...> _mutexes;
    };

    struct SourceFrameInfo {
        int frameNb;
        PVideoFrame avsFrame;
        REFERENCE_TIME startTime;
        std::shared_ptr<HDRSideData> hdrSideData;
    };

    static auto RefreshFrameRatesTemplate(int sampleNb, REFERENCE_TIME startTime,
                                          int &checkpointSampleNb, REFERENCE_TIME &checkpointStartTime,
                                          int &currentFrameRate) -> void;

    auto ResetInput() -> void;
    auto ResetOutput() -> void;
    auto PrepareOutputSample(ATL::CComPtr<IMediaSample> &sample, REFERENCE_TIME startTime, REFERENCE_TIME stopTime) const -> bool;
    auto WorkerProc() -> void;
    auto GarbageCollect(int srcFrameNb) -> void;
    auto RefreshInputFrameRates(int frameNb, REFERENCE_TIME startTime) -> void;
    auto RefreshOutputFrameRates(int frameNb, REFERENCE_TIME startTime) -> void;

    static constexpr const REFERENCE_TIME INVALID_REF_TIME = -1;
    static constexpr const int NUM_SRC_FRAMES_PER_PROCESSING = 3;

    CAviSynthFilter &_filter;

    std::map<int, SourceFrameInfo> _sourceFrames;

    mutable std::shared_mutex _mutex;

    std::condition_variable_any _addInputSampleCv;
    std::condition_variable_any _newSourceFrameCv;

    std::atomic<int> _maxRequestedFrameNb;
    int _nextSourceFrameNb;
    int _nextProcessSrcFrameNb;
    int _nextOutputFrameNb;
    REFERENCE_TIME _nextOutputFrameStartTime;

    std::thread _workerThread;

    std::atomic<bool> _isFlushing = false;
    std::atomic<bool> _isStoppingWorker = false;
    std::atomic<bool> _isWorkerLatched = false;

    int _frameRateCheckpointInputSampleNb;
    REFERENCE_TIME _frameRateCheckpointInputSampleStartTime;
    int _frameRateCheckpointOutputFrameNb;
    REFERENCE_TIME _frameRateCheckpointOutputFrameStartTime;
    int _currentInputFrameRate;
    int _currentOutputFrameRate;
};

}
