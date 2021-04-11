// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "frame_handler.h"
#include "avs_handler.h"
#include "constants.h"
#include "environment.h"
#include "filter.h"
#include "format.h"


namespace AvsFilter {

FrameHandler::FrameHandler(CAviSynthFilter &filter)
    : _filter(filter) {
    ResetInput();
}

auto FrameHandler::AddInputSample(IMediaSample *inSample) -> HRESULT {
    HRESULT hr;
    REFERENCE_TIME inSampleStartTime;
    REFERENCE_TIME inSampleStopTime = 0;
    AM_MEDIA_TYPE *pmtIn = nullptr;

    {
        std::shared_lock sharedLock(_mutex);
        const MultiLock multiLock(_filter.m_csReceive, sharedLock);

        _addInputSampleCv.wait(multiLock, [this]() -> bool {
            if (_isFlushing) {
                return true;
            }

            // at least 3 source frames are needed in queue for stop time calculation
            if (_sourceFrames.size() < NUM_SRC_FRAMES_PER_PROCESSING) {
                return true;
            }

            // headroom added to make sure at least some frames are in input queue for frame time calculation
            if (_nextSourceFrameNb <= _maxRequestedFrameNb + g_env.GetExtraSourceBuffer()) {
                return true;
            }

            return false;
        });

        if (_isFlushing) {
            return S_OK;
        }

        hr = inSample->GetMediaType(&pmtIn);
        if (FAILED(hr)) {
            return hr;
        }
        if (hr != S_OK) {
            pmtIn = nullptr;
        }

        if (inSample->GetTime(&inSampleStartTime, &inSampleStopTime) == VFW_E_SAMPLE_TIME_NOT_SET) {
            // for samples without start time, always treat as fixed frame rate
            inSampleStartTime = _nextSourceFrameNb * g_avs->GetSourceAvgFrameDuration();
        }

        // since the key of _sourceFrames is frame number, which only strictly increases, rbegin() returns the last emplaced frame
        if (!_sourceFrames.empty() && inSampleStartTime <= _sourceFrames.crbegin()->second.startTime) {
            g_env.Log(L"Rejecting source sample due to start time not going forward: %10lli", inSampleStartTime);
            return VFW_E_SAMPLE_REJECTED;
        }

        if (_nextSourceFrameNb == 0) {
            _frameRateCheckpointInputSampleStartTime = inSampleStartTime;
        }

        RefreshInputFrameRates(_nextSourceFrameNb, inSampleStartTime);
    }

    BYTE *sampleBuffer;
    hr = inSample->GetPointer(&sampleBuffer);
    if (FAILED(hr)) {
        return hr;
    }

    const PVideoFrame avsFrame = Format::CreateFrame(_filter._inputVideoFormat, sampleBuffer, g_avs->GetMainScriptInstance().GetEnv());

    std::shared_ptr<HDRSideData> hdrSideData = std::make_shared<HDRSideData>();
    {
        if (const ATL::CComQIPtr<IMediaSideData> inSampleSideData(inSample); inSampleSideData != nullptr) {
            hdrSideData->ReadFrom(inSampleSideData);

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
                              std::forward_as_tuple(_nextSourceFrameNb, avsFrame, inSampleStartTime, SharedMediaTypePtr(pmtIn, MediaTypeDeleter), hdrSideData));

        g_env.Log(L"Stored source frame: %6i at %10lli ~ %10lli duration(literal) %10lli nextSourceFrameNb %6i",
                  _nextSourceFrameNb, inSampleStartTime, inSampleStopTime, inSampleStopTime - inSampleStartTime, _nextSourceFrameNb);

        _nextSourceFrameNb += 1;
    }
    _newSourceFrameCv.notify_all();

    return S_OK;
}

auto FrameHandler::GetSourceFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame {
    g_env.Log(L"Get source frame: frameNb %6i Input queue size %2zu", frameNb, _sourceFrames.size());

    std::shared_lock sharedLock(_mutex);

    _maxRequestedFrameNb = max(frameNb, _maxRequestedFrameNb.load());
    _addInputSampleCv.notify_one();

    std::map<int, SourceFrameInfo>::const_iterator iter;
    _newSourceFrameCv.wait(sharedLock, [this, &iter, frameNb]() -> bool {
        if (_isFlushing) {
            return true;
        }

        // use map.lower_bound() in case the exact frame is removed by the script
        iter = _sourceFrames.lower_bound(frameNb);
        return iter != _sourceFrames.cend();
    });

    ASSERT(_isFlushing || iter != _sourceFrames.cend());

    if (_isFlushing || iter->second.avsFrame == nullptr) {
        if (_isFlushing) {
            g_env.Log(L"Drain for frame %6i", frameNb);
        } else {
            g_env.Log(L"Bad frame %6i", frameNb);
        }

        return g_avs->GetSourceDrainFrame();
    }

    return iter->second.avsFrame;
}

auto FrameHandler::BeginFlush() -> void {
    _isFlushing = true;

    _addInputSampleCv.notify_all();
    _newSourceFrameCv.notify_all();
}

auto FrameHandler::EndFlush() -> void {
    g_env.Log(L"FrameHandler start EndFlush()");

    /*
     * EndFlush() can be called by either the application or the worker thread
     *
     * when called by former, we need to synchronize with the worker thread
     * when called by latter, current thread ID should be same as the worker thread, and no need for sync
     */
    if (std::this_thread::get_id() != _outputThread.get_id()) {
        _isWorkerThreadPaused.wait(false);
    }

    ResetInput();

    _isFlushing = false;
    _isFlushing.notify_all();

    g_env.Log(L"FrameHandler finish EndFlush()");
}

auto FrameHandler::StartWorkerThread() -> void {
    _isStopping = false;
    _isFlushing = false;

    _outputThread = std::thread(&FrameHandler::WorkerProc, this);
}

auto FrameHandler::StopWorkerThreads() -> void {
    _isStopping = true;

    if (_outputThread.joinable()) {
        _outputThread.join();
    }

    BeginFlush();
}

auto FrameHandler::GetInputBufferSize() const -> int {
    const std::shared_lock sharedLock(_mutex);

    return static_cast<int>(_sourceFrames.size());
}

auto FrameHandler::ResetInput() -> void {
    const std::unique_lock uniqueLock(_mutex);

    _sourceFrames.clear();

    _maxRequestedFrameNb = 0;
    _nextSourceFrameNb = 0;

    _frameRateCheckpointInputSampleNb = 0;
    _currentInputFrameRate = 0;
}

auto FrameHandler::ResetOutput() -> void {
    // to ensure these non-atomic properties are only modified by their sole consumer the worker thread
    ASSERT(!_outputThread.joinable() || std::this_thread::get_id() == _outputThread.get_id());

    _nextProcessSrcFrameNb = 0;
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

        g_env.Log(L"New output format: name %s, width %5li, height %5li",
                  _filter._outputVideoFormat.pixelFormat->name.c_str(), _filter._outputVideoFormat.bmi.biWidth, _filter._outputVideoFormat.bmi.biHeight);

        DeleteMediaType(pmtOut);
    }

    if (FAILED(sample->SetTime(&startTime, &stopTime))) {
        return false;
    }

    if (_nextOutputFrameNb == 0 && FAILED(sample->SetDiscontinuity(TRUE))) {
        return false;
    }

    if (BYTE *outBuffer; FAILED(sample->GetPointer(&outBuffer))) {
        return false;
    } else {
        try {
            // some AviSynth internal filter (e.g. Subtitle) can't tolerate multi-thread access
            const PVideoFrame scriptFrame = g_avs->GetMainScriptInstance().GetScriptClip()->GetFrame(_nextOutputFrameNb, g_avs->GetMainScriptInstance().GetEnv());
            Format::WriteSample(_filter._outputVideoFormat, scriptFrame, outBuffer, g_avs->GetMainScriptInstance().GetEnv());
        } catch (AvisynthError) {
            return false;
        }
    }

    return true;
}

auto FrameHandler::WorkerProc() -> void {
    g_env.Log(L"Start output worker thread");

#ifdef _DEBUG
    SetThreadDescription(GetCurrentThread(), L"CAviSynthFilter Output Worker");
#endif

    ResetOutput();

    while (!_isStopping) {
        if (_isFlushing) {
            _isWorkerThreadPaused = true;
            _isWorkerThreadPaused.notify_one();

            _isFlushing.wait(true);

            ResetOutput();
            _isWorkerThreadPaused = false;
        }

        if (_filter._changeOutputMediaType || _filter._reloadAvsSource) {
            g_env.Log(L"Upstream proposes to change input format: name %s, width %5li, height %5li",
                      _filter._inputVideoFormat.pixelFormat->name.c_str(), _filter._inputVideoFormat.bmi.biWidth, _filter._inputVideoFormat.bmi.biHeight);

            {
                const std::unique_lock receiveLock(_filter.m_csReceive);

                _filter.StopStreaming();

                BeginFlush();
                g_avs->GetMainScriptInstance().ReloadScript(_filter.m_pInput->CurrentMediaType(), true);
                EndFlush();

                ResetOutput();

                _filter._changeOutputMediaType = false;
                _filter._reloadAvsSource = false;
            }

            auto potentialOutputMediaTypes = _filter.InputToOutputMediaType(&_filter.m_pInput->CurrentMediaType());
            const auto newOutputMediaTypeIter = std::ranges::find_if(potentialOutputMediaTypes, [this](const CMediaType &outputMediaType) -> bool {
                /*
                 * "QueryAccept (Downstream)" forces the downstream to use the new output media type as-is, which may lead to wrong rendering result
                 * "ReceiveConnection" allows downstream to counter-propose suitable media type for the connection
                 * after ReceiveConnection(), the next output sample should carry the new output media type, which is handled in PrepareOutputSample()
                 */

                return SUCCEEDED(_filter.m_pOutput->GetConnected()->ReceiveConnection(_filter.m_pOutput, &outputMediaType));
            });

            if (newOutputMediaTypeIter == potentialOutputMediaTypes.end()) {
                g_env.Log(L"Downstream does not accept any of the new output media types. Terminate");
                _filter.AbortPlayback(VFW_E_TYPE_NOT_ACCEPTED);
                break;
            }

            {
                const std::unique_lock receiveLock(_filter.m_csReceive);

                _filter.StartStreaming();
            }
        }

        std::array<SourceFrameInfo, NUM_SRC_FRAMES_PER_PROCESSING> processSrcFrames;
        {
            std::shared_lock sharedLock(_mutex);

            _newSourceFrameCv.wait(sharedLock, [this, &processSrcFrames]() -> bool {
                if (_isFlushing) {
                    return true;
                }

                // use map.lower_bound() in case the exact frame is removed by the script
                std::map<int, SourceFrameInfo>::const_iterator iter = _sourceFrames.lower_bound(_nextProcessSrcFrameNb);

                for (int i = 0; i < NUM_SRC_FRAMES_PER_PROCESSING; ++i) {
                    if (iter == _sourceFrames.cend()) {
                        return false;
                    }

                    processSrcFrames[i] = iter->second;
                    ++iter;
                }

                if (_nextProcessSrcFrameNb == 0) {
                    _nextOutputFrameStartTime = processSrcFrames[0].startTime;
                    _frameRateCheckpointOutputFrameStartTime = processSrcFrames[0].startTime;
                }

                _nextProcessSrcFrameNb += 1;
                return true;
            });

            if (_isFlushing) {
                continue;
            }
        }

        /*
         * Some video decoders set the correct start time but the wrong stop time (stop time always being start time + average frame time).
         * Therefore instead of directly using the stop time from the current sample, we use the start time of the next sample.
         */

        const REFERENCE_TIME outputFrameDurationAfterEdge = llMulDiv(processSrcFrames[2].startTime - processSrcFrames[1].startTime, g_avs->GetMainScriptInstance().GetScriptAvgFrameDuration(), g_avs->GetSourceAvgFrameDuration(), 0);
        const REFERENCE_TIME outputFrameDurationBeforeEdge = llMulDiv(processSrcFrames[1].startTime - processSrcFrames[0].startTime, g_avs->GetMainScriptInstance().GetScriptAvgFrameDuration(), g_avs->GetSourceAvgFrameDuration(), 0);

        while (true) {
            const REFERENCE_TIME outFrameDurationBeforeEdgePortion = min(processSrcFrames[1].startTime - _nextOutputFrameStartTime, outputFrameDurationBeforeEdge);
            if (outFrameDurationBeforeEdgePortion <= 0) {
                g_env.Log(L"Frame time drift: %10lli", -outFrameDurationBeforeEdgePortion);
                break;
            }
            const REFERENCE_TIME outFrameDurationAfterEdgePortion = outputFrameDurationAfterEdge - llMulDiv(outputFrameDurationAfterEdge, outFrameDurationBeforeEdgePortion, outputFrameDurationBeforeEdge, 0);

            const REFERENCE_TIME outputStartTime = _nextOutputFrameStartTime;
            REFERENCE_TIME outputStopTime = outputStartTime + outFrameDurationBeforeEdgePortion + outFrameDurationAfterEdgePortion;
            if (outputStopTime < processSrcFrames[1].startTime && outputStopTime >= processSrcFrames[1].startTime - MAX_OUTPUT_FRAME_DURATION_PADDING) {
                outputStopTime = processSrcFrames[1].startTime;
            }
            _nextOutputFrameStartTime = outputStopTime;

            g_env.Log(L"Processing output frame %6i for source frame %6i at %10lli ~ %10lli duration %10lli",
                      _nextOutputFrameNb, processSrcFrames[0].frameNb, outputStartTime, outputStopTime, outputStopTime - outputStartTime);

            RefreshOutputFrameRates(_nextOutputFrameNb, outputStartTime);

            if (ATL::CComPtr<IMediaSample> outputSample; PrepareOutputSample(outputSample, outputStartTime, outputStopTime)) {
                if (const ATL::CComQIPtr<IMediaSideData> outSampleSideData(outputSample); outSampleSideData != nullptr) {
                    processSrcFrames[0].hdrSideData->WriteTo(outSampleSideData);
                }

                _filter.m_pOutput->Deliver(outputSample);
                g_env.Log(L"Delivered frame %6i", _nextOutputFrameNb);
            }

            _nextOutputFrameNb += 1;
        }

        GarbageCollect(processSrcFrames[0].frameNb);
    }

    g_env.Log(L"Stop output worker thread");
}

auto FrameHandler::GarbageCollect(int srcFrameNb) -> void {
    const std::unique_lock uniqueLock(_mutex);

    const size_t dbgPreSize = _sourceFrames.size();

    // search for all previous frames in case of some source frames are never used
    // this could happen by plugins that decrease frame rate
    while (!_sourceFrames.empty() && _sourceFrames.begin()->first <= srcFrameNb) {
        _sourceFrames.erase(_sourceFrames.begin());
    }

    _addInputSampleCv.notify_one();

    g_env.Log(L"GarbageCollect frames until %6i pre size %3zu post size %3zu", srcFrameNb, dbgPreSize, _sourceFrames.size());
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

auto FrameHandler::RefreshInputFrameRates(int frameNb, REFERENCE_TIME startTime) -> void {
    RefreshFrameRatesTemplate(frameNb, startTime, _frameRateCheckpointInputSampleNb, _frameRateCheckpointInputSampleStartTime, _currentInputFrameRate);
}

auto FrameHandler::RefreshOutputFrameRates(int frameNb, REFERENCE_TIME startTime) -> void {
    RefreshFrameRatesTemplate(frameNb, startTime, _frameRateCheckpointOutputFrameNb, _frameRateCheckpointOutputFrameStartTime, _currentOutputFrameRate);
}

}
