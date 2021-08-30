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
    auto GetSourceFrame(int frameNb) -> const VSFrame *;
    auto BeginFlush() -> void;
    auto EndFlush(const std::function<void ()> &interim) -> void;
    auto StartWorker() -> void;
    auto GetInputBufferSize() const -> int;
    constexpr auto GetSourceFrameNb() const -> int { return _nextSourceFrameNb; }
    constexpr auto GetOutputFrameNb() const -> int { return _nextOutputFrameNb; }
    constexpr auto GetDeliveryFrameNb() const -> int { return _nextDeliveryFrameNb; }
    constexpr auto GetCurrentInputFrameRate() const -> int { return _currentInputFrameRate; }
    constexpr auto GetCurrentOutputFrameRate() const -> int { return _currentOutputFrameRate; }
    constexpr auto GetCurrentDeliveryFrameRate() const -> int { return _currentDeliveryFrameRate; }

private:
    struct SourceFrameInfo {
        ~SourceFrameInfo();

        VSFrame *frame;
        REFERENCE_TIME startTime;
        std::unique_ptr<HDRSideData> hdrSideData;
    };

    static auto VS_CC VpsGetFrameCallback(void *userData, const VSFrame *f, int n, VSNode *node, const char *errorMsg) -> void;
    static auto RefreshFrameRatesTemplate(int sampleNb, int &checkpointSampleNb, DWORD &checkpointStartTime, int &currentFrameRate) -> void;

    auto ResetInput() -> void;
    auto PrepareOutputSample(ATL::CComPtr<IMediaSample> &outSample, int outputFrameNb, const VSFrame *outputFrame, int sourceFrameNb) -> bool;
    auto WorkerProc() -> void;
    auto GarbageCollect(int srcFrameNb) -> void;
    auto ChangeOutputFormat() -> bool;
    auto UpdateExtraSourceBuffer() -> void;
    auto RefreshInputFrameRates(int frameNb) -> void;
    auto RefreshOutputFrameRates(int frameNb) -> void;
    auto RefreshDeliveryFrameRates(int frameNb) -> void;

    static constexpr const int NUM_SRC_FRAMES_PER_PROCESSING = 2;

    CSynthFilter &_filter;

    std::map<int, SourceFrameInfo> _sourceFrames;
    std::map<int, const VSFrame *> _outputFrames;

    mutable std::shared_mutex _sourceMutex;
    std::shared_mutex _outputMutex;

    std::condition_variable_any _addInputSampleCv;
    std::condition_variable_any _newSourceFrameCv;
    std::condition_variable_any _deliverSampleCv;
    std::condition_variable_any _flushOutputSampleCv;

    int _nextSourceFrameNb;
    int _nextProcessSourceFrameNb;
    int _nextOutputFrameNb;
    REFERENCE_TIME _nextOutputFrameStartTime;
    std::atomic<int> _lastUsedSourceFrameNb;
    bool _notifyChangedOutputMediaType;
    int _nextDeliveryFrameNb;
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
