// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

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

        if (_nextSourceFrameNb <= Environment::GetInstance().GetInitialSrcBuffer()) {
            return true;
        }

        UpdateExtraSrcBuffer();

        // at least NUM_SRC_FRAMES_PER_PROCESSING source frames are needed in queue for stop time calculation
        if (static_cast<int>(_sourceFrames.size()) < NUM_SRC_FRAMES_PER_PROCESSING + _extraSrcBuffer) {
            return true;
        }

        return _nextSourceFrameNb <= _maxRequestedFrameNb;
    });

    if (_isFlushing || _isStopping) {
        Environment::GetInstance().Log(L"Reject input sample due to flush or stop");
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
            Environment::GetInstance().Log(L"Reject input sample due to start time going backward: curr %10lld last %10lld", inputSampleStartTime, lastSampleStartTime);
            return S_FALSE;
        }
    }

    RefreshInputFrameRates(_nextSourceFrameNb);

    BYTE *sampleBuffer;
    hr = inputSample->GetPointer(&sampleBuffer);
    if (FAILED(hr)) {
        return S_FALSE;
    }

    PVideoFrame frame = Format::CreateFrame(_filter._inputVideoFormat, sampleBuffer);

    if (FrameServerCommon::GetInstance().IsFramePropsSupported()) {
        AVSMap *frameProps = AVSF_AVS_API->getFramePropsRW(frame);

        AVSF_AVS_API->propSetFloat(frameProps, FRAME_PROP_NAME_ABS_TIME, inputSampleStartTime / static_cast<double>(UNITS), PROPAPPENDMODE_REPLACE);
        AVSF_AVS_API->propSetInt(frameProps, "_SARNum", _filter._inputVideoFormat.pixelAspectRatioNum, PROPAPPENDMODE_REPLACE);
        AVSF_AVS_API->propSetInt(frameProps, "_SARDen", _filter._inputVideoFormat.pixelAspectRatioDen, PROPAPPENDMODE_REPLACE);

        if (const std::optional<int> &optColorRange = _filter._inputVideoFormat.colorSpaceInfo.colorRange) {
            AVSF_AVS_API->propSetInt(frameProps, "_ColorRange", *optColorRange, PROPAPPENDMODE_REPLACE);
        }
        AVSF_AVS_API->propSetInt(frameProps, "_Primaries", _filter._inputVideoFormat.colorSpaceInfo.primaries, PROPAPPENDMODE_REPLACE);
        AVSF_AVS_API->propSetInt(frameProps, "_Matrix", _filter._inputVideoFormat.colorSpaceInfo.matrix, PROPAPPENDMODE_REPLACE);
        AVSF_AVS_API->propSetInt(frameProps, "_Transfer", _filter._inputVideoFormat.colorSpaceInfo.transfer, PROPAPPENDMODE_REPLACE);

        const DWORD typeSpecificFlags = _filter.m_pInput->SampleProps()->dwTypeSpecificFlags;
        // C++ lacks if-expression, so use IIFE to simulate
        const int rfpFieldBased = [&]() {
            if (typeSpecificFlags & AM_VIDEO_FLAG_WEAVE) {
                return VSFieldBased::VSC_FIELD_PROGRESSIVE;
            } else if (typeSpecificFlags & AM_VIDEO_FLAG_FIELD1FIRST) {
                return VSFieldBased::VSC_FIELD_TOP;
            } else {
                return VSFieldBased::VSC_FIELD_BOTTOM;
            }
        }();
        AVSF_AVS_API->propSetInt(frameProps, FRAME_PROP_NAME_FIELD_BASED, rfpFieldBased, PROPAPPENDMODE_REPLACE);
    }

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

    {
        const std::unique_lock uniqueSourceLock(_sourceMutex);

        _sourceFrames.emplace(std::piecewise_construct,
                              std::forward_as_tuple(_nextSourceFrameNb),
                              std::forward_as_tuple(frame, inputSampleStartTime, _filter.m_pInput->SampleProps()->dwTypeSpecificFlags, std::move(hdrSideData)));
        Environment::GetInstance().Log(L"Store source frame: %6d at %10lld ~ %10lld duration(literal) %10lld max_requested %6d extra_buffer %6d",
                                       _nextSourceFrameNb,
                                       inputSampleStartTime,
                                       inputSampleStopTime,
                                       inputSampleStopTime - inputSampleStartTime,
                                       _maxRequestedFrameNb.load(),
                                       _extraSrcBuffer);
        _nextSourceFrameNb += 1;
    }

    // delay activating the main frameserver until we have enough pre-buffered frames in store
    if (_nextSourceFrameNb == Environment::GetInstance().GetInitialSrcBuffer()) {
        MainFrameServer::GetInstance().ReloadScript(_filter.m_pInput->CurrentMediaType(), true);
    }

    _newSourceFrameCv.notify_all();

    return S_OK;
}

auto FrameHandler::GetSourceFrame(int frameNb) -> PVideoFrame {
    Environment::GetInstance().Log(L"Get source frame: frameNb %6d input queue size %2zd", frameNb, _sourceFrames.size());

    std::shared_lock sharedSourceLock(_sourceMutex);

    _maxRequestedFrameNb = std::max(frameNb, _maxRequestedFrameNb.load());
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
            Environment::GetInstance().Log(L"Drain for frame %6d", frameNb);
        } else {
            Environment::GetInstance().Log(L"Bad frame %6d", frameNb);
        }

        return MainFrameServer::GetInstance().CreateSourceDummyFrame();
    }

    Environment::GetInstance().Log(L"Return source frame %6d", frameNb);
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

auto FrameHandler::EndFlush() -> void {
    Environment::GetInstance().Log(L"FrameHandler start EndFlush()");

    ResetInput();

    _isFlushing = false;
    _isFlushing.notify_all();

    Environment::GetInstance().Log(L"FrameHandler finish EndFlush()");
}

auto FrameHandler::ResetInput() -> void {
    _sourceFrames.clear();

    _nextSourceFrameNb = 0;
    _maxRequestedFrameNb = 0;
    _notifyChangedOutputMediaType = false;
    _extraSrcBuffer = 0;

    _frameRateCheckpointInputSampleNb = 0;
    _currentInputFrameRate = 0;
}

auto FrameHandler::PrepareOutputSample(ATL::CComPtr<IMediaSample> &outSample, REFERENCE_TIME startTime, REFERENCE_TIME stopTime, DWORD sourceTypeSpecificFlags) -> bool {
    if (FAILED(_filter.m_pOutput->GetDeliveryBuffer(&outSample, &startTime, &stopTime, 0))) {
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

        Environment::GetInstance().Log(L"New output format: name %5ls, width %5ld, height %5ld",
                                       _filter._outputVideoFormat.pixelFormat->name,
                                       _filter._outputVideoFormat.bmi.biWidth,
                                       _filter._outputVideoFormat.bmi.biHeight);
    }

    if (FAILED(outSample->SetTime(&startTime, &stopTime))) {
        return false;
    }

    if (_nextOutputFrameNb == 0 && FAILED(outSample->SetDiscontinuity(TRUE))) {
        return false;
    }

    if (BYTE *outputBuffer; FAILED(outSample->GetPointer(&outputBuffer))) {
        return false;
    } else {
        try {
            if ((_filter._outputVideoFormat.outputBufferTemporalFlags & 0b11) == 0b01) {
                MEMORY_BASIC_INFORMATION dstBufferInfo;
                VirtualQuery(outputBuffer, &dstBufferInfo, sizeof(dstBufferInfo));
                _filter._outputVideoFormat.outputBufferTemporalFlags |= (((dstBufferInfo.Protect & PAGE_WRITECOMBINE) != 0) << 2) + 0b10;
            }

            // some AviSynth internal filter (e.g. Subtitle) can't tolerate multi-thread access
            const PVideoFrame outputFrame = MainFrameServer::GetInstance().GetFrame(_nextOutputFrameNb);

            if (const ATL::CComQIPtr<IMediaSample2> outSample2(outSample); outSample2 != nullptr) {
                if (AM_SAMPLE2_PROPERTIES sampleProps; SUCCEEDED(outSample2->GetProperties(SAMPLE2_TYPE_SPECIFIC_FLAGS_SIZE, reinterpret_cast<BYTE *>(&sampleProps)))) {
                    if (FrameServerCommon::GetInstance().IsFramePropsSupported()) {
                        const AVSMap *frameProps = AVSF_AVS_API->getFramePropsRO(outputFrame);
                        int propGetError;

                        if (const int64_t rfpFieldBased = AVSF_AVS_API->propGetInt(frameProps, FRAME_PROP_NAME_FIELD_BASED, 0, &propGetError);
                            propGetError == GETPROPERROR_UNSET || rfpFieldBased == 0) {
                            sampleProps.dwTypeSpecificFlags = AM_VIDEO_FLAG_WEAVE;
                        } else if (rfpFieldBased == 2) {
                            sampleProps.dwTypeSpecificFlags = AM_VIDEO_FLAG_FIELD1FIRST;
                        } else {
                            sampleProps.dwTypeSpecificFlags = 0;
                        }
                    } else {
                        // there is no way to convey interlace status without the "_FieldBased" frame property
                        // default to progressive to avoid unwanted deinterlacing
                        // TODO: remove this hack when support for AviSynth+ 3.5 is dropped
                        sampleProps.dwTypeSpecificFlags = AM_VIDEO_FLAG_WEAVE;
                    }

                    if (sourceTypeSpecificFlags & AM_VIDEO_FLAG_REPEAT_FIELD) {
                        sampleProps.dwTypeSpecificFlags |= AM_VIDEO_FLAG_REPEAT_FIELD;
                    }

                    outSample2->SetProperties(SAMPLE2_TYPE_SPECIFIC_FLAGS_SIZE, reinterpret_cast<BYTE *>(&sampleProps));
                }
            }

            Format::WriteSample(_filter._outputVideoFormat, outputFrame, outputBuffer);
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

        std::array<decltype(_sourceFrames)::iterator, NUM_SRC_FRAMES_PER_PROCESSING> processSourceFrameIters;
        std::array<REFERENCE_TIME, NUM_SRC_FRAMES_PER_PROCESSING - 1> outputFrameDurations;

        {
            std::shared_lock sharedSourceLock(_sourceMutex);

            _newSourceFrameCv.wait(sharedSourceLock, [&]() -> bool {
                if (_isFlushing) {
                    return true;
                }

                if (_nextSourceFrameNb <= Environment::GetInstance().GetInitialSrcBuffer()) {
                    return false;
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
            const REFERENCE_TIME outputFrameDurationBeforeEdgePortion = std::min(processSourceFrameIters[1]->second.startTime - _nextOutputFrameStartTime, outputFrameDurations[0]);
            if (outputFrameDurationBeforeEdgePortion <= 0) {
                Environment::GetInstance().Log(L"Frame time drift: %10lld", -outputFrameDurationBeforeEdgePortion);
                break;
            }
            const REFERENCE_TIME outputFrameDurationAfterEdgePortion = outputFrameDurations[1] - llMulDiv(outputFrameDurations[1], outputFrameDurationBeforeEdgePortion, outputFrameDurations[0], 0);

            const REFERENCE_TIME outputStartTime = _nextOutputFrameStartTime;
            REFERENCE_TIME outputStopTime = outputStartTime + outputFrameDurationBeforeEdgePortion + outputFrameDurationAfterEdgePortion;
            if (outputStopTime < processSourceFrameIters[1]->second.startTime && outputStopTime >= processSourceFrameIters[1]->second.startTime - MAX_OUTPUT_FRAME_DURATION_PADDING) {
                outputStopTime = processSourceFrameIters[1]->second.startTime;
            }
            _nextOutputFrameStartTime = outputStopTime;

            if (FrameServerCommon::GetInstance().IsFramePropsSupported()) {
                AVSMap *frameProps = AVSF_AVS_API->getFramePropsRW(processSourceFrameIters[0]->second.frame);
                REFERENCE_TIME frameDurationNum = processSourceFrameIters[1]->second.startTime - processSourceFrameIters[0]->second.startTime;
                REFERENCE_TIME frameDurationDen = UNITS;
                CoprimeIntegers(frameDurationNum, frameDurationDen);
                AVSF_AVS_API->propSetInt(frameProps, FRAME_PROP_NAME_DURATION_NUM, frameDurationNum, PROPAPPENDMODE_REPLACE);
                AVSF_AVS_API->propSetInt(frameProps, FRAME_PROP_NAME_DURATION_DEN, frameDurationDen, PROPAPPENDMODE_REPLACE);
            }

            Environment::GetInstance().Log(L"Processing output frame %6d for source frame %6d at %10lld ~ %10lld duration %10lld",
                                           _nextOutputFrameNb,
                                           processSourceFrameIters[0]->first,
                                           outputStartTime,
                                           outputStopTime,
                                           outputStopTime - outputStartTime);

            RefreshOutputFrameRates(_nextOutputFrameNb);

            if (ATL::CComPtr<IMediaSample> outSample; PrepareOutputSample(outSample, outputStartTime, outputStopTime, processSourceFrameIters[0]->second.typeSpecificFlags)) {
                if (const ATL::CComQIPtr<IMediaSideData> sideData(outSample); sideData != nullptr) {
                    processSourceFrameIters[0]->second.hdrSideData->WriteTo(sideData);
                }

                _filter.m_pOutput->Deliver(outSample);
                RefreshDeliveryFrameRates(_nextOutputFrameNb);

                Environment::GetInstance().Log(L"Deliver frame %6d", _nextOutputFrameNb);
            }

            _nextOutputFrameNb += 1;
        }

        GarbageCollect(processSourceFrameIters[0]->first);
    }

    Environment::GetInstance().Log(L"Stop worker thread");
}

}
