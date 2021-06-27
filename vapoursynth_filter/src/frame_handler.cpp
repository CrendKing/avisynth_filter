// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "frame_handler.h"
#include "filter.h"


namespace SynthFilter {

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
        return _nextSourceFrameNb <= _lastUsedSourceFrameNb + NUM_SRC_FRAMES_PER_PROCESSING + Environment::GetInstance().GetExtraSourceBuffer();
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
            Environment::GetInstance().Log(L"Rejecting source sample due to start time going backward: curr %10lli last %10lli", inputSampleStartTime, lastSampleStartTime);
            return S_FALSE;
        }
    }

    RefreshInputFrameRates(_nextSourceFrameNb);

    BYTE *sampleBuffer;
    hr = inputSample->GetPointer(&sampleBuffer);
    if (FAILED(hr)) {
        return S_FALSE;
    }

    VSFrameRef *frame = Format::CreateFrame(_filter._inputVideoFormat, sampleBuffer);
    VSMap *frameProps = AVSF_VS_API->getFramePropsRW(frame);
    AVSF_VS_API->propSetInt(frameProps, "_FieldBased", 0, paReplace);
    AVSF_VS_API->propSetFloat(frameProps, VS_PROP_NAME_ABS_TIME, inputSampleStartTime / static_cast<double>(UNITS), paReplace);
    AVSF_VS_API->propSetInt(frameProps, "_SARNum", _filter._inputVideoFormat.pixelAspectRatioNum, paReplace);
    AVSF_VS_API->propSetInt(frameProps, "_SARDen", _filter._inputVideoFormat.pixelAspectRatioDen, paReplace);

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

    Environment::GetInstance().Log(L"Stored source frame: %6i at %10lli ~ %10lli duration(literal) %10lli, last used: %6i",
                                   _nextSourceFrameNb, inputSampleStartTime, inputSampleStopTime, inputSampleStopTime - inputSampleStartTime, _lastUsedSourceFrameNb.load());

    _nextSourceFrameNb += 1;

    /*
     * Some video decoders set the correct start time but the wrong stop time (stop time always being start time + average frame time).
     * Therefore instead of directly using the stop time from the current sample, we use the start time of the next sample.
     */

    // use map.lower_bound() in case the exact frame is removed by the script
    std::array<decltype(_sourceFrames)::const_iterator, NUM_SRC_FRAMES_PER_PROCESSING> processSourceFrameIters = { _sourceFrames.lower_bound(_nextProcessSourceFrameNb) };

    {
        const std::shared_lock sharedSourceLock(_sourceMutex);

        for (int i = 0; i < NUM_SRC_FRAMES_PER_PROCESSING; ++i) {
            if (processSourceFrameIters[i] == _sourceFrames.end()) {
                return S_OK;
            }

            if (i < NUM_SRC_FRAMES_PER_PROCESSING - 1) {
                processSourceFrameIters[i + 1] = processSourceFrameIters[i];
                ++processSourceFrameIters[i + 1];
            }
        }
    }

    frameProps = AVSF_VS_API->getFramePropsRW(processSourceFrameIters[0]->second.frame);
    REFERENCE_TIME frameDurationNum = processSourceFrameIters[1]->second.startTime - processSourceFrameIters[0]->second.startTime;
    REFERENCE_TIME frameDurationDen = UNITS;
    vs_normalizeRational(&frameDurationNum, &frameDurationDen);
    AVSF_VS_API->propSetInt(frameProps, VS_PROP_NAME_DURATION_NUM, frameDurationNum, paReplace);
    AVSF_VS_API->propSetInt(frameProps, VS_PROP_NAME_DURATION_DEN, frameDurationDen, paReplace);
    _newSourceFrameCv.notify_all();

    const int64_t maxRequestOutputFrameNb = llMulDiv(processSourceFrameIters[0]->first,
                                                     MainFrameServer::GetInstance().GetSourceAvgFrameDuration(),
                                                     MainFrameServer::GetInstance().GetScriptAvgFrameDuration(),
                                                     0);
    while (_nextOutputFrameNb <= maxRequestOutputFrameNb) {
        // preemptively put frame data into the queue instead of passing a new pointer to the callback userdata
        // so that if the callback is not called due to frameserver shutdown, there won't be memory leak
        {
            std::unique_lock uniqueOutputLock(_outputMutex);

            _outputFrames.emplace(std::piecewise_construct,
                                   std::forward_as_tuple(_nextOutputFrameNb),
                                   std::forward_as_tuple(processSourceFrameIters[0]->first, processSourceFrameIters[0]->second.hdrSideData));
        }
        AVSF_VS_API->getFrameAsync(_nextOutputFrameNb, MainFrameServer::GetInstance().GetScriptClip(), VpsGetFrameCallback, this);

        _nextOutputFrameNb += 1;
    }

    _nextProcessSourceFrameNb = processSourceFrameIters[1]->first;

    return S_OK;
}

auto FrameHandler::GetSourceFrame(int frameNb) -> const VSFrameRef * {
    Environment::GetInstance().Log(L"Wait to get source frame: frameNb %6i input queue size %2zu", frameNb, _sourceFrames.size());

    std::shared_lock sharedSourceLock(_sourceMutex);

    decltype(_sourceFrames)::const_iterator iter;
    _newSourceFrameCv.wait(sharedSourceLock, [this, &iter, frameNb]() -> bool {
        if (_isFlushing) {
            return true;
        }

        // use map.lower_bound() in case the exact frame is removed by the script
        iter = _sourceFrames.lower_bound(frameNb);
        if (iter == _sourceFrames.end()) {
            return false;
        }

        const VSMap *frameProps = AVSF_VS_API->getFramePropsRO(iter->second.frame);
        return AVSF_VS_API->propNumElements(frameProps, VS_PROP_NAME_DURATION_NUM) > 0 && AVSF_VS_API->propNumElements(frameProps, VS_PROP_NAME_DURATION_DEN) > 0;
    });

    if (_isFlushing) {
        Environment::GetInstance().Log(L"Drain for frame %6i", frameNb);
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
    _deliverSampleCv.notify_all();

    // wait for pending Receive() to finish
    std::unique_lock uniqueReceiveLock(_filter.m_csReceive);

    Environment::GetInstance().Log(L"FrameHandler finish BeginFlush()");
}

auto FrameHandler::EndFlush(const std::function<void ()> &interim) -> void {
    Environment::GetInstance().Log(L"FrameHandler start EndFlush()");

    _isWorkerLatched.wait(false);

    {
        std::shared_lock sharedOutputLock(_outputMutex);

        _flushOutputSampleCv.wait(sharedOutputLock, [this]() {
            return std::ranges::all_of(_outputFrames | std::views::values, [](const OutputFrameData &data) {
                return data.frame!= nullptr;
            });
        });
    }

    if (interim) {
        interim();
    }

    _sourceFrames.clear();
    _outputFrames.clear();

    ResetInput();

    _isFlushing = false;
    _isFlushing.notify_all();

    Environment::GetInstance().Log(L"FrameHandler finish EndFlush()");
}

FrameHandler::SourceFrameInfo::~SourceFrameInfo() {
    AVSF_VS_API->freeFrame(frame);
}

FrameHandler::OutputFrameData::~OutputFrameData() {
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
    Environment::GetInstance().Log(L"Output frame %6i is ready, output queue size %2zu", n, frameHandler->_outputFrames.size());

    if (frameHandler->_isFlushing) {
        {
            std::unique_lock uniqueOutputLock(frameHandler->_outputMutex);

            frameHandler->_outputFrames.erase(n);
        }
        frameHandler->_flushOutputSampleCv.notify_all();

        AVSF_VS_API->freeFrame(f);
    } else {
        {
            std::shared_lock sharedOutputLock(frameHandler->_outputMutex);

            const decltype(frameHandler->_outputFrames)::iterator iter = frameHandler->_outputFrames.find(n);
            ASSERT(iter != frameHandler->_outputFrames.end());
            iter->second.frame = f;
        }
        frameHandler->_deliverSampleCv.notify_all();
    }
}

auto FrameHandler::ResetInput() -> void {
    _nextSourceFrameNb = 0;
    _nextProcessSourceFrameNb = 0;
    _nextOutputFrameNb = 0;
    _lastUsedSourceFrameNb = 0;
    _notifyChangedOutputMediaType = false;

    _frameRateCheckpointInputSampleNb = 0;
    _currentInputFrameRate = 0;
}

auto FrameHandler::PrepareOutputSample(ATL::CComPtr<IMediaSample> &sample, int frameNb, OutputFrameData &data) -> bool {
    const VSMap *frameProps = AVSF_VS_API->getFramePropsRO(data.frame);
    int propGetError;
    const int64_t frameDurationNum = AVSF_VS_API->propGetInt(frameProps, VS_PROP_NAME_DURATION_NUM, 0, &propGetError);
    const int64_t frameDurationDen = AVSF_VS_API->propGetInt(frameProps, VS_PROP_NAME_DURATION_DEN, 0, &propGetError);
    int64_t frameDuration;

    if (frameDurationNum > 0 && frameDurationDen > 0) {
        frameDuration = llMulDiv(frameDurationNum, UNITS, frameDurationDen, 0);
    } else {
        frameDuration = MainFrameServer::GetInstance().GetScriptAvgFrameDuration();
    }

    if (_nextOutputFrameStartTime == 0) {
        _nextOutputFrameStartTime = static_cast<REFERENCE_TIME>(AVSF_VS_API->propGetFloat(frameProps, VS_PROP_NAME_ABS_TIME, 0, &propGetError) * UNITS);
    }

    REFERENCE_TIME frameStartTime = _nextOutputFrameStartTime;
    REFERENCE_TIME frameStopTime = frameStartTime + frameDuration;
    _nextOutputFrameStartTime = frameStopTime;

    Environment::GetInstance().Log(L"Output frame: frameNb %6i startTime %10lli stopTime %10lli duration %10lli", frameNb, frameStartTime, frameStopTime, frameDuration);

    if (FAILED(_filter.m_pOutput->GetDeliveryBuffer(&sample, &frameStartTime, &frameStopTime, 0))) {
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

    if (FAILED(sample->SetTime(&frameStartTime, &frameStopTime))) {
        return false;
    }

    if (frameNb == 0 && FAILED(sample->SetDiscontinuity(TRUE))) {
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

    RefreshOutputFrameRates(frameNb);

    return true;
}

auto FrameHandler::WorkerProc() -> void {
    const auto ResetOutput = [this]() -> void {
        _nextOutputFrameStartTime = 0;
        _nextDeliveryFrameNb = 0;

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

        decltype(_outputFrames)::iterator iter;
        {
            std::shared_lock sharedOutputLock(_outputMutex);

            _deliverSampleCv.wait(sharedOutputLock, [this, &iter]() -> bool {
                if (_isFlushing) {
                    return true;
                }

                iter = _outputFrames.find(_nextDeliveryFrameNb);
                if (iter == _outputFrames.end()) {
                    return false;
                }

                return iter->second.frame != nullptr;
            });
        }

        if (_isFlushing) {
            continue;
        }

        _lastUsedSourceFrameNb = iter->second.sourceFrameNb;
        _addInputSampleCv.notify_all();

        if (ATL::CComPtr<IMediaSample> outputSample; PrepareOutputSample(outputSample, iter->first, iter->second)) {
            _filter.m_pOutput->Deliver(outputSample);
            RefreshDeliveryFrameRates(iter->first);

            Environment::GetInstance().Log(L"Delivered output sample %6i from source frame %6i", iter->first, iter->second.sourceFrameNb);
        }

        {
            std::unique_lock uniqueOutputLock(_outputMutex);

            _outputFrames.erase(iter);
        }

        GarbageCollect(_lastUsedSourceFrameNb - 1);
        _nextDeliveryFrameNb += 1;
    }

    _isWorkerLatched = true;
    _isWorkerLatched.notify_all();

    Environment::GetInstance().Log(L"Stop worker thread");
}

}
