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
    : _filter(filter)
    , _flushGatekeeper(1)
    , _isFlushing(false)
    , _isStopping(false) {
    Reset();
}

auto FrameHandler::AddInputSample(IMediaSample *inSample) -> HRESULT {
    std::unique_lock srcLock(_sourceFramesMutex);

    _addInputSampleCv.wait(srcLock, [this]() -> bool {
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

    SourceFrameInfo srcFrameInfo { .frameNb = _nextSourceFrameNb };

    REFERENCE_TIME inSampleStopTime = 0;
    if (inSample->GetTime(&srcFrameInfo.startTime, &inSampleStopTime) == VFW_E_SAMPLE_TIME_NOT_SET) {
        // for samples without start time, always treat as fixed frame rate
        srcFrameInfo.startTime = _nextSourceFrameNb * g_avs->GetSourceAvgFrameDuration();
    }

    // since the key of _sourceFrames is frame number, which only strictly increases, rbegin() returns the last emplaced frame
    if (!_sourceFrames.empty() && srcFrameInfo.startTime <= _sourceFrames.rbegin()->second.startTime) {
        g_env.Log(L"Rejecting source sample due to start time not going forward: %10lli", srcFrameInfo.startTime);
        return VFW_E_SAMPLE_REJECTED;
    }

    if (_nextOutputFrameStartTime == -1) {
        _nextOutputFrameStartTime = srcFrameInfo.startTime;
    }
    if (_frameRateCheckpointInputSampleStartTime == -1) {
        _frameRateCheckpointInputSampleStartTime = srcFrameInfo.startTime;
    }
    if (_frameRateCheckpointOutputFrameStartTime == -1) {
        _frameRateCheckpointOutputFrameStartTime = srcFrameInfo.startTime;
    }

    RefreshInputFrameRates(srcFrameInfo.frameNb, srcFrameInfo.startTime);

    BYTE *sampleBuffer;
    const HRESULT hr = inSample->GetPointer(&sampleBuffer);
    if (FAILED(hr)) {
        return hr;
    }

    srcFrameInfo.avsFrame = Format::CreateFrame(_filter._inputVideoFormat, sampleBuffer, g_avs->GetEnv());

    {
        const ATL::CComQIPtr<IMediaSideData> inSampleSideData(inSample);
        if (inSampleSideData != nullptr) {
            srcFrameInfo.hdrSideData.ReadFrom(inSampleSideData);

            if (const std::optional<const BYTE *> optHdr = srcFrameInfo.hdrSideData.GetHDRData()) {
                _filter._inputVideoFormat.hdrType = 1;

                if (const std::optional<const BYTE *> optHdrCll = srcFrameInfo.hdrSideData.GetHDRContentLightLevelData()) {
                    _filter._inputVideoFormat.hdrLuminance = reinterpret_cast<const MediaSideDataHDRContentLightLevel *>(*optHdrCll)->MaxCLL;
                } else {
                    _filter._inputVideoFormat.hdrLuminance = static_cast<int>(reinterpret_cast<const MediaSideDataHDR *>(*optHdr)->max_display_mastering_luminance);
                }
            }
        }
    }

    _sourceFrames.emplace(_nextSourceFrameNb, srcFrameInfo).first;

    g_env.Log(L"Stored source frame: %6i at %10lli ~ %10lli duration(literal) %10lli nextSourceFrameNb %6i nextOutputFrameStartTime %10lli",
              _nextSourceFrameNb, srcFrameInfo.startTime, inSampleStopTime, inSampleStopTime - srcFrameInfo.startTime, _nextSourceFrameNb, _nextOutputFrameStartTime);

    _nextSourceFrameNb += 1;
    srcLock.unlock();
    _newSourceFrameCv.notify_all();

    return S_OK;
}

auto FrameHandler::GetSourceFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame {
    std::shared_lock srcLock(_sourceFramesMutex);

    _maxRequestedFrameNb = max(frameNb, _maxRequestedFrameNb);
    _addInputSampleCv.notify_one();

    std::map<int, SourceFrameInfo>::const_iterator iter;
    _newSourceFrameCv.wait(srcLock, [this, &iter, frameNb]() -> bool {
        if (_isFlushing) {
            return true;
        }

        // use map.lower_bound() in case the exact frame is removed by the script
        iter = _sourceFrames.lower_bound(frameNb);
        return iter != _sourceFrames.cend();
    });

    if (_isFlushing || iter->second.avsFrame == nullptr) {
        if (_isFlushing) {
            g_env.Log(L"Drain for frame %6i", frameNb);
        } else {
            g_env.Log(L"Bad frame %6i", frameNb);
        }

        return g_avs->GetSourceDrainFrame();
    }

    g_env.Log(L"Get source frame: frameNb %6i Input queue size %2zu", frameNb, _sourceFrames.size());

    return iter->second.avsFrame;
}

auto FrameHandler::Flush(const std::function<void ()> &interim) -> void {
    std::unique_lock flushLock(_flushMutex);

    g_env.Log(L"Frame handler start Flush()");

    _isFlushing = true;
    _addInputSampleCv.notify_all();
    _newSourceFrameCv.notify_all();

    g_env.Log(L"Frame handler wait for barriers");
    _flushGatekeeper.WaitForCount(flushLock);

    /*
     * The reason to stop avs script AFTER threads are paused is because otherwise some cached frames
     * might already be released while the threads are trying to get frame.
     *
     * The concern could be that prefetcher threads be stuck at trying to get frame but since threads
     * are paused, no new frame is allowed to be add in AddInputSample(). To unstuck, GetSourceFrame()
     * just returns a empty new frame during flush.
     */
    g_avs->StopScript();

    {
        std::unique_lock srcLock(_sourceFramesMutex);
        _sourceFrames.clear();
    }

    Reset();

    g_env.Log(L"Frame handler before calling interim");

    if (interim) {
        interim();
    }

    g_env.Log(L"Frame handler after calling interim");

    _isFlushing = false;
    flushLock.unlock();
    _flushGatekeeper.OpenGate();

    if (_isStopping) {
        g_env.Log(L"Frame handler cleanup after stop threads");

        if (_outputThread.joinable()) {
            _outputThread.join();
        }
    }

    g_env.Log(L"Frame handler finish Flush()");
}

auto FrameHandler::GetInputBufferSize() const -> int {
    const std::shared_lock srcLock(_sourceFramesMutex);

    return static_cast<int>(_sourceFrames.size());
}

auto FrameHandler::GetSourceFrameNb() const -> int {
    return _nextSourceFrameNb;
}

auto FrameHandler::GetOutputFrameNb() const -> int {
    return _nextOutputFrameNb;
}

auto FrameHandler::GetCurrentInputFrameRate() const -> int {
    return _currentInputFrameRate;
}

auto FrameHandler::GetCurrentOutputFrameRate() const -> int {
    return _currentOutputFrameRate;
}

auto FrameHandler::StartWorkerThread() -> void {
    _isStopping = false;
    _outputThread = std::thread(&FrameHandler::ProcessOutputSamples, this);
}

auto FrameHandler::StopWorkerThreads() -> void {
    _isStopping = true;
    Flush(nullptr);
}

auto FrameHandler::Reset() -> void {
    _maxRequestedFrameNb = 0;
    _nextSourceFrameNb = 0;
    _nextProcessSrcFrameNb = 0;
    _nextOutputFrameNb = 0;
    _nextOutputFrameStartTime = -1;

    _frameRateCheckpointInputSampleNb = 0;
    _frameRateCheckpointInputSampleStartTime = -1;
    _frameRateCheckpointOutputFrameNb = 0;
    _frameRateCheckpointOutputFrameStartTime = -1;
    _currentInputFrameRate = 0;
    _currentOutputFrameRate = 0;
}

auto FrameHandler::PrepareForDelivery(ATL::CComPtr<IMediaSample> &outSample, REFERENCE_TIME outputStartTime, REFERENCE_TIME outputStopTime) const -> bool {
    if (FAILED(_filter.InitializeOutputSample(nullptr, &outSample))) {
        // avoid releasing the invalid pointer in case the function change it to some random invalid address
        outSample.Detach();
        return false;
    }

    if (FAILED(outSample->SetTime(&outputStartTime, &outputStopTime))) {
        return false;
    }

    PVideoFrame scriptFrame;
    try {
        // some AviSynth internal filter (e.g. Subtitle) can't tolerate multi-thread access
        scriptFrame = g_avs->GetScriptClip()->GetFrame(_nextOutputFrameNb, g_avs->GetEnv());
    } catch (AvisynthError) {
        return false;
    }


    if (BYTE *outBuffer; FAILED(outSample->GetPointer(&outBuffer))) {
        return false;
    } else {
        Format::WriteSample(_filter._outputVideoFormat, scriptFrame, outBuffer, g_avs->GetEnv());
    }

    if (_filter._sendOutputVideoFormatInNextSample && SUCCEEDED(outSample->SetMediaType(&_filter.m_pOutput->CurrentMediaType()))) {
        _filter._sendOutputVideoFormatInNextSample = false;
    }

    return true;
}

auto FrameHandler::ProcessOutputSamples() -> void {
    g_env.Log(L"Start output worker thread");

#ifdef _DEBUG
    SetThreadDescription(GetCurrentThread(), L"CAviSynthFilter Output Worker");
#endif

    while (true) {
        if (_isFlushing) {
            g_env.Log(L"Output worker thread wait for flush");

            std::unique_lock flushLock(_flushMutex);
            _flushGatekeeper.ArriveAndWait(flushLock);

            if (_isStopping) {
                break;
            }
        }

        std::shared_lock srcLock(_sourceFramesMutex);

        std::array<const SourceFrameInfo *, NUM_SRC_FRAMES_PER_PROCESSING> processSrcFrames;
        _newSourceFrameCv.wait(srcLock, [this, &processSrcFrames]() -> bool {
            if (_isFlushing) {
                return true;
            }

            // use map.lower_bound() in case the exact frame is removed by the script
            std::map<int, SourceFrameInfo>::const_iterator iter = _sourceFrames.lower_bound(_nextProcessSrcFrameNb);

            for (int i = 0; i < NUM_SRC_FRAMES_PER_PROCESSING; ++i) {
                if (iter == _sourceFrames.cend()) {
                    return false;
                }

                processSrcFrames[i] = &iter->second;
                ++iter;
            }

            _nextProcessSrcFrameNb += 1;
            return true;
        });

        if (_isFlushing) {
            continue;
        }

        srcLock.unlock();

        /*
         * Some video decoders set the correct start time but the wrong stop time (stop time always being start time + average frame time).
         * Therefore instead of directly using the stop time from the current sample, we use the start time of the next sample.
         */

        const REFERENCE_TIME outputFrameDurationAfterEdge = llMulDiv(processSrcFrames[2]->startTime - processSrcFrames[1]->startTime, g_avs->GetScriptAvgFrameDuration(), g_avs->GetSourceAvgFrameDuration(), 0);
        const REFERENCE_TIME outputFrameDurationBeforeEdge = llMulDiv(processSrcFrames[1]->startTime - processSrcFrames[0]->startTime, g_avs->GetScriptAvgFrameDuration(), g_avs->GetSourceAvgFrameDuration(), 0);

        while (true) {
            const REFERENCE_TIME outFrameDurationBeforeEdgePortion = min(processSrcFrames[1]->startTime - _nextOutputFrameStartTime, outputFrameDurationBeforeEdge);
            if (outFrameDurationBeforeEdgePortion <= 0) {
                g_env.Log(L"Frame time drift: %10lli", -outFrameDurationBeforeEdgePortion);
                break;
            }
            const REFERENCE_TIME outFrameDurationAfterEdgePortion = outputFrameDurationAfterEdge - llMulDiv(outputFrameDurationAfterEdge, outFrameDurationBeforeEdgePortion, outputFrameDurationBeforeEdge, 0);

            const REFERENCE_TIME outputStartTime = _nextOutputFrameStartTime;
            REFERENCE_TIME outputStopTime = outputStartTime + outFrameDurationBeforeEdgePortion + outFrameDurationAfterEdgePortion;
            if (outputStopTime < processSrcFrames[1]->startTime && outputStopTime >= processSrcFrames[1]->startTime - MAX_OUTPUT_FRAME_DURATION_PADDING) {
                outputStopTime = processSrcFrames[1]->startTime;
            }
            _nextOutputFrameStartTime = outputStopTime;

            g_env.Log(L"Processing output frame %6i for source frame %6i at %10lli ~ %10lli duration %10lli",
                      _nextOutputFrameNb, processSrcFrames[0]->frameNb, outputStartTime, outputStopTime, outputStopTime - outputStartTime);

            RefreshOutputFrameRates(_nextOutputFrameNb, outputStartTime);

            if (ATL::CComPtr<IMediaSample> outSample; PrepareForDelivery(outSample, outputStartTime, outputStopTime)) {
                {
                    const ATL::CComQIPtr<IMediaSideData> outSampleSideData(outSample);
                    if (outSampleSideData != nullptr) {
                        processSrcFrames[0]->hdrSideData.WriteTo(outSampleSideData);
                    }
                }

                if (!_isFlushing) {
                    _filter.m_pOutput->Deliver(outSample);
                    g_env.Log(L"Delivered frame %6i", _nextOutputFrameNb);
                }
            }

            _nextOutputFrameNb += 1;
        }

        GarbageCollect(processSrcFrames[0]->frameNb);
    }

    g_env.Log(L"Stop output worker thread");
}

auto FrameHandler::GarbageCollect(int srcFrameNb) -> void {
    std::unique_lock srcLock(_sourceFramesMutex);

    const size_t dbgPreSize = _sourceFrames.size();

    // search for all previous frames in case of some source frames are never used
    // this could happen by plugins that decrease frame rate
    std::erase_if(_sourceFrames, [srcFrameNb](const std::pair<int, SourceFrameInfo> &entry) {
        return entry.first <= srcFrameNb;
    });

    srcLock.unlock();
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
