#include "pch.h"
#include "frame_handler.h"
#include "config.h"
#include "filter.h"
#include "format.h"


namespace AvsFilter {

FrameHandler::FrameHandler(CAviSynthFilter &filter)
    : _filter(filter)
    , _flushBarrier(g_config.GetOutputThreads())
    , _stopThreads(true)
    , _isFlushing(false) {
    Reset();
}

FrameHandler::~FrameHandler() {
    // not all upstreams call EndFlush at the end of stream, we need to always cleanup

    Flush();
}

auto FrameHandler::AddInputSample(IMediaSample *inSample) -> HRESULT {
    std::unique_lock srcLock(_sourceFramesMutex);

    _addInputSampleCv.wait(srcLock, [this]() {
        if (_isFlushing ||_stopThreads) {
            return true;
        }

        // need at least 2 source frames for stop time calculation
        if (_sourceFrames.size() <= 1) {
            return true;
        }

        // block upstream for flooding in samples until AviSynth actually requests them
        // +1 for headroom to avoid GetSourceFrame() being blocked
        if (_nextSourceFrameNb <= _maxRequestedFrameNb + 1) {
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
        srcFrameInfo.startTime = _nextSourceFrameNb * _filter._sourceAvgFrameTime;
    }

    RefreshInputFrameRates(srcFrameInfo);

    BYTE *sampleBuffer;
    const HRESULT hr = inSample->GetPointer(&sampleBuffer);
    if (FAILED(hr)) {
        return hr;
    }

    srcFrameInfo.avsFrame = Format::CreateFrame(_filter._inputFormat, sampleBuffer, _filter._avsEnv);

    IMediaSideData *inSampleSideData;
    if (SUCCEEDED(inSample->QueryInterface(&inSampleSideData))) {
        srcFrameInfo.hdrSideData.Read(inSampleSideData);
        inSampleSideData->Release();

        if (auto hdr = srcFrameInfo.hdrSideData.GetHDRData()) {
            _filter._inputFormat.hdrType = 1;

            if (auto hdrCll = srcFrameInfo.hdrSideData.GetContentLightLevelData()) {
                _filter._inputFormat.hdrLuminance = reinterpret_cast<const MediaSideDataHDRContentLightLevel *>(*hdrCll)->MaxCLL;
            } else {
                _filter._inputFormat.hdrLuminance = static_cast<int>(reinterpret_cast<const MediaSideDataHDR *>(*hdr)->max_display_mastering_luminance);
            }
        }
    }

    _sourceFrames.emplace(_nextSourceFrameNb, srcFrameInfo);

    g_config.Log("Processed source frame: %6i at %10lli ~ %10lli, nextSourceFrameNb %6i nextOutputFrameStartTime %10lli",
                 _nextSourceFrameNb, srcFrameInfo.startTime, inSampleStopTime, _nextSourceFrameNb, _nextOutputFrameStartTime);

    std::unordered_map<int, SourceFrameInfo>::iterator iter;
    if ((iter = _sourceFrames.find(_nextSourceFrameNb - 1)) != _sourceFrames.cend()) {
        /*
         * Some video decoders set the correct start time but the wrong stop time (stop time always being start time + average frame time).
         * Therefore instead of directly using the stop time from the current sample, we use the start time of the next sample.
         */

        SourceFrameInfo &preSrcFrameInfo = iter->second;
        const REFERENCE_TIME outputFrameTime = llMulDiv(srcFrameInfo.startTime - preSrcFrameInfo.startTime, _filter._scriptAvgFrameTime, _filter._sourceAvgFrameTime, 0);

        if (_nextOutputFrameStartTime < preSrcFrameInfo.startTime) {
            _nextOutputFrameStartTime = preSrcFrameInfo.startTime;
        }

        {
            std::unique_lock outLock(_outputFramesMutex);

            while (srcFrameInfo.startTime > _nextOutputFrameStartTime) {
                const REFERENCE_TIME outStartTime = _nextOutputFrameStartTime;
                const REFERENCE_TIME outStopTime = outStartTime + outputFrameTime;
                _nextOutputFrameStartTime = outStopTime;

                g_config.Log("Create output frame %6i for source frame %6i at %10lli ~ %10lli", _nextOutputFrameNb, preSrcFrameInfo.frameNb, outStartTime, outStopTime);

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

    std::unordered_map<int, SourceFrameInfo>::const_iterator iter;
    _newSourceFrameCv.wait(srcLock, [this, &iter, frameNb]() {
        if (_isFlushing) {
            return true;
        }

        iter = _sourceFrames.find(frameNb);
        return iter != _sourceFrames.cend();
    });

    if (_isFlushing || iter->second.avsFrame == nullptr) {
        if (_isFlushing) {
            g_config.Log("Drain for frame %6i", frameNb);
        } else {
            g_config.Log("Bad frame %6i", frameNb);
        }

        return env->NewVideoFrame(_filter._inputFormat.videoInfo);
    }

    g_config.Log("Get source frame: frameNb %6i Input queue size %2u", frameNb, _sourceFrames.size());

    return iter->second.avsFrame;
}

auto FrameHandler::Flush() -> void {
    g_config.Log("Frame handler begin flush");

    _isFlushing = true;
    _addInputSampleCv.notify_all();
    _newSourceFrameCv.notify_all();
    _outputFramesCv.notify_all();
    _deliveryCv.notify_all();

    _filter.StopAviSynthScript();

    if (_stopThreads) {
        g_config.Log("Frame handler end flush after stop threads");

        for (std::thread &t : _outputThreads) {
            if (t.joinable()) {
                t.join();
            }
        }
        _outputThreads.clear();
    } else {
        g_config.Log("Frame handler wait for barriers");

        _flushBarrier.Wait();
    }

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

    g_config.Log("Frame handler end flush");
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

    for (int i = 0; i < g_config.GetOutputThreads(); ++i) {
        _outputThreads.emplace_back(&FrameHandler::ProcessOutputSamples, this);
    }
}

auto FrameHandler::StopWorkerThreads() -> void {
    _stopThreads = true;

    // necessary to unlock output pin's Inactive() in CTransformFilter::Stop()
    _addInputSampleCv.notify_all();
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
    g_config.Log("Start output worker thread");

    SetThreadDescription(GetCurrentThread(), L"CAviSynthFilter Output Worker");

    while (!_stopThreads) {
        if (_isFlushing) {
            g_config.Log("Output worker thread wait for flush");

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

        g_config.Log("Start processing output frame %6i at %10lli ~ %10lli frameTime %10lli for source %6i Output queue size %2u Front %6i Back %6i",
                     outFrameInfo.frameNb, outFrameInfo.startTime, outFrameInfo.stopTime, outFrameInfo.stopTime - outFrameInfo.startTime, srcFrameNb,
                     _outputFrames.size(), _outputFrames.empty() ? -1 : _outputFrames.front().frameNb, _outputFrames.empty() ? -1 : _outputFrames.back().frameNb);

        bool doDelivery = false;

        IMediaSample *outSample = nullptr;
        if (FAILED(_filter.InitializeOutputSample(nullptr, &outSample))) {
            goto BEGIN_OF_DELIVERY;
        }

        if (FAILED(outSample->SetTime(&outFrameInfo.startTime, &outFrameInfo.stopTime))) {
            goto BEGIN_OF_DELIVERY;
        }

        {
            PVideoFrame scriptFrame;

            try {
                scriptFrame = _filter._avsScriptClip->GetFrame(outFrameInfo.frameNb, _filter._avsEnv);
            } catch (AvisynthError) {
                goto BEGIN_OF_DELIVERY;
            }

            BYTE *outBuffer;
            if (FAILED(outSample->GetPointer(&outBuffer))) {
                goto BEGIN_OF_DELIVERY;
            }
            Format::WriteSample(_filter._outputFormat, scriptFrame, outBuffer, _filter._avsEnv);
        }

        if (_filter._confirmNewOutputFormat && SUCCEEDED(outSample->SetMediaType(&_filter.m_pOutput->CurrentMediaType()))) {
            _filter._confirmNewOutputFormat = false;
        }

        IMediaSideData *outSampleSideData;
        if (SUCCEEDED(outSample->QueryInterface(&outSampleSideData))) {
            outFrameInfo.srcFrameInfo->hdrSideData.Write(outSampleSideData);
            outSampleSideData->Release();
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
                g_config.Log("Delivered frame %6i", outFrameInfo.frameNb);
            }
        }

        _nextDeliverFrameNb += 1;
        delLock.unlock();
        _deliveryCv.notify_all();

        if (outSample != nullptr) {
            outSample->Release();
        }

        GarbageCollect(srcFrameNb);
    }

    g_config.Log("Stop output worker thread");
}

auto FrameHandler::GarbageCollect(int srcFrameNb) -> void {
    std::unique_lock srcLock(_sourceFramesMutex);

    auto srcFrameIter = _sourceFrames.find(srcFrameNb);
    ASSERT(srcFrameIter != _sourceFrames.end());

    const int dbgPreRefCount = srcFrameIter->second.refCount;
    const int dbgPreQueueSize = static_cast<int>(_sourceFrames.size());

    if (srcFrameIter->second.refCount <= 1) {
        _sourceFrames.erase(srcFrameIter);
        srcLock.unlock();
        _addInputSampleCv.notify_one();
    } else {
        srcFrameIter->second.refCount -= 1;
    }

    g_config.Log("GarbageCollect frame %6i pre refcount %4i post queue size %2u", srcFrameNb, dbgPreRefCount, dbgPreQueueSize);
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
