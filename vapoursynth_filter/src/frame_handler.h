// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "hdr.h"


namespace SynthFilter {

class CSynthFilter;

class FrameHandler {
public:
    explicit FrameHandler(CSynthFilter &filter);

    DISABLE_COPYING(FrameHandler)

    auto AddInputSample(IMediaSample *inputSample) -> HRESULT;
    auto GetSourceFrame(int frameNb) -> const VSFrameRef *;
    auto BeginFlush() -> void;
    auto EndFlush(const std::function<void ()> &interim) -> void;
    auto Start() -> void;
    auto Stop() -> void;
    auto GetInputBufferSize() const -> int;
    constexpr auto GetSourceFrameNb() const -> int { return _nextSourceFrameNb; }
    constexpr auto GetOutputFrameNb() const -> int { return _nextOutputFrameNb; }
    constexpr auto GetDeliveryFrameNb() const -> int { return _nextDeliveryFrameNb; }
    constexpr auto GetCurrentInputFrameRate() const -> int { return _currentInputFrameRate; }
    constexpr auto GetCurrentOutputFrameRate() const -> int { return _currentOutputFrameRate; }

private:
    struct SourceFrameInfo {
        ~SourceFrameInfo();

        VSFrameRef *frame;
        REFERENCE_TIME startTime;
        std::shared_ptr<HDRSideData> hdrSideData;
    };

    struct OutputFrameData {
        ~OutputFrameData();

        int sourceFrameNb;
        std::shared_ptr<HDRSideData> hdrSideData;
        const VSFrameRef *frame = nullptr;
    };

    static auto VS_CC VpsGetFrameCallback(void *userData, const VSFrameRef *f, int n, VSNodeRef *node, const char *errorMsg) -> void;
    static auto RefreshFrameRatesTemplate(int sampleNb, REFERENCE_TIME startTime,
                                          int &checkpointSampleNb, REFERENCE_TIME &checkpointStartTime,
                                          int &currentFrameRate) -> void;

    auto ResetInput() -> void;
    auto PrepareOutputSample(ATL::CComPtr<IMediaSample> &sample, int frameNb, OutputFrameData &data) -> bool;
    auto WorkerProc() -> void;
    auto GarbageCollect(int srcFrameNb) -> void;
    auto ChangeOutputFormat() -> bool;
    auto RefreshInputFrameRates(int frameNb, REFERENCE_TIME startTime) -> void;
    auto RefreshOutputFrameRates(int frameNb, REFERENCE_TIME startTime) -> void;

    static constexpr const int NUM_SRC_FRAMES_PER_PROCESSING = 2;
    static constexpr const char *VS_PROP_NAME_ABS_TIME = "_AbsoluteTime";
    static constexpr const char *VS_PROP_NAME_DURATION_NUM = "_DurationNum";
    static constexpr const char *VS_PROP_NAME_DURATION_DEN = "_DurationDen";

    CSynthFilter &_filter;

    std::map<int, SourceFrameInfo> _sourceFrames;
    std::map<int, OutputFrameData> _outputFrames;

    mutable std::shared_mutex _sourceMutex;
    std::shared_mutex _outputMutex;

    std::condition_variable_any _addInputSampleCv;
    std::condition_variable_any _newSourceFrameCv;

    std::condition_variable_any _deliverSampleCv;
    std::condition_variable_any _flushOutputSampleCv;

    int _nextSourceFrameNb;
    int _nextProcessSourceFrameNb;
    int _nextOutputFrameNb;
    std::atomic<int> _lastUsedSourceFrameNb;
    bool _notifyChangedOutputMediaType = false;
    int _nextDeliveryFrameNb;

    std::thread _workerThread;

    std::atomic<bool> _isFlushing = false;
    std::atomic<bool> _isStopping = false;
    std::atomic<bool> _isWorkerLatched = false;

    int _frameRateCheckpointInputSampleNb;
    REFERENCE_TIME _frameRateCheckpointInputSampleStartTime;
    int _frameRateCheckpointOutputFrameNb;
    REFERENCE_TIME _frameRateCheckpointOutputFrameStartTime;
    int _currentInputFrameRate;
    int _currentOutputFrameRate;
};

}
