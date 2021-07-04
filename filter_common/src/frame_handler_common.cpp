// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"

#include "constants.h"
#include "frame_handler.h"
#include "filter.h"


namespace SynthFilter {

FrameHandler::FrameHandler(CSynthFilter &filter)
    : _filter(filter) {
    ResetInput();
}

FrameHandler::~FrameHandler() {
    if (_workerThread.joinable()) {
        _isStopping = true;

        BeginFlush();
        EndFlush([]() -> void {
            MainFrameServer::GetInstance().StopScript();
        });

        _workerThread.join();
    }
}

auto FrameHandler::StartWorker() -> void {
    if (!_workerThread.joinable()) {
        _isStopping = false;
        _workerThread = std::thread(&FrameHandler::WorkerProc, this);
    }
}

auto FrameHandler::UpdateExtraSourceBuffer() -> void {
    if (const int sourceAvgFps = MainFrameServer::GetInstance().GetSourceAvgFrameRate();
        _currentInputFrameRate > 0 && _nextSourceFrameNb % (sourceAvgFps / FRAME_RATE_SCALE_FACTOR) == 0) {
        if (const double ratio = static_cast<double>(_currentInputFrameRate) / sourceAvgFps;
            ratio < 1 - EXTRA_SOURCE_BUFFER_CHANGE_THRESHOLD) {
            _extraSourceBuffer = min(_extraSourceBuffer, MAXIMUM_EXTRA_SOURCE_BUFFER) + 2;
        } else if (ratio > 1 + EXTRA_SOURCE_BUFFER_CHANGE_THRESHOLD) {
            _extraSourceBuffer = max(_extraSourceBuffer, 1) - 1;
        }
    }
}

auto FrameHandler::GetInputBufferSize() const -> int {
    const std::shared_lock sharedSourceLock(_sourceMutex);

    return static_cast<int>(_sourceFrames.size());
}

auto FrameHandler::RefreshFrameRatesTemplate(int sampleNb, int &checkpointSampleNb, DWORD &checkpointTime, int &currentFrameRate) -> void {
    const DWORD currentTime = timeGetTime();
    bool reachCheckpoint = checkpointTime == 0;

    if (const REFERENCE_TIME elapsedRefTime = currentTime - checkpointTime; elapsedRefTime >= STATUS_PAGE_TIMER_INTERVAL_MS) {
        currentFrameRate = static_cast<int>(llMulDiv((static_cast<LONGLONG>(sampleNb) - checkpointSampleNb) * FRAME_RATE_SCALE_FACTOR, MILLISECONDS, elapsedRefTime, 0));
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
    for (decltype(_sourceFrames)::const_iterator iter = _sourceFrames.begin(); iter != sourceEnd && iter->first <= srcFrameNb; iter = _sourceFrames.begin()) {
        _sourceFrames.erase(iter);
    }

    _addInputSampleCv.notify_all();

    Environment::GetInstance().Log(L"GarbageCollect frames until %6i pre size %3zu post size %3zu", srcFrameNb, dbgPreSize, _sourceFrames.size());
}

auto FrameHandler::ChangeOutputFormat() -> bool {
    Environment::GetInstance().Log(L"Upstream proposes to change input format: name %s, width %5li, height %5li",
                                   _filter._inputVideoFormat.pixelFormat->name, _filter._inputVideoFormat.bmi.biWidth, _filter._inputVideoFormat.bmi.biHeight);

    _filter.StopStreaming();

    BeginFlush();
    EndFlush([this]() -> void {
        MainFrameServer::GetInstance().ReloadScript(_filter.m_pInput->CurrentMediaType(), true);
    });

    _filter._inputMediaTypeChanged = false;
    _filter._reloadScript = false;

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
            Environment::GetInstance().Log(L"Attempt to reconnect output pin with media type: output %s result %i", Format::LookupMediaSubtype(outputMediaType.subtype)->name, result);

            if (result) {
                _filter.m_pOutput->SetMediaType(&outputMediaType);
                _filter._outputVideoFormat = Format::GetVideoFormat(outputMediaType, &MainFrameServer::GetInstance());
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
