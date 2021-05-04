// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "frame_handler.h"
#include "constants.h"
#include "filter.h"


namespace SynthFilter {

FrameHandler::FrameHandler(CSynthFilter &filter)
    : _filter(filter) {
    ResetInput();
}

auto FrameHandler::AddInputSample(IMediaSample *inputSample) -> HRESULT {
    HRESULT hr;

    _addInputSampleCv.wait(_filter.m_csReceive, [this]() -> bool {
        if (_isFlushing) {
            return true;
        }

        // at least NUM_SRC_FRAMES_PER_PROCESSING source frames are needed in queue for stop time calculation
        if (_sourceFrames.size() < NUM_SRC_FRAMES_PER_PROCESSING) {
            return true;
        }

        // add headroom to avoid blocking and context switch
        return _nextSourceFrameNb <= _nextOutputSourceFrameNb + NUM_SRC_FRAMES_PER_PROCESSING + Environment::GetInstance().GetExtraSourceBuffer();
    });

    if (_isFlushing || _isStopping) {
        return S_FALSE;
    }

    if ((_filter._changeOutputMediaType || _filter._reloadScript) && !ChangeOutputFormat()) {
        return S_FALSE;
    }

    REFERENCE_TIME inputSampleStartTime;
    REFERENCE_TIME inputSampleStopTime = 0;
    if (inputSample->GetTime(&inputSampleStartTime, &inputSampleStopTime) == VFW_E_SAMPLE_TIME_NOT_SET) {
        // for samples without start time, always treat as fixed frame rate
        inputSampleStartTime = _nextSourceFrameNb * MainFrameServer::GetInstance().GetSourceAvgFrameDuration();
    }

    {
        const std::shared_lock sharedSourceLock(_sourceMutex);

        // since the key of _sourceFrames is frame number, which only strictly increases, rbegin() returns the last emplaced frame
        if (const REFERENCE_TIME lastSampleStartTime = _sourceFrames.empty() ? 0 : _sourceFrames.crbegin()->second.startTime;
            inputSampleStartTime <= lastSampleStartTime) {
            Environment::GetInstance().Log(L"Rejecting source sample due to start time going backward: curr %10lli last %10lli", inputSampleStartTime, lastSampleStartTime);
            return S_FALSE;
        }
    }

    if (_nextSourceFrameNb == 0) {
        _frameRateCheckpointInputSampleStartTime = inputSampleStartTime;
    }

    RefreshInputFrameRates(_nextSourceFrameNb, inputSampleStartTime);

    BYTE *sampleBuffer;
    hr = inputSample->GetPointer(&sampleBuffer);
    if (FAILED(hr)) {
        return S_FALSE;
    }

    const VSFrameRef *frame = Format::CreateFrame(_filter._inputVideoFormat, sampleBuffer);

    std::shared_ptr<HDRSideData> hdrSideData = std::make_shared<HDRSideData>();
    {
        if (const ATL::CComQIPtr<IMediaSideData> inputSampleSideData(inputSample); inputSampleSideData != nullptr) {
            hdrSideData->ReadFrom(inputSampleSideData);

            if (const std::optional<const BYTE *> optHdr = hdrSideData->GetHDRData()) {
                _filter._inputVideoFormat.hdrType = 1;

                if (const std::optional<const BYTE *> optHdrCll = hdrSideData->GetHDRContentLightLevelData()) {
                    _filter._inputVideoFormat.hdrLuminance = reinterpret_cast<const MediaSideDataHDRContentLightLevel *>(*optHdrCll)->MaxCLL;
                } else {
                    _filter._inputVideoFormat.hdrLuminance = static_cast<int>(reinterpret_cast<const MediaSideDataHDR *>(*optHdr)->max_display_mastering_luminance);
                }
            }
        }
    }

    {
        const std::unique_lock uniqueSourceLock(_sourceMutex);

        _sourceFrames.emplace(std::piecewise_construct,
                              std::forward_as_tuple(_nextSourceFrameNb),
                              std::forward_as_tuple(frame, inputSampleStartTime, hdrSideData));
    }
    _newSourceFrameCv.notify_all();

    Environment::GetInstance().Log(L"Stored source frame: %6i at %10lli ~ %10lli duration(literal) %10lli",
                                   _nextSourceFrameNb, inputSampleStartTime, inputSampleStopTime, inputSampleStopTime - inputSampleStartTime);

    _nextSourceFrameNb += 1;

    /*
     * Some video decoders set the correct start time but the wrong stop time (stop time always being start time + average frame time).
     * Therefore instead of directly using the stop time from the current sample, we use the start time of the next sample.
     */

    // use map.lower_bound() in case the exact frame is removed by the script
    std::array<decltype(_sourceFrames)::const_iterator, NUM_SRC_FRAMES_PER_PROCESSING> processSourceFrameIters = { _sourceFrames.lower_bound(_nextProcessSourceFrameNb) };
    std::array<REFERENCE_TIME, NUM_SRC_FRAMES_PER_PROCESSING - 1> outputFrameDurations;

    {
        const std::shared_lock sharedSourceLock(_sourceMutex);

        for (int i = 0; i < NUM_SRC_FRAMES_PER_PROCESSING; ++i) {
            if (processSourceFrameIters[i] == _sourceFrames.cend()) {
                return S_OK;
            }

            if (i < NUM_SRC_FRAMES_PER_PROCESSING - 1) {
                processSourceFrameIters[i + 1] = processSourceFrameIters[i];
                ++processSourceFrameIters[i + 1];
            }

            if (i > 0) {
                outputFrameDurations[i - 1] = llMulDiv(processSourceFrameIters[i]->second.startTime - processSourceFrameIters[i - 1]->second.startTime,
                                                       MainFrameServer::GetInstance().GetScriptAvgFrameDuration(),
                                                       MainFrameServer::GetInstance().GetSourceAvgFrameDuration(),
                                                       0);
            }
        }
    }

    if (processSourceFrameIters[0]->first == 0) {
        _nextOutputFrameStartTime = processSourceFrameIters[0]->second.startTime;
        _frameRateCheckpointOutputFrameStartTime = processSourceFrameIters[0]->second.startTime;
    }

    while (true) {
        const REFERENCE_TIME outputFrameDurationBeforeEdgePortion = min(processSourceFrameIters[1]->second.startTime - _nextOutputFrameStartTime, outputFrameDurations[0]);
        if (outputFrameDurationBeforeEdgePortion <= 0) {
            Environment::GetInstance().Log(L"Frame time drift: %10lli", -outputFrameDurationBeforeEdgePortion);
            break;
        }
        const REFERENCE_TIME outputFrameDurationAfterEdgePortion = outputFrameDurations[1] - llMulDiv(outputFrameDurations[1], outputFrameDurationBeforeEdgePortion, outputFrameDurations[0], 0);

        const REFERENCE_TIME outputStartTime = _nextOutputFrameStartTime;
        REFERENCE_TIME outputStopTime = outputStartTime + outputFrameDurationBeforeEdgePortion + outputFrameDurationAfterEdgePortion;
        if (outputStopTime < processSourceFrameIters[1]->second.startTime && outputStopTime >= processSourceFrameIters[1]->second.startTime - MAX_OUTPUT_FRAME_DURATION_PADDING) {
            outputStopTime = processSourceFrameIters[1]->second.startTime;
        }
        _nextOutputFrameStartTime = outputStopTime;

        Environment::GetInstance().Log(L"Requesting output frame %6i for source frame %6i at %10lli ~ %10lli duration %10lli",
                                       _nextOutputFrameNb, processSourceFrameIters[0]->first, outputStartTime, outputStopTime, outputStopTime - outputStartTime);

        RefreshOutputFrameRates(_nextOutputFrameNb, outputStartTime);

        {
            std::unique_lock uniqueOutputLock(_outputMutex);

            _outputSamples.emplace(std::piecewise_construct,
                                   std::forward_as_tuple(_nextOutputFrameNb),
                                   std::forward_as_tuple(outputStartTime, outputStopTime, processSourceFrameIters[0]->first, processSourceFrameIters[0]->second.hdrSideData));
        }
        AVSF_VS_API->getFrameAsync(_nextOutputFrameNb, MainFrameServer::GetInstance().GetScriptClip(), VpsGetFrameCallback, this);

        _nextOutputFrameNb += 1;
    }

    _nextProcessSourceFrameNb += 1;

    return S_OK;
}

auto FrameHandler::GetSourceFrame(int frameNb) -> const VSFrameRef * {
    Environment::GetInstance().Log(L"Get source frame: frameNb %6i input queue size %2zu", frameNb, _sourceFrames.size());

    std::shared_lock sharedSourceLock(_sourceMutex);

    decltype(_sourceFrames)::const_iterator iter;
    _newSourceFrameCv.wait(sharedSourceLock, [this, &iter, frameNb]() -> bool {
        if (_isFlushing) {
            return true;
        }

        // use map.lower_bound() in case the exact frame is removed by the script
        iter = _sourceFrames.lower_bound(frameNb);
        return iter != _sourceFrames.cend();
    });

    if (_isFlushing) {
        Environment::GetInstance().Log(L"Drain for frame %6i", frameNb);
        return MainFrameServer::GetInstance().GetSourceDrainFrame();
    }

    return iter->second.frame;
}

auto FrameHandler::BeginFlush() -> void {
    Environment::GetInstance().Log(L"FrameHandler start BeginFlush()");

    // make sure there is at most one flush session active at any time
    // or else assumptions such as "_isFlushing stays true until end of EndFlush()" will no longer hold

    _isFlushing.wait(true);
    _isFlushing = true;

    _addInputSampleCv.notify_all();
    _newSourceFrameCv.notify_all();
    _deliverSampleCv.notify_all();

    Environment::GetInstance().Log(L"FrameHandler finish BeginFlush()");
}

auto FrameHandler::EndFlush(const std::function<void ()> &interim) -> void {
    Environment::GetInstance().Log(L"FrameHandler start EndFlush()");

    /*
     * EndFlush() can be called by either the application or the worker thread
     *
     * when called by former, we need to synchronize with the worker thread
     * when called by latter, current thread ID should be same as the worker thread, and no need for sync
     */
    if (std::this_thread::get_id() != _workerThread.get_id()) {
        _isWorkerLatched.wait(false);
    }

    {
        std::shared_lock sharedOutputLock(_outputMutex);

        _flushOutputSampleCv.wait(sharedOutputLock, [this]() {
            return std::ranges::all_of(_outputSamples | std::views::values, [](const OutputSampleData &data) {
                return data.frame!= nullptr;
            });
        });
    }

    if (interim) {
        interim();
    }

    _sourceFrames.clear();
    _outputSamples.clear();

    ResetInput();

    _isFlushing = false;
    _isFlushing.notify_all();

    Environment::GetInstance().Log(L"FrameHandler finish EndFlush()");
}

auto FrameHandler::Start() -> void {
    _isStopping = false;
    _workerThread = std::thread(&FrameHandler::WorkerProc, this);
}

auto FrameHandler::Stop() -> void {
    _isStopping = true;

    BeginFlush();
    EndFlush([]() -> void {
        /*
         * Stop the script after worker threads are paused and before flushing is done so that no new frame request (GetSourceFrame()) happens.
         * And since _isFlushing is still on, existing frame request should also just drain instead of block.
         *
         * If no stop here, since AddInputSample() no longer adds frame, existing GetSourceFrame() calls will stuck forever.
         */
        MainFrameServer::GetInstance().StopScript();
    });

    if (_workerThread.joinable()) {
        _workerThread.join();
    }
}

auto FrameHandler::GetInputBufferSize() const -> int {
    const std::shared_lock sharedSourceLock(_sourceMutex);

    return static_cast<int>(_sourceFrames.size());
}

FrameHandler::SourceFrameInfo::~SourceFrameInfo() {
    AVSF_VS_API->freeFrame(frame);
}

FrameHandler::OutputSampleData::~OutputSampleData() {
    if (frame != nullptr) {
        AVSF_VS_API->freeFrame(frame);
    }
}

auto VS_CC FrameHandler::VpsGetFrameCallback(void *userData, const VSFrameRef *f, int n, VSNodeRef *node, const char *errorMsg) -> void {
    if (f == nullptr) {
        Environment::GetInstance().Log(L"Failed to generate output frame %6i with message: %S", n, errorMsg);
        return;
    }

    FrameHandler *frameHandler = static_cast<FrameHandler *>(userData);
    Environment::GetInstance().Log(L"Output frame %6i is ready, output queue size %2zu", n, frameHandler->_outputSamples.size());

    if (frameHandler->_isFlushing) {
        {
            std::unique_lock uniqueOutputLock(frameHandler->_outputMutex);

            frameHandler->_outputSamples.erase(n);
        }

        AVSF_VS_API->freeFrame(f);
        frameHandler->_flushOutputSampleCv.notify_all();
    } else {
        {
            std::shared_lock sharedOutputLock(frameHandler->_outputMutex);

            const decltype(frameHandler->_outputSamples)::iterator iter = frameHandler->_outputSamples.find(n);
            ASSERT(iter != frameHandler->_outputSamples.end());
            iter->second.frame = f;
        }

        frameHandler->_deliverSampleCv.notify_all();
    }
}

auto FrameHandler::RefreshFrameRatesTemplate(int sampleNb, REFERENCE_TIME startTime,
                                             int &checkpointSampleNb, REFERENCE_TIME &checkpointStartTime,
                                             int &currentFrameRate) -> void {
    bool reachCheckpoint = checkpointStartTime == 0;

    if (const REFERENCE_TIME elapsedRefTime = startTime - checkpointStartTime; elapsedRefTime >= UNITS) {
        currentFrameRate = static_cast<int>(llMulDiv((static_cast<LONGLONG>(sampleNb) - checkpointSampleNb) * FRAME_RATE_SCALE_FACTOR, UNITS, elapsedRefTime, 0));
        reachCheckpoint = true;
    }

    if (reachCheckpoint) {
        checkpointSampleNb = sampleNb;
        checkpointStartTime = startTime;
    }
}

auto FrameHandler::ResetInput() -> void {
    _nextSourceFrameNb = 0;
    _nextProcessSourceFrameNb = 0;
    _nextOutputFrameNb = 0;
    _nextOutputSourceFrameNb = 0;

    _frameRateCheckpointInputSampleNb = 0;
    _currentInputFrameRate = 0;

    _frameRateCheckpointOutputFrameNb = 0;
    _currentOutputFrameRate = 0;
}

auto FrameHandler::ResetOutput() -> void {
    // to ensure these non-atomic properties are only modified by their sole consumer the worker thread
    ASSERT(std::this_thread::get_id() == _workerThread.get_id());

    _nextDeliveryFrameNb = 0;
}

auto FrameHandler::PrepareOutputSample(ATL::CComPtr<IMediaSample> &sample, int outputFrameNb, OutputSampleData &data) const -> bool {
    if (FAILED(_filter.m_pOutput->GetDeliveryBuffer(&sample, &data.startTime, &data.stopTime, 0))) {
        // avoid releasing the invalid pointer in case the function change it to some random invalid address
        sample.Detach();
        return false;
    }

    AM_MEDIA_TYPE *pmtOut;
    sample->GetMediaType(&pmtOut);

    if (pmtOut != nullptr && pmtOut->pbFormat != nullptr) {
        _filter.m_pOutput->SetMediaType(static_cast<CMediaType *>(pmtOut));
        _filter._outputVideoFormat = Format::GetVideoFormat(*pmtOut, &MainFrameServer::GetInstance());
        sample->SetMediaType(&_filter.m_pOutput->CurrentMediaType());

        Environment::GetInstance().Log(L"New output format: name %s, width %5li, height %5li",
                                       _filter._outputVideoFormat.pixelFormat->name.c_str(), _filter._outputVideoFormat.bmi.biWidth, _filter._outputVideoFormat.bmi.biHeight);

        DeleteMediaType(pmtOut);
    }

    if (FAILED(sample->SetTime(&data.startTime, &data.stopTime))) {
        return false;
    }

    if (outputFrameNb == 0 && FAILED(sample->SetDiscontinuity(TRUE))) {
        return false;
    }

    BYTE *outputBuffer;
    if (FAILED(sample->GetPointer(&outputBuffer))) {
        return false;
    }

    Format::WriteSample(_filter._outputVideoFormat, data.frame, outputBuffer);

    if (const ATL::CComQIPtr<IMediaSideData> outputSampleSideData(sample); outputSampleSideData != nullptr) {
        data.hdrSideData->WriteTo(outputSampleSideData);
    }

    return true;
}

auto FrameHandler::WorkerProc() -> void {
    Environment::GetInstance().Log(L"Start worker thread");

#ifdef _DEBUG
    SetThreadDescription(GetCurrentThread(), L"CSynthFilter Worker");
#endif

    ResetOutput();
    _isWorkerLatched = false;

    while (true) {
        if (_isFlushing) {
            _isWorkerLatched = true;
            _isWorkerLatched.notify_all();
            _isFlushing.wait(true);

            if (_isStopping) {
                break;
            }

            ResetOutput();
            _isWorkerLatched = false;
        }

        decltype(_outputSamples)::iterator iter;
        {
            std::shared_lock sharedOutputLock(_outputMutex);

            _deliverSampleCv.wait(sharedOutputLock, [this, &iter]() -> bool {
                if (_isFlushing) {
                    return true;
                }

                iter = _outputSamples.find(_nextDeliveryFrameNb);
                if (iter == _outputSamples.end()) {
                    return false;
                }

                return iter->second.frame != nullptr;
            });
        }

        if (_isFlushing) {
            continue;
        }

        _nextOutputSourceFrameNb = iter->second.sourceFrameNb;

        if (ATL::CComPtr<IMediaSample> outputSample; PrepareOutputSample(outputSample, iter->first, iter->second)) {
            _filter.m_pOutput->Deliver(outputSample);
            Environment::GetInstance().Log(L"Delivered output sample %6i from source frame %6i", iter->first, iter->second.sourceFrameNb);
        }

        {
            std::unique_lock uniqueOutputLock(_outputMutex);

            _outputSamples.erase(iter);
        }

        GarbageCollect(_nextOutputSourceFrameNb - 1);
        _nextDeliveryFrameNb += 1;
    }

    _isWorkerLatched = true;
    _isWorkerLatched.notify_all();

    Environment::GetInstance().Log(L"Stop worker thread");
}

auto FrameHandler::GarbageCollect(int srcFrameNb) -> void {
    const std::unique_lock uniqueSourceLock(_sourceMutex);

    const size_t dbgPreSize = _sourceFrames.size();

    // search for all previous frames in case of some source frames are never used
    // this could happen by plugins that decrease frame rate
    const auto sourceEnd = _sourceFrames.cend();
    for (decltype(_sourceFrames)::const_iterator iter = _sourceFrames.begin(); iter != sourceEnd && iter->first <= srcFrameNb; iter = _sourceFrames.begin()) {
        _sourceFrames.erase(iter);
    }

    _addInputSampleCv.notify_all();

    Environment::GetInstance().Log(L"GarbageCollect frames until %6i pre size %3zu post size %3zu", srcFrameNb, dbgPreSize, _sourceFrames.size());
}

auto FrameHandler::ChangeOutputFormat() -> bool {
    Environment::GetInstance().Log(L"Upstream proposes to change input format: name %s, width %5li, height %5li",
                                   _filter._inputVideoFormat.pixelFormat->name.c_str(), _filter._inputVideoFormat.bmi.biWidth, _filter._inputVideoFormat.bmi.biHeight);

    _filter.StopStreaming();

    BeginFlush();
    EndFlush([this]() -> void {
        MainFrameServer::GetInstance().ReloadScript(_filter.m_pInput->CurrentMediaType(), true);
    });

    _filter._changeOutputMediaType = false;
    _filter._reloadScript = false;

    auto potentialOutputMediaTypes = _filter.InputToOutputMediaType(&_filter.m_pInput->CurrentMediaType());
    const auto newOutputMediaTypeIter = std::ranges::find_if(potentialOutputMediaTypes, [this](const CMediaType &outputMediaType) -> bool {
        /*
         * "QueryAccept (Downstream)" forces the downstream to use the new output media type as-is, which may lead to wrong rendering result
         * "ReceiveConnection" allows downstream to counter-propose suitable media type for the connection
         * after ReceiveConnection(), the next output sample should carry the new output media type, which is handled in PrepareOutputSample()
         */

        if (_isFlushing) {
            return false;
        }

        return SUCCEEDED(_filter.m_pOutput->GetConnected()->ReceiveConnection(_filter.m_pOutput, &outputMediaType));
    });

    if (newOutputMediaTypeIter == potentialOutputMediaTypes.end()) {
        Environment::GetInstance().Log(L"Downstream does not accept any of the new output media types");
        _filter.AbortPlayback(VFW_E_TYPE_NOT_ACCEPTED);
        return false;
    }

    _filter.StartStreaming();

    return true;
}

auto FrameHandler::RefreshInputFrameRates(int frameNb, REFERENCE_TIME startTime) -> void {
    RefreshFrameRatesTemplate(frameNb, startTime, _frameRateCheckpointInputSampleNb, _frameRateCheckpointInputSampleStartTime, _currentInputFrameRate);
}

auto FrameHandler::RefreshOutputFrameRates(int frameNb, REFERENCE_TIME startTime) -> void {
    RefreshFrameRatesTemplate(frameNb, startTime, _frameRateCheckpointOutputFrameNb, _frameRateCheckpointOutputFrameStartTime, _currentOutputFrameRate);
}

}
