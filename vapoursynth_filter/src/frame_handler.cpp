// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "frame_handler.h"

#include "constants.h"
#include "filter.h"


namespace SynthFilter {

auto FrameHandler::AddInputSample(IMediaSample *inputSample) -> HRESULT {
    HRESULT hr;

    UpdateExtraSrcBuffer();

    _addInputSampleCv.wait(_filter.m_csReceive, [this]() -> bool {
        if (_isFlushing) {
            return true;
        }

        // at least NUM_SRC_FRAMES_PER_PROCESSING source frames are needed in queue for stop time calculation
        if (static_cast<int>(_sourceFrames.size()) < NUM_SRC_FRAMES_PER_PROCESSING + _extraSrcBuffer) {
            return true;
        }

        // add headroom to avoid blocking and context switch
        return _nextSourceFrameNb <= _lastUsedSourceFrameNb + NUM_SRC_FRAMES_PER_PROCESSING + 1;
    });

    if (_isFlushing || _isStopping) {
        return S_FALSE;
    }

    if ((_filter._isInputMediaTypeChanged || _filter._needReloadScript) && !ChangeOutputFormat()) {
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
            Environment::GetInstance().Log(L"Rejecting source sample due to start time going backward: curr %10lld last %10lld", inputSampleStartTime, lastSampleStartTime);
            return S_FALSE;
        }
    }

    RefreshInputFrameRates(_nextSourceFrameNb);

    BYTE *sampleBuffer;
    hr = inputSample->GetPointer(&sampleBuffer);
    if (FAILED(hr)) {
        return S_FALSE;
    }

    VSFrame *frame = Format::CreateFrame(_filter._inputVideoFormat, sampleBuffer);
    VSMap *frameProps = AVSF_VPS_API->getFramePropertiesRW(frame);

    AVSF_VPS_API->mapSetFloat(frameProps, FRAME_PROP_NAME_ABS_TIME, inputSampleStartTime / static_cast<double>(UNITS), maReplace);
    AVSF_VPS_API->mapSetInt(frameProps, "_SARNum", _filter._inputVideoFormat.pixelAspectRatioNum, maReplace);
    AVSF_VPS_API->mapSetInt(frameProps, "_SARDen", _filter._inputVideoFormat.pixelAspectRatioDen, maReplace);
    AVSF_VPS_API->mapSetInt(frameProps, FRAME_PROP_NAME_SOURCE_FRAME_NB, _nextSourceFrameNb, maReplace);

    if (const std::optional<int> &optColorRange = _filter._inputVideoFormat.colorSpaceInfo.colorRange) {
        AVSF_VPS_API->mapSetInt(frameProps, "_ColorRange", *optColorRange, maReplace);
    }
    AVSF_VPS_API->mapSetInt(frameProps, "_Primaries", _filter._inputVideoFormat.colorSpaceInfo.primaries, maReplace);
    AVSF_VPS_API->mapSetInt(frameProps, "_Matrix", _filter._inputVideoFormat.colorSpaceInfo.matrix, maReplace);
    AVSF_VPS_API->mapSetInt(frameProps, "_Transfer", _filter._inputVideoFormat.colorSpaceInfo.transfer, maReplace);

    const DWORD typeSpecificFlags = _filter.m_pInput->SampleProps()->dwTypeSpecificFlags;
    int rfpFieldBased;
    if (typeSpecificFlags & AM_VIDEO_FLAG_WEAVE) {
        rfpFieldBased = VSFieldBased::VSC_FIELD_PROGRESSIVE;
    } else if (typeSpecificFlags & AM_VIDEO_FLAG_FIELD1FIRST) {
        rfpFieldBased = VSFieldBased::VSC_FIELD_TOP;
    } else {
        rfpFieldBased = VSFieldBased::VSC_FIELD_BOTTOM;
    }
    AVSF_VPS_API->mapSetInt(frameProps, FRAME_PROP_NAME_FIELD_BASED, rfpFieldBased, maReplace);
    AVSF_VPS_API->mapSetInt(frameProps, FRAME_PROP_NAME_TYPE_SPECIFIC_FLAGS, typeSpecificFlags, maReplace);

    std::unique_ptr<HDRSideData> hdrSideData = std::make_unique<HDRSideData>();
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
                              std::forward_as_tuple(frame, inputSampleStartTime, std::move(hdrSideData)));
    }

    Environment::GetInstance().Log(L"Stored source frame: %6d at %10lld ~ %10lld duration(literal) %10lld, last_used %6d, extra_buffer %6d",
                                   _nextSourceFrameNb,
                                   inputSampleStartTime,
                                   inputSampleStopTime,
                                   inputSampleStopTime - inputSampleStartTime,
                                   _lastUsedSourceFrameNb.load(),
                                   _extraSrcBuffer);

    _nextSourceFrameNb += 1;

    /*
     * Some video decoders set the correct start time but the wrong stop time (stop time always being start time + average frame time).
     * Therefore instead of directly using the stop time from the current sample, we use the start time of the next sample.
     */

    // use map.lower_bound() in case the exact frame is removed by the script
    std::array<decltype(_sourceFrames)::const_iterator, NUM_SRC_FRAMES_PER_PROCESSING> processSourceFrameIters { _sourceFrames.lower_bound(_nextProcessSourceFrameNb) };

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
    _nextProcessSourceFrameNb = processSourceFrameIters[1]->first;

    frameProps = AVSF_VPS_API->getFramePropertiesRW(processSourceFrameIters[0]->second.autoFrame.frame);
    REFERENCE_TIME frameDurationNum = processSourceFrameIters[1]->second.startTime - processSourceFrameIters[0]->second.startTime;
    REFERENCE_TIME frameDurationDen = UNITS;
    CoprimeIntegers(frameDurationNum, frameDurationDen);
    AVSF_VPS_API->mapSetInt(frameProps, FRAME_PROP_NAME_DURATION_NUM, frameDurationNum, maReplace);
    AVSF_VPS_API->mapSetInt(frameProps, FRAME_PROP_NAME_DURATION_DEN, frameDurationDen, maReplace);
    _newSourceFrameCv.notify_all();

    const int maxRequestOutputFrameNb = static_cast<int>(llMulDiv(processSourceFrameIters[0]->first,
                                                                  MainFrameServer::GetInstance().GetSourceAvgFrameDuration(),
                                                                  MainFrameServer::GetInstance().GetScriptAvgFrameDuration(),
                                                                  0));
    while (_nextOutputFrameNb <= maxRequestOutputFrameNb) {
        // before every async request to a frame, we need to keep track of the request so that when flushing we can wait for
        // any pending request to finish before destroying the script

        {
            std::unique_lock uniqueOutputLock(_outputMutex);

            _outputFrames.emplace(_nextOutputFrameNb, nullptr);
        }
        AVSF_VPS_API->getFrameAsync(_nextOutputFrameNb, MainFrameServer::GetInstance().GetScriptClip(), VpsGetFrameCallback, this);

        _nextOutputFrameNb += 1;
    }

    return S_OK;
}

auto FrameHandler::GetSourceFrame(int frameNb) -> const VSFrame * {
    Environment::GetInstance().Log(L"Waiting for source frame: frameNb %6d input queue size %2zd", frameNb, _sourceFrames.size());

    if (!_filter._isReadyToReceive) {
        Environment::GetInstance().Log(L"Frame %6d is requested before filter is ready to receive", frameNb);
        return FrameServerCommon::GetInstance().CreateSourceDummyFrame(MainFrameServer::GetInstance().GetVsCore());
    }

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

        const VSMap *frameProps = AVSF_VPS_API->getFramePropertiesRO(iter->second.autoFrame.frame);
        return AVSF_VPS_API->mapNumElements(frameProps, FRAME_PROP_NAME_DURATION_NUM) > 0 && AVSF_VPS_API->mapNumElements(frameProps, FRAME_PROP_NAME_DURATION_DEN) > 0;
    });

    if (_isFlushing) {
        Environment::GetInstance().Log(L"Drain for frame %6d", frameNb);
        return FrameServerCommon::GetInstance().CreateSourceDummyFrame(MainFrameServer::GetInstance().GetVsCore());
    }

    Environment::GetInstance().Log(L"Return source frame %6d", frameNb);
    return iter->second.autoFrame.frame;
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
            return std::ranges::all_of(
                _outputFrames | std::views::values,
                [](const VSFrame *frame) {
                    return frame != nullptr;
                },
                &AutoReleaseVSFrame::frame);
        });
    }

    if (interim) {
        interim();
    }

    // only the current thread is active here, no need to lock

    _outputFrames.clear();
    _sourceFrames.clear();
    ResetInput();

    _isFlushing = false;
    _isFlushing.notify_all();

    Environment::GetInstance().Log(L"FrameHandler finish EndFlush()");
}

auto VS_CC FrameHandler::VpsGetFrameCallback(void *userData, const VSFrame *f, int n, VSNode *node, const char *errorMsg) -> void {
    if (f == nullptr) {
        Environment::GetInstance().Log(L"Failed to generate output frame %6d with message: %hs", n, errorMsg);
        return;
    }

    FrameHandler *frameHandler = static_cast<FrameHandler *>(userData);
    Environment::GetInstance().Log(L"Output frame %6d is ready, output queue size %2zd", n, frameHandler->_outputFrames.size());

    if (frameHandler->_isFlushing) {
        {
            std::unique_lock uniqueOutputLock(frameHandler->_outputMutex);

            frameHandler->_outputFrames.erase(n);
        }
        frameHandler->_flushOutputSampleCv.notify_all();

        AVSF_VPS_API->freeFrame(f);
    } else {
        {
            std::shared_lock sharedOutputLock(frameHandler->_outputMutex);

            frameHandler->_outputFrames[n] = const_cast<VSFrame *>(f);
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

auto FrameHandler::PrepareOutputSample(ATL::CComPtr<IMediaSample> &outSample, int outputFrameNb, const VSFrame *outputFrame, int sourceFrameNb) -> bool {
    const VSMap *frameProps = AVSF_VPS_API->getFramePropertiesRO(outputFrame);
    int propGetError;
    const int64_t frameDurationNum = AVSF_VPS_API->mapGetInt(frameProps, FRAME_PROP_NAME_DURATION_NUM, 0, &propGetError);
    const int64_t frameDurationDen = AVSF_VPS_API->mapGetInt(frameProps, FRAME_PROP_NAME_DURATION_DEN, 0, &propGetError);
    int64_t frameDuration;

    if (frameDurationNum > 0 && frameDurationDen > 0) {
        frameDuration = llMulDiv(frameDurationNum, UNITS, frameDurationDen, 0);
    } else {
        frameDuration = MainFrameServer::GetInstance().GetScriptAvgFrameDuration();
    }

    if (_nextOutputFrameStartTime == 0) {
        _nextOutputFrameStartTime = static_cast<REFERENCE_TIME>(AVSF_VPS_API->mapGetFloat(frameProps, FRAME_PROP_NAME_ABS_TIME, 0, &propGetError) * UNITS);
    }

    REFERENCE_TIME frameStartTime = _nextOutputFrameStartTime;
    REFERENCE_TIME frameStopTime = frameStartTime + frameDuration;
    _nextOutputFrameStartTime = frameStopTime;

    Environment::GetInstance().Log(L"Output frame: frameNb %6d startTime %10lld stopTime %10lld duration %10lld", outputFrameNb, frameStartTime, frameStopTime, frameDuration);

    if (FAILED(_filter.m_pOutput->GetDeliveryBuffer(&outSample, &frameStartTime, &frameStopTime, 0))) {
        // avoid releasing the invalid pointer in case the function change it to some random invalid address
        outSample.Detach();
        return false;
    }

    AM_MEDIA_TYPE *pmtOut;
    outSample->GetMediaType(&pmtOut);

    if (const std::shared_ptr<AM_MEDIA_TYPE> pmtOutPtr(pmtOut, &DeleteMediaType);
        pmtOut != nullptr && pmtOut->pbFormat != nullptr) {
        _filter.m_pOutput->SetMediaType(static_cast<CMediaType *>(pmtOut));
        _filter._outputVideoFormat = Format::GetVideoFormat(*pmtOut, &MainFrameServer::GetInstance());
        _notifyChangedOutputMediaType = true;
    }

    if (_notifyChangedOutputMediaType) {
        outSample->SetMediaType(&_filter.m_pOutput->CurrentMediaType());
        _notifyChangedOutputMediaType = false;

        Environment::GetInstance().Log(L"New output format: name %s, width %5ld, height %5ld",
                                       _filter._outputVideoFormat.pixelFormat->name,
                                       _filter._outputVideoFormat.bmi.biWidth,
                                       _filter._outputVideoFormat.bmi.biHeight);
    }

    if (FAILED(outSample->SetTime(&frameStartTime, &frameStopTime))) {
        return false;
    }

    if (outputFrameNb == 0 && FAILED(outSample->SetDiscontinuity(TRUE))) {
        return false;
    }

    BYTE *outputBuffer;
    if (FAILED(outSample->GetPointer(&outputBuffer))) {
        return false;
    }

    if (const ATL::CComQIPtr<IMediaSample2> outSample2(outSample); outSample2 != nullptr) {
        if (AM_SAMPLE2_PROPERTIES sampleProps; SUCCEEDED(outSample2->GetProperties(SAMPLE2_TYPE_SPECIFIC_FLAGS_SIZE, reinterpret_cast<BYTE *>(&sampleProps)))) {
            if (const int64_t rfpFieldBased = AVSF_VPS_API->mapGetInt(frameProps, FRAME_PROP_NAME_FIELD_BASED, 0, &propGetError);
                propGetError == peUnset || rfpFieldBased == 0) {
                sampleProps.dwTypeSpecificFlags = AM_VIDEO_FLAG_WEAVE;
            } else if (rfpFieldBased == 2) {
                sampleProps.dwTypeSpecificFlags = AM_VIDEO_FLAG_FIELD1FIRST;
            } else {
                sampleProps.dwTypeSpecificFlags = 0;
            }

            if (const int64_t sourceTypeSpecificFlags = AVSF_VPS_API->mapGetInt(frameProps, FRAME_PROP_NAME_TYPE_SPECIFIC_FLAGS, 0, &propGetError);
                sourceTypeSpecificFlags & AM_VIDEO_FLAG_REPEAT_FIELD) {
                sampleProps.dwTypeSpecificFlags |= AM_VIDEO_FLAG_REPEAT_FIELD;
            }

            outSample2->SetProperties(SAMPLE2_TYPE_SPECIFIC_FLAGS_SIZE, reinterpret_cast<BYTE *>(&sampleProps));
        }
    }

    Format::WriteSample(_filter._outputVideoFormat, outputFrame, outputBuffer);

    const auto iter = _sourceFrames.find(sourceFrameNb);
    ASSERT(iter != _sourceFrames.end());

    if (const ATL::CComQIPtr<IMediaSideData> sideData(outSample); sideData != nullptr) {
        iter->second.hdrSideData->WriteTo(sideData);
    }

    RefreshOutputFrameRates(outputFrameNb);

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

        const VSMap *frameProps = AVSF_VPS_API->getFramePropertiesRO(iter->second.frame);
        int propGetError;
        const int sourceFrameNb = static_cast<int>(AVSF_VPS_API->mapGetInt(frameProps, FRAME_PROP_NAME_SOURCE_FRAME_NB, 0, &propGetError));

        _lastUsedSourceFrameNb = sourceFrameNb;
        _addInputSampleCv.notify_all();

        if (ATL::CComPtr<IMediaSample> outSample; PrepareOutputSample(outSample, iter->first, iter->second.frame, sourceFrameNb)) {
            _filter.m_pOutput->Deliver(outSample);
            RefreshDeliveryFrameRates(iter->first);

            Environment::GetInstance().Log(L"Delivered output sample %6d from source frame %6d", iter->first, sourceFrameNb);
        }

        {
            std::unique_lock uniqueOutputLock(_outputMutex);

            _outputFrames.erase(iter);
        }

        GarbageCollect(sourceFrameNb - 1);
        _nextDeliveryFrameNb += 1;
    }

    _isWorkerLatched = true;
    _isWorkerLatched.notify_all();

    Environment::GetInstance().Log(L"Stop worker thread");
}

}
