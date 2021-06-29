// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "frame_handler.h"
#include "constants.h"
#include "filter.h"


namespace SynthFilter {

auto FrameHandler::AddInputSample(IMediaSample *inputSample) -> HRESULT {
    HRESULT hr;

    _addInputSampleCv.wait(_filter.m_csReceive, [this]() -> bool {
        if (_isFlushing) {
            return true;
        }

        // at least NUM_SRC_FRAMES_PER_PROCESSING source frames are needed in queue for stop time calculation
        if (_sourceFrames.size() < NUM_SRC_FRAMES_PER_PROCESSING + _extraSourceBuffer) {
            return true;
        }

        // add headroom to avoid blocking and context switch
        return _nextSourceFrameNb <= _maxRequestedFrameNb;
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
        if (const REFERENCE_TIME lastSampleStartTime = _sourceFrames.empty() ? -1 : _sourceFrames.rbegin()->second.startTime;
            inputSampleStartTime <= lastSampleStartTime) {
            Environment::GetInstance().Log(L"Rejecting source sample due to start time going backward: curr %10lli last %10lli",
                                           inputSampleStartTime, lastSampleStartTime);
            return S_FALSE;
        }
    }

    RefreshInputFrameRates(_nextSourceFrameNb);

    BYTE *sampleBuffer;
    hr = inputSample->GetPointer(&sampleBuffer);
    if (FAILED(hr)) {
        return S_FALSE;
    }

    const PVideoFrame frame = Format::CreateFrame(_filter._inputVideoFormat, sampleBuffer);

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

    if (!_filter._isReadyToReceive) {
        Environment::GetInstance().Log(L"Discarding obsolete input sample due to filter state change");
        return S_FALSE;
    }

    {
        const std::unique_lock uniqueSourceLock(_sourceMutex);

        _sourceFrames.emplace(std::piecewise_construct,
                              std::forward_as_tuple(_nextSourceFrameNb),
                              std::forward_as_tuple(frame, inputSampleStartTime, hdrSideData));
    }
    _newSourceFrameCv.notify_all();

    Environment::GetInstance().Log(L"Stored source frame: %6i at %10lli ~ %10lli duration(literal) %10lli max_requested %6i extra_buffer %6i",
                                   _nextSourceFrameNb, inputSampleStartTime, inputSampleStopTime, inputSampleStopTime - inputSampleStartTime, _maxRequestedFrameNb.load(), _extraSourceBuffer);

    _nextSourceFrameNb += 1;
    UpdateExtraSourceBuffer();

    return S_OK;
}

auto FrameHandler::GetSourceFrame(int frameNb) -> PVideoFrame {
    Environment::GetInstance().Log(L"Get source frame: frameNb %6i input queue size %2zu", frameNb, _sourceFrames.size());

    std::shared_lock sharedSourceLock(_sourceMutex);

    _maxRequestedFrameNb = max(frameNb, _maxRequestedFrameNb.load());
    _addInputSampleCv.notify_all();

    decltype(_sourceFrames)::const_iterator iter;
    _newSourceFrameCv.wait(sharedSourceLock, [this, &iter, frameNb]() -> bool {
        if (_isFlushing) {
            return true;
        }

        // use map.lower_bound() in case the exact frame is removed by the script
        iter = _sourceFrames.lower_bound(frameNb);
        return iter != _sourceFrames.end();
    });

    if (_isFlushing || iter->second.frame == nullptr) {
        if (_isFlushing) {
            Environment::GetInstance().Log(L"Drain for frame %6i", frameNb);
        } else {
            Environment::GetInstance().Log(L"Bad frame %6i", frameNb);
        }

        return MainFrameServer::GetInstance().GetSourceDrainFrame();
    }

    Environment::GetInstance().Log(L"Return source frame %6i", frameNb);

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

    Environment::GetInstance().Log(L"FrameHandler finish BeginFlush()");
}

auto FrameHandler::EndFlush(const std::function<void ()> &interim) -> void {
    Environment::GetInstance().Log(L"FrameHandler start EndFlush()");

    _isWorkerLatched.wait(false);

    if (interim) {
        interim();
    }

    _sourceFrames.clear();
    ResetInput();

    _isFlushing = false;
    _isFlushing.notify_all();

    Environment::GetInstance().Log(L"FrameHandler finish EndFlush()");
}

auto FrameHandler::ResetInput() -> void {
    _nextSourceFrameNb = 0;
    _maxRequestedFrameNb = 0;
    _notifyChangedOutputMediaType = false;
    _extraSourceBuffer = 0;

    _frameRateCheckpointInputSampleNb = 0;
    _currentInputFrameRate = 0;
}

auto FrameHandler::PrepareOutputSample(ATL::CComPtr<IMediaSample> &sample, REFERENCE_TIME startTime, REFERENCE_TIME stopTime) -> bool {
    if (FAILED(_filter.m_pOutput->GetDeliveryBuffer(&sample, &startTime, &stopTime, 0))) {
        // avoid releasing the invalid pointer in case the function change it to some random invalid address
        sample.Detach();
        return false;
    }

    AM_MEDIA_TYPE *pmtOut;
    sample->GetMediaType(&pmtOut);

    if (const std::shared_ptr<AM_MEDIA_TYPE> pmtOutPtr(pmtOut, &DeleteMediaType);
        pmtOut != nullptr && pmtOut->pbFormat != nullptr) {
        _filter.m_pOutput->SetMediaType(static_cast<CMediaType *>(pmtOut));
        _filter._outputVideoFormat = Format::GetVideoFormat(*pmtOut, &MainFrameServer::GetInstance());
        _notifyChangedOutputMediaType = true;
    }

    if (_notifyChangedOutputMediaType) {
        sample->SetMediaType(&_filter.m_pOutput->CurrentMediaType());
        _notifyChangedOutputMediaType = false;

        Environment::GetInstance().Log(L"New output format: name %s, width %5li, height %5li",
                                       _filter._outputVideoFormat.pixelFormat->name, _filter._outputVideoFormat.bmi.biWidth, _filter._outputVideoFormat.bmi.biHeight);
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
            const PVideoFrame scriptFrame = MainFrameServer::GetInstance().GetFrame(_nextOutputFrameNb);
            Format::WriteSample(_filter._outputVideoFormat, scriptFrame, outputBuffer);
        } catch (AvisynthError) {
            return false;
        }
    }

    return true;
}

auto FrameHandler::WorkerProc() -> void {
    const auto ResetOutput = [this]() -> void {
        _nextOutputFrameNb = 0;

        _frameRateCheckpointOutputFrameNb = 0;
        _currentOutputFrameRate = 0;
        _frameRateCheckpointDeliveryFrameNb = 0;
        _currentDeliveryFrameRate = 0;
    };

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

        /*
         * Some video decoders set the correct start time but the wrong stop time (stop time always being start time + average frame time).
         * Therefore instead of directly using the stop time from the current sample, we use the start time of the next sample.
         */

        std::array<decltype(_sourceFrames)::const_iterator, NUM_SRC_FRAMES_PER_PROCESSING> processSourceFrameIters;
        std::array<REFERENCE_TIME, NUM_SRC_FRAMES_PER_PROCESSING - 1> outputFrameDurations;

        {
            std::shared_lock sharedSourceLock(_sourceMutex);

            _newSourceFrameCv.wait(sharedSourceLock, [&]() -> bool {
                if (_isFlushing) {
                    return true;
                }

                return _sourceFrames.size() >= NUM_SRC_FRAMES_PER_PROCESSING;
            });

            if (_isFlushing) {
                continue;
            }

            processSourceFrameIters[0] = _sourceFrames.begin();

            for (int i = 1; i < NUM_SRC_FRAMES_PER_PROCESSING; ++i) {
                processSourceFrameIters[i] = processSourceFrameIters[i - 1];
                ++processSourceFrameIters[i];

                outputFrameDurations[i - 1] = llMulDiv(processSourceFrameIters[i]->second.startTime - processSourceFrameIters[i - 1]->second.startTime,
                                                       MainFrameServer::GetInstance().GetScriptAvgFrameDuration(),
                                                       MainFrameServer::GetInstance().GetSourceAvgFrameDuration(),
                                                       0);
            }
        }

        if (processSourceFrameIters[0]->first == 0) {
            _nextOutputFrameStartTime = processSourceFrameIters[0]->second.startTime;
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

            RefreshOutputFrameRates(_nextOutputFrameNb);

            if (ATL::CComPtr<IMediaSample> outputSample; PrepareOutputSample(outputSample, outputStartTime, outputStopTime)) {
                if (const ATL::CComQIPtr<IMediaSideData> outputSampleSideData(outputSample); outputSampleSideData != nullptr) {
                    processSourceFrameIters[0]->second.hdrSideData->WriteTo(outputSampleSideData);
                }

                _filter.m_pOutput->Deliver(outputSample);
                RefreshDeliveryFrameRates(_nextOutputFrameNb);

                Environment::GetInstance().Log(L"Delivered frame %6i", _nextOutputFrameNb);
            }

            _nextOutputFrameNb += 1;
        }

        GarbageCollect(processSourceFrameIters[0]->first);
    }

    _isWorkerLatched = true;
    _isWorkerLatched.notify_all();

    Environment::GetInstance().Log(L"Stop worker thread");
}

}
