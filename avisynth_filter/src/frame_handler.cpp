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
    , _flushBarrier(g_env->GetOutputThreads())
    , _stopThreads(true)
    , _isFlushing(false) {
    Reset();
}

auto FrameHandler::AddInputSample(IMediaSample *inSample) -> HRESULT {
    std::unique_lock srcLock(_sourceFramesMutex);

    _addInputSampleCv.wait(srcLock, [this]() {
        if (_isFlushing || _stopThreads) {
            return true;
        }

        // need at least 2 source frames for stop time calculation
        if (_sourceFrames.size() <= 1) {
            return true;
        }

        // +1 to make sure at least 2 frames in input queue for frame time calculation
        if (_nextSourceFrameNb <= _maxRequestedFrameNb + 1 + MAX_SOURCE_FRAMES_AHEAD_OF_DELIVERY) {
            return true;
        }

        return false;
    });

    if (_isFlushing || _stopThreads) {
        return S_OK;
    }

    SourceFrameInfo srcFrameInfo { _nextSourceFrameNb };

    REFERENCE_TIME inSampleStopTime = 0;
    if (inSample->GetTime(&srcFrameInfo.startTime, &inSampleStopTime) == VFW_E_SAMPLE_TIME_NOT_SET) {
        // for samples without start time, always treat as fixed frame rate
        srcFrameInfo.startTime = _nextSourceFrameNb * g_avs->GetSourceAvgFrameTime();
    }

    RefreshInputFrameRates(srcFrameInfo);

    BYTE *sampleBuffer;
    const HRESULT hr = inSample->GetPointer(&sampleBuffer);
    if (FAILED(hr)) {
        return hr;
    }

    srcFrameInfo.avsFrame = Format::CreateFrame(_filter._inputFormat, sampleBuffer, g_avs->GetEnv());

    {
        const ATL::CComQIPtr<IMediaSideData> inSampleSideData(inSample);
        if (inSampleSideData != nullptr) {
            srcFrameInfo.hdrSideData.Read(inSampleSideData);

            if (const std::optional<const BYTE *> optHdr = srcFrameInfo.hdrSideData.GetHDRData()) {
                _filter._inputFormat.hdrType = 1;

                if (const std::optional<const BYTE *> optHdrCll = srcFrameInfo.hdrSideData.GetContentLightLevelData()) {
                    _filter._inputFormat.hdrLuminance = reinterpret_cast<const MediaSideDataHDRContentLightLevel *>(*optHdrCll)->MaxCLL;
                } else {
                    _filter._inputFormat.hdrLuminance = static_cast<int>(reinterpret_cast<const MediaSideDataHDR *>(*optHdr)->max_display_mastering_luminance);
                }
            }
        }
    }

    _sourceFrames.emplace(_nextSourceFrameNb, srcFrameInfo);

    g_env->Log("Processed source frame: %6i at %10lli ~ %10lli, nextSourceFrameNb %6i nextOutputFrameStartTime %10lli",
                 _nextSourceFrameNb, srcFrameInfo.startTime, inSampleStopTime, _nextSourceFrameNb, _nextOutputFrameStartTime);

    // we just emplaced an entry inside mutex, so map.lower_bound() should never return map::cend()
    const auto iter = --_sourceFrames.lower_bound(_nextSourceFrameNb);
    if (iter != _sourceFrames.cend()) {
        /*
         * Some video decoders set the correct start time but the wrong stop time (stop time always being start time + average frame time).
         * Therefore instead of directly using the stop time from the current sample, we use the start time of the next sample.
         */

        SourceFrameInfo &preSrcFrameInfo = iter->second;
        const REFERENCE_TIME outputFrameTime = llMulDiv(srcFrameInfo.startTime - preSrcFrameInfo.startTime, g_avs->GetScriptAvgFrameTime(), g_avs->GetSourceAvgFrameTime(), 0);

        if (_nextOutputFrameStartTime < preSrcFrameInfo.startTime) {
            _nextOutputFrameStartTime = preSrcFrameInfo.startTime;
        }

        {
            std::unique_lock outLock(_outputFramesMutex);

            while (srcFrameInfo.startTime > _nextOutputFrameStartTime) {
                const REFERENCE_TIME outStartTime = _nextOutputFrameStartTime;
                const REFERENCE_TIME outStopTime = outStartTime + outputFrameTime;
                _nextOutputFrameStartTime = outStopTime;

                g_env->Log("Create output frame %6i for source frame %6i at %10lli ~ %10lli", _nextOutputFrameNb, preSrcFrameInfo.frameNb, outStartTime, outStopTime);

                _outputFrames.emplace_back(OutputFrameInfo { _nextOutputFrameNb, outStartTime, outStopTime, &preSrcFrameInfo });
                _nextOutputFrameNb += 1;
                preSrcFrameInfo.refCount += 1;
            }
        }

        _outputFramesCv.notify_one();
    }

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
    _newSourceFrameCv.wait(srcLock, [this, &iter, frameNb]() {
        if (_isFlushing) {
            return true;
        }

        // use map.lower_bound() in case the exact frame is removed by the script
        iter = _sourceFrames.lower_bound(frameNb);
        return iter != _sourceFrames.cend();
    });

    if (_isFlushing || iter->second.avsFrame == nullptr) {
        if (_isFlushing) {
            g_env->Log("Drain for frame %6i", frameNb);
        } else {
            g_env->Log("Bad frame %6i", frameNb);
        }

        return env->NewVideoFrame(_filter._inputFormat.videoInfo);
    }

    g_env->Log("Get source frame: frameNb %6i Input queue size %2zu", frameNb, _sourceFrames.size());

    return iter->second.avsFrame;
}

auto FrameHandler::Flush() -> void {
    g_env->Log("Frame handler begin flush");

    _isFlushing = true;
    _addInputSampleCv.notify_all();
    _newSourceFrameCv.notify_all();
    _outputFramesCv.notify_all();
    _deliveryCv.notify_all();

    if (_stopThreads) {
        g_env->Log("Frame handler cleanup after stop threads");

        for (std::thread &t : _outputThreads) {
            if (t.joinable()) {
                t.join();
            }
        }
        _outputThreads.clear();
    } else {
        g_env->Log("Frame handler wait for barriers");

        _flushBarrier.Wait();
    }

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

    {
        std::unique_lock outLock(_outputFramesMutex);
        _outputFrames.clear();
    }

    Reset();
    _isFlushing = false;

    _flushBarrier.Unlock();

    g_env->Log("Frame handler end flush");
}

auto FrameHandler::GetInputBufferSize() const -> int {
    const std::shared_lock srcLock(_sourceFramesMutex);

    return static_cast<int>(_sourceFrames.size());
}

auto FrameHandler::GetOutputBufferSize() const -> int {
    const std::shared_lock outLock(_outputFramesMutex);

    return static_cast<int>(_outputFrames.size());
}

auto FrameHandler::GetSourceFrameNb() const -> int {
    return _nextSourceFrameNb;
}

auto FrameHandler::GetOutputFrameNb() const -> int {
    return _nextOutputFrameNb;
}

auto FrameHandler::GetDeliveryFrameNb() const -> int {
    return _nextDeliverFrameNb;
}

auto FrameHandler::GetCurrentInputFrameRate() const -> int {
    return _currentInputFrameRate;
}

auto FrameHandler::GetCurrentOutputFrameRate() const -> int {
    return _currentOutputFrameRate;
}

auto FrameHandler::GetOutputWorkerThreadCount() const -> int {
    return static_cast<int>(_outputThreads.size());
}

auto FrameHandler::StartWorkerThreads() -> void {
    ASSERT(_outputThreads.empty());
    
    _stopThreads = false;

    for (int i = 0; i < g_env->GetOutputThreads(); ++i) {
        _outputThreads.emplace_back(&FrameHandler::ProcessOutputSamples, this);
    }
}

auto FrameHandler::StopWorkerThreads() -> void {
    _stopThreads = true;

    // necessary to unlock output pin's Inactive() in CTransformFilter::Stop()
    _addInputSampleCv.notify_all();

    Flush();
}

auto FrameHandler::Reset() -> void {
    _maxRequestedFrameNb = 0;
    _nextSourceFrameNb = 0;
    _nextOutputFrameNb = 0;
    _nextDeliverFrameNb = 0;
    _nextOutputFrameStartTime = 0;

    _frameRateCheckpointInputSampleNb = 0;
    _frameRateCheckpointInputSampleStartTime = 0;
    _frameRateCheckpointOutputFrameNb = 0;
    _frameRateCheckpointOutputFrameStartTime = 0;
    _currentInputFrameRate = 0;
    _currentOutputFrameRate = 0;
}

auto FrameHandler::ProcessOutputSamples() -> void {
    g_env->Log("Start output worker thread");

#ifdef _DEBUG
    SetThreadDescription(GetCurrentThread(), L"CAviSynthFilter Output Worker");
#endif

    while (!_stopThreads) {
        if (_isFlushing) {
            g_env->Log("Output worker thread wait for flush");

            _flushBarrier.Arrive();
        }

        std::unique_lock outLock(_outputFramesMutex);

        _outputFramesCv.wait(outLock, [this]() {
            return _isFlushing || !_outputFrames.empty();
        });

        if (_isFlushing) {
            continue;
        }

        OutputFrameInfo outFrameInfo = _outputFrames.front();
        _outputFrames.pop_front();
        outLock.unlock();

        RefreshOutputFrameRates(outFrameInfo);

        const int srcFrameNb = outFrameInfo.srcFrameInfo->frameNb;

        g_env->Log("Start processing output frame %6i at %10lli ~ %10lli frameTime %10lli for source %6i Output queue size %2zu Front %6i Back %6i",
                     outFrameInfo.frameNb, outFrameInfo.startTime, outFrameInfo.stopTime, outFrameInfo.stopTime - outFrameInfo.startTime, srcFrameNb,
                     _outputFrames.size(), _outputFrames.empty() ? -1 : _outputFrames.front().frameNb, _outputFrames.empty() ? -1 : _outputFrames.back().frameNb);

        bool doDelivery = false;
        ATL::CComPtr<IMediaSample> outSample;

        if (FAILED(_filter.InitializeOutputSample(nullptr, &outSample))) {
            // reset it to nullptr here in case the function change it to some random invalid address
            outSample = nullptr;
            goto BEGIN_OF_DELIVERY;
        }

        if (FAILED(outSample->SetTime(&outFrameInfo.startTime, &outFrameInfo.stopTime))) {
            goto BEGIN_OF_DELIVERY;
        }

        {
            PVideoFrame scriptFrame;
            try {
                scriptFrame = g_avs->GetScriptClip()->GetFrame(outFrameInfo.frameNb, g_avs->GetEnv());
            } catch (AvisynthError) {
                goto BEGIN_OF_DELIVERY;
            }

            BYTE *outBuffer;
            if (FAILED(outSample->GetPointer(&outBuffer))) {
                goto BEGIN_OF_DELIVERY;
            }
            Format::WriteSample(_filter._outputFormat, scriptFrame, outBuffer, g_avs->GetEnv());
        }

        if (_filter._confirmNewOutputFormat && SUCCEEDED(outSample->SetMediaType(&_filter.m_pOutput->CurrentMediaType()))) {
            _filter._confirmNewOutputFormat = false;
        }

        {
            const ATL::CComQIPtr<IMediaSideData> outSampleSideData(outSample);
            if (outSampleSideData != nullptr) {
                outFrameInfo.srcFrameInfo->hdrSideData.Write(outSampleSideData);
            }
        }

        doDelivery = true;

BEGIN_OF_DELIVERY:
        std::unique_lock delLock(_deliveryMutex);

        if (doDelivery) {
            // most renderers require samples to be delivered in order
            // so we need to synchronize between the output threads

            _deliveryCv.wait(delLock, [this, &outFrameInfo]() {
                return _isFlushing || outFrameInfo.frameNb == _nextDeliverFrameNb;
            });

            if (!_isFlushing) {
                _filter.m_pOutput->Deliver(outSample);
                g_env->Log("Delivered frame %6i", outFrameInfo.frameNb);
            }
        }

        _nextDeliverFrameNb += 1;
        delLock.unlock();
        _deliveryCv.notify_all();

        GarbageCollect(srcFrameNb);
    }

    g_env->Log("Stop output worker thread");
}

auto FrameHandler::GarbageCollect(int srcFrameNb) -> void {
    std::unique_lock srcLock(_sourceFramesMutex);

    const size_t dbgPreSize = _sourceFrames.size();

    auto iter = _sourceFrames.begin();
    do {
        if (iter->first == srcFrameNb) {
            iter->second.refCount -= 1;
        }

        if (iter->second.refCount <= 0) {
            iter = _sourceFrames.erase(iter);
        } else {
            ++iter;
        }
    } while (iter != _sourceFrames.cend() && iter->first <= srcFrameNb);

    srcLock.unlock();
    _addInputSampleCv.notify_one();

    g_env->Log("GarbageCollect frames until %6i pre size %3zu post size %3zu", srcFrameNb, dbgPreSize, _sourceFrames.size());
}

auto FrameHandler::RefreshFrameRatesTemplate(int sampleNb, REFERENCE_TIME startTime,
                                             int &checkpointSampleNb, REFERENCE_TIME &checkpointStartTime,
                                             int &currentFrameRate) -> void {
    bool reachCheckpoint = checkpointStartTime == 0;
    const REFERENCE_TIME elapsedRefTime = startTime - checkpointStartTime;

    if (elapsedRefTime >= UNITS) {
        currentFrameRate = static_cast<int>(llMulDiv((static_cast<LONGLONG>(sampleNb) - checkpointSampleNb) * FRAME_RATE_SCALE_FACTOR, UNITS, elapsedRefTime, 0));
        reachCheckpoint = true;
    }

    if (reachCheckpoint) {
        checkpointSampleNb = sampleNb;
        checkpointStartTime = startTime;
    }
}

auto FrameHandler::RefreshInputFrameRates(const SourceFrameInfo &info) -> void {
    RefreshFrameRatesTemplate(info.frameNb, info.startTime, _frameRateCheckpointInputSampleNb, _frameRateCheckpointInputSampleStartTime, _currentInputFrameRate);
}

auto FrameHandler::RefreshOutputFrameRates(const OutputFrameInfo &info) -> void {
    RefreshFrameRatesTemplate(info.frameNb, info.startTime, _frameRateCheckpointOutputFrameNb, _frameRateCheckpointOutputFrameStartTime, _currentOutputFrameRate);
}

}
