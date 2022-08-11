// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "constants.h"
#include "filter.h"


namespace SynthFilter {

FrameHandler::FrameHandler(CSynthFilter &filter)
    : _filter(filter) {
    ResetInput();
}

FrameHandler::~FrameHandler() {
    if (_workerThread.joinable()) {
        _isStopping = true;

        // the pairing BeginFlush() is in input pin's Inactive()
        EndFlush(nullptr);

        _workerThread.join();
    }
}

auto FrameHandler::StartWorker() -> void {
    if (!_workerThread.joinable()) {
        _isStopping = false;
        _workerThread = std::thread(&FrameHandler::WorkerProc, this);
    }
}

auto FrameHandler::UpdateExtraSrcBuffer() -> void {
    if (const int sourceAvgFps = MainFrameServer::GetInstance().GetSourceAvgFrameRate();
        _nextSourceFrameNb % (sourceAvgFps / FRAME_RATE_SCALE_FACTOR) == 0) {
        const double ratio = static_cast<double>(_currentInputFrameRate) / sourceAvgFps;
        Environment::GetInstance().Log(L"Source rate ratio %5f", ratio);
        if (ratio < 1 - EXTRA_SRC_BUFFER_CHANGE_THRESHOLD) {
            _extraSrcBuffer += Environment::GetInstance().GetExtraSrcBufferIncStep();
        } else if (ratio > 1 + EXTRA_SRC_BUFFER_CHANGE_THRESHOLD) {
            _extraSrcBuffer -= Environment::GetInstance().GetExtraSrcBufferDecStep();
        }

        _extraSrcBuffer = std::clamp(_extraSrcBuffer, Environment::GetInstance().GetMinExtraSrcBuffer(), Environment::GetInstance().GetMaxExtraSrcBuffer());
    }
}

auto FrameHandler::GetInputBufferSize() const -> int {
    const std::shared_lock sharedSourceLock(_sourceMutex);

    return static_cast<int>(_sourceFrames.size());
}

auto FrameHandler::RefreshFrameRatesTemplate(int sampleNb, int &checkpointSampleNb, std::chrono::steady_clock::time_point &checkpointTime, int &currentFrameRate) -> void {
    const std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
    bool reachCheckpoint = checkpointTime.time_since_epoch().count() == 0;

    if (const std::chrono::steady_clock::duration elapsed = currentTime - checkpointTime; elapsed >= STATUS_PAGE_TIMER_INTERVAL) {
        currentFrameRate = static_cast<int>(llMulDiv((static_cast<LONGLONG>(sampleNb) - checkpointSampleNb) * FRAME_RATE_SCALE_FACTOR, NANOSECONDS, std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count(), 0));
        reachCheckpoint = true;
    }

    if (reachCheckpoint) {
        checkpointSampleNb = sampleNb;
        checkpointTime = currentTime;
    }
}

auto FrameHandler::GarbageCollect(int srcFrameNb) -> void {
    const std::unique_lock uniqueSourceLock(_sourceMutex);

    const size_t dbgPreSize = _sourceFrames.size();

    // search for all previous frames in case of some source frames are never used
    // this could happen by plugins that decrease frame rate
    const auto sourceEnd = _sourceFrames.end();
    for (auto iter = _sourceFrames.begin(); iter != sourceEnd && iter->first <= srcFrameNb; iter = _sourceFrames.begin()) {
        _sourceFrames.erase(iter);
    }

    _addInputSampleCv.notify_all();

    Environment::GetInstance().Log(L"GarbageCollect frames until %6d pre size %3zd post size %3zd", srcFrameNb, dbgPreSize, _sourceFrames.size());
}

auto FrameHandler::ChangeOutputFormat() -> bool {
    Environment::GetInstance().Log(L"Upstream proposes to change input format: name %s, width %5ld, height %5ld",
                                   _filter._inputVideoFormat.pixelFormat->name,
                                   _filter._inputVideoFormat.bmi.biWidth,
                                   _filter._inputVideoFormat.bmi.biHeight);

    _filter.StopStreaming();

    BeginFlush();
    EndFlush([this]() -> void {
        AuxFrameServer::GetInstance().ReloadScript(_filter.m_pInput->CurrentMediaType(), true);
    });

    _filter._isInputMediaTypeChanged = false;
    _filter._needReloadScript = false;

    auto potentialOutputMediaTypes = _filter.InputToOutputMediaType(&_filter.m_pInput->CurrentMediaType());
    if (const auto newOutputMediaTypeIter = std::ranges::find_if(potentialOutputMediaTypes, [this](const CMediaType &outputMediaType) -> bool {
            /*
             * "QueryAccept (Downstream)" forces the downstream to use the new output media type as-is, which may lead to wrong rendering result
             * "ReceiveConnection" allows downstream to counter-propose suitable media type for the connection
             * after ReceiveConnection(), the next output sample should carry the new output media type, which is handled in PrepareOutputSample()
             */

            if (_isFlushing) {
                return false;
            }

            const bool result = SUCCEEDED(_filter.m_pOutput->GetConnected()->ReceiveConnection(_filter.m_pOutput, &outputMediaType));
            Environment::GetInstance().Log(L"Attempt to reconnect output pin with media type: output %s result %d",
                                           Format::LookupMediaSubtype(outputMediaType.subtype)->name,
                                           result);

            if (result) {
                _filter.m_pOutput->SetMediaType(&outputMediaType);
                _filter._outputVideoFormat = Format::GetVideoFormat(outputMediaType, &AuxFrameServer::GetInstance());
                _notifyChangedOutputMediaType = true;
            }

            return result;
        });
        newOutputMediaTypeIter == potentialOutputMediaTypes.end()) {
        Environment::GetInstance().Log(L"Downstream does not accept any of the new output media types");
        _filter.AbortPlayback(VFW_E_TYPE_NOT_ACCEPTED);
        return false;
    }

    _filter.StartStreaming();

    return true;
}

auto FrameHandler::RefreshInputFrameRates(int frameNb) -> void {
    RefreshFrameRatesTemplate(frameNb, _frameRateCheckpointInputSampleNb, _frameRateCheckpointInputSampleTime, _currentInputFrameRate);
}

auto FrameHandler::RefreshOutputFrameRates(int frameNb) -> void {
    RefreshFrameRatesTemplate(frameNb, _frameRateCheckpointOutputFrameNb, _frameRateCheckpointOutputFrameTime, _currentOutputFrameRate);
}

auto FrameHandler::RefreshDeliveryFrameRates(int frameNb) -> void {
    RefreshFrameRatesTemplate(frameNb, _frameRateCheckpointDeliveryFrameNb, _frameRateCheckpointDeliveryFrameTime, _currentDeliveryFrameRate);
}

}
