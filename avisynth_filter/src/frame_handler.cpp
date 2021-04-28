// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "frame_handler.h"
#include "constants.h"
#include "filter.h"


namespace AvsFilter {

FrameHandler::FrameHandler(CAviSynthFilter &filter)
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
        return _nextSourceFrameNb <= _maxRequestedFrameNb + Environment::GetInstance().GetExtraSourceBuffer();
    });

    if (_isFlushing) {
        return S_FALSE;
    }

    if ((_filter._changeOutputMediaType || _filter._reloadAvsSource) && !ChangeOutputFormat()) {
        return S_FALSE;
    }

    REFERENCE_TIME inputSampleStartTime;
    REFERENCE_TIME inputSampleStopTime = 0;
    if (inputSample->GetTime(&inputSampleStartTime, &inputSampleStopTime) == VFW_E_SAMPLE_TIME_NOT_SET) {
        // for samples without start time, always treat as fixed frame rate
        inputSampleStartTime = _nextSourceFrameNb * AvsHandler::GetInstance().GetMainScriptInstance().GetSourceAvgFrameDuration();
    }

    {
        std::shared_lock sharedLock(_mutex);

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

    const PVideoFrame avsFrame = Format::CreateFrame(_filter._inputVideoFormat, sampleBuffer, AvsHandler::GetInstance().GetMainScriptInstance().GetEnv());

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
        const std::unique_lock uniqueLock(_mutex);

        _sourceFrames.emplace(std::piecewise_construct,
                              std::forward_as_tuple(_nextSourceFrameNb),
                              std::forward_as_tuple(avsFrame, inputSampleStartTime, hdrSideData));
    }
    _newSourceFrameCv.notify_all();

    Environment::GetInstance().Log(L"Stored source frame: %6i at %10lli ~ %10lli duration(literal) %10lli",
                                   _nextSourceFrameNb, inputSampleStartTime, inputSampleStopTime, inputSampleStopTime - inputSampleStartTime);

    _nextSourceFrameNb += 1;

    return S_OK;
}

auto FrameHandler::GetSourceFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame {
    Environment::GetInstance().Log(L"Get source frame: frameNb %6i input queue size %2zu", frameNb, _sourceFrames.size());

    std::shared_lock sharedLock(_mutex);

    _maxRequestedFrameNb = max(frameNb, _maxRequestedFrameNb.load());
    _addInputSampleCv.notify_all();

    decltype(_sourceFrames)::const_iterator iter;
    _newSourceFrameCv.wait(sharedLock, [this, &iter, frameNb]() -> bool {
        if (_isFlushing) {
            return true;
        }

        // use map.lower_bound() in case the exact frame is removed by the script
        iter = _sourceFrames.lower_bound(frameNb);
        return iter != _sourceFrames.cend();
    });

    if (_isFlushing || iter->second.avsFrame == nullptr) {
        if (_isFlushing) {
            Environment::GetInstance().Log(L"Drain for frame %6i", frameNb);
        } else {
            Environment::GetInstance().Log(L"Bad frame %6i", frameNb);
        }

        return AvsHandler::GetInstance().GetMainScriptInstance().GetSourceDrainFrame();
    }

    return iter->second.avsFrame;
}

auto FrameHandler::BeginFlush() -> void {
    Environment::GetInstance().Log(L"FrameHandler start BeginFlush()");

    // make sure there is at most one flush session active at any time
    // or else assumptions such as "_isFlushing stays true until end of EndFlush()" will no longer hold

    _isFlushing.wait(true);
    _isFlushing = true;

    _addInputSampleCv.notify_all();
    _newSourceFrameCv.notify_all();

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

    if (interim) {
        interim();
    }

    _sourceFrames.clear();

    ResetInput();

    _isFlushing = false;
    _isFlushing.notify_all();

    Environment::GetInstance().Log(L"FrameHandler finish EndFlush()");
}

auto FrameHandler::StartWorkerThread() -> void {
    _workerThread = std::thread(&FrameHandler::WorkerProc, this);
}

auto FrameHandler::StopWorkerThreads() -> void {
    _isStoppingWorker = true;

    BeginFlush();
    EndFlush([]() -> void {
        /*
         * Stop the Avs script after worker threads are paused and before flushing is done so that no new frame request (GetSourceFrame()) happens.
         * And since _isFlushing is still on, existing frame request should also just drain instead of block.
         *
         * If no stop here, since AddInputSample() no longer adds frame, existing GetSourceFrame() calls will stuck forever.
         */
        AvsHandler::GetInstance().GetMainScriptInstance().StopScript();
    });

    if (_workerThread.joinable()) {
        _workerThread.join();
    }

    _isStoppingWorker = false;
}

auto FrameHandler::GetInputBufferSize() const -> int {
    const std::shared_lock sharedLock(_mutex);

    return static_cast<int>(_sourceFrames.size());
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
    _maxRequestedFrameNb = 0;

    _frameRateCheckpointInputSampleNb = 0;
    _currentInputFrameRate = 0;
}

auto FrameHandler::ResetOutput() -> void {
    // to ensure these non-atomic properties are only modified by their sole consumer the worker thread
    ASSERT(std::this_thread::get_id() == _workerThread.get_id());

    _nextOutputFrameNb = 0;

    _frameRateCheckpointOutputFrameNb = 0;
    _currentOutputFrameRate = 0;
}

auto FrameHandler::PrepareOutputSample(ATL::CComPtr<IMediaSample> &sample, REFERENCE_TIME startTime, REFERENCE_TIME stopTime) const -> bool {
    if (FAILED(_filter.m_pOutput->GetDeliveryBuffer(&sample, &startTime, &stopTime, 0))) {
        // avoid releasing the invalid pointer in case the function change it to some random invalid address
        sample.Detach();
        return false;
    }

    AM_MEDIA_TYPE *pmtOut;
    sample->GetMediaType(&pmtOut);

    if (pmtOut != nullptr && pmtOut->pbFormat != nullptr) {
        _filter.m_pOutput->SetMediaType(static_cast<CMediaType *>(pmtOut));
        _filter._outputVideoFormat = Format::GetVideoFormat(*pmtOut);
        sample->SetMediaType(&_filter.m_pOutput->CurrentMediaType());

        Environment::GetInstance().Log(L"New output format: name %s, width %5li, height %5li",
                                       _filter._outputVideoFormat.pixelFormat->name.c_str(), _filter._outputVideoFormat.bmi.biWidth, _filter._outputVideoFormat.bmi.biHeight);

        DeleteMediaType(pmtOut);
    }

    if (FAILED(sample->SetTime(&startTime, &stopTime))) {
        return false;
    }

    if (_nextOutputFrameNb == 0 && FAILED(sample->SetDiscontinuity(TRUE))) {
        return false;
    }

    if (BYTE *outputBuffer; FAILED(sample->GetPointer(&outputBuffer))) {
        return false;
    } else {
        try {
            // some AviSynth internal filter (e.g. Subtitle) can't tolerate multi-thread access
            const PVideoFrame scriptFrame = AvsHandler::GetInstance().GetMainScriptInstance().GetFrame(_nextOutputFrameNb);
            Format::WriteSample(_filter._outputVideoFormat, scriptFrame, outputBuffer, AvsHandler::GetInstance().GetMainScriptInstance().GetEnv());
        } catch (AvisynthError) {
            return false;
        }
    }

    return true;
}

auto FrameHandler::WorkerProc() -> void {
    Environment::GetInstance().Log(L"Start output worker thread");

#ifdef _DEBUG
    SetThreadDescription(GetCurrentThread(), L"CAviSynthFilter Worker");
#endif

    ResetOutput();
    _isWorkerLatched = false;

    while (true) {
        if (_isFlushing) {
            _isWorkerLatched = true;
            _isWorkerLatched.notify_all();
            _isFlushing.wait(true);

            if (_isStoppingWorker) {
                break;
            }

            ResetOutput();
            _isWorkerLatched = false;
        }

        /*
         * Some video decoders set the correct start time but the wrong stop time (stop time always being start time + average frame time).
         * Therefore instead of directly using the stop time from the current sample, we use the start time of the next sample.
         */

        std::array<decltype(_sourceFrames)::const_iterator, NUM_SRC_FRAMES_PER_PROCESSING> processSourceFrameIters;
        std::array<REFERENCE_TIME, NUM_SRC_FRAMES_PER_PROCESSING - 1> outputFrameDurations;

        {
            std::shared_lock sharedLock(_mutex);

            _newSourceFrameCv.wait(sharedLock, [&]() -> bool {
                if (_isFlushing) {
                    return true;
                }

                return _sourceFrames.size() >= NUM_SRC_FRAMES_PER_PROCESSING;
            });

            if (_isFlushing) {
                continue;
            }

            processSourceFrameIters[0] = _sourceFrames.cbegin();

            for (int i = 1; i < NUM_SRC_FRAMES_PER_PROCESSING; ++i) {
                processSourceFrameIters[i] = processSourceFrameIters[i - 1];
                ++processSourceFrameIters[i];

                outputFrameDurations[i - 1] = llMulDiv(processSourceFrameIters[i]->second.startTime - processSourceFrameIters[i - 1]->second.startTime,
                                                       AvsHandler::GetInstance().GetMainScriptInstance().GetScriptAvgFrameDuration(),
                                                       AvsHandler::GetInstance().GetMainScriptInstance().GetSourceAvgFrameDuration(),
                                                       0);
            }
        }

        if (processSourceFrameIters[0]->first == 0) {
            _nextOutputFrameStartTime = processSourceFrameIters[0]->second.startTime;
            _frameRateCheckpointOutputFrameStartTime = processSourceFrameIters[0]->second.startTime;
        }

        while (!_isFlushing) {
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

            Environment::GetInstance().Log(L"Processing output frame %6i for source frame %6i at %10lli ~ %10lli duration %10lli",
                                           _nextOutputFrameNb, processSourceFrameIters[0]->first, outputStartTime, outputStopTime, outputStopTime - outputStartTime);

            RefreshOutputFrameRates(_nextOutputFrameNb, outputStartTime);

            if (ATL::CComPtr<IMediaSample> outputSample; PrepareOutputSample(outputSample, outputStartTime, outputStopTime)) {
                if (const ATL::CComQIPtr<IMediaSideData> outputSampleSideData(outputSample); outputSampleSideData != nullptr) {
                    processSourceFrameIters[0]->second.hdrSideData->WriteTo(outputSampleSideData);
                }

                _filter.m_pOutput->Deliver(outputSample);
                Environment::GetInstance().Log(L"Delivered frame %6i", _nextOutputFrameNb);
            }

            _nextOutputFrameNb += 1;
        }

        GarbageCollect(processSourceFrameIters[0]->first);
    }

    _isWorkerLatched = true;
    _isWorkerLatched.notify_all();

    Environment::GetInstance().Log(L"Stop output worker thread");
}

auto FrameHandler::GarbageCollect(int srcFrameNb) -> void {
    const std::unique_lock uniqueLock(_mutex);

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
        AvsHandler::GetInstance().GetMainScriptInstance().ReloadScript(_filter.m_pInput->CurrentMediaType(), true);
    });

    _filter._changeOutputMediaType = false;
    _filter._reloadAvsSource = false;

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
