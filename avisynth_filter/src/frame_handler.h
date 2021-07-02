// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "hdr.h"


namespace SynthFilter {

class CSynthFilter;

class FrameHandler {
public:
    explicit FrameHandler(CSynthFilter &filter);
    ~FrameHandler();

    DISABLE_COPYING(FrameHandler)

    auto AddInputSample(IMediaSample *inputSample) -> HRESULT;
    auto GetSourceFrame(int frameNb) -> PVideoFrame;
    auto BeginFlush() -> void;
    auto EndFlush(const std::function<void ()> &interim) -> void;
    auto StartWorker() -> void;
    auto GetInputBufferSize() const -> int;
    constexpr auto GetSourceFrameNb() const -> int { return _nextSourceFrameNb; }
    constexpr auto GetOutputFrameNb() const -> int { return _nextOutputFrameNb; }
    constexpr auto GetDeliveryFrameNb() const -> int { return _nextOutputFrameNb; }
    constexpr auto GetCurrentInputFrameRate() const -> int { return _currentInputFrameRate; }
    constexpr auto GetCurrentOutputFrameRate() const -> int { return _currentOutputFrameRate; }
    constexpr auto GetCurrentDeliveryFrameRate() const -> int { return _currentDeliveryFrameRate; }

private:
    struct SourceFrameInfo {
        PVideoFrame frame;
        REFERENCE_TIME startTime;
        std::unique_ptr<HDRSideData> hdrSideData;
    };

    static auto RefreshFrameRatesTemplate(int sampleNb, int &checkpointSampleNb, DWORD &checkpointTime, int &currentFrameRate) -> void;

    auto ResetInput() -> void;
    auto PrepareOutputSample(ATL::CComPtr<IMediaSample> &sample, REFERENCE_TIME startTime, REFERENCE_TIME stopTime) -> bool;
    auto WorkerProc() -> void;
    auto GarbageCollect(int srcFrameNb) -> void;
    auto ChangeOutputFormat() -> bool;
    auto UpdateExtraSourceBuffer() -> void;
    auto RefreshInputFrameRates(int frameNb) -> void;
    auto RefreshOutputFrameRates(int frameNb) -> void;
    auto RefreshDeliveryFrameRates(int frameNb) -> void;

    static constexpr const int NUM_SRC_FRAMES_PER_PROCESSING = 3;

    CSynthFilter &_filter;

    std::map<int, SourceFrameInfo> _sourceFrames;

    mutable std::shared_mutex _sourceMutex;

    std::condition_variable_any _addInputSampleCv;
    std::condition_variable_any _newSourceFrameCv;

    int _nextSourceFrameNb;
    std::atomic<int> _maxRequestedFrameNb;
    int _nextOutputFrameNb;
    REFERENCE_TIME _nextOutputFrameStartTime;
    bool _notifyChangedOutputMediaType;
    unsigned int _extraSourceBuffer;

    std::thread _workerThread;

    std::atomic<bool> _isFlushing = false;
    std::atomic<bool> _isStopping = false;
    std::atomic<bool> _isWorkerLatched = false;

    int _frameRateCheckpointInputSampleNb;
    DWORD _frameRateCheckpointInputSampleTime;
    int _frameRateCheckpointOutputFrameNb;
    DWORD _frameRateCheckpointOutputFrameTime;
    int _frameRateCheckpointDeliveryFrameNb;
    DWORD _frameRateCheckpointDeliveryFrameTime;
    int _currentInputFrameRate;
    int _currentOutputFrameRate;
    int _currentDeliveryFrameRate;
};

}
