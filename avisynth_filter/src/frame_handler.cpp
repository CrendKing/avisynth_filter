#include "pch.h"
#include "frame_handler.h"
#include "filter.h"
#include "format.h"
#include "logging.h"
#include "media_sample.h"


namespace AvsFilter {

#define ContinueHr(expr) { if (FAILED(expr)) { continue; } }

FrameHandler::FrameHandler(CAviSynthFilter &filter)
    : _filter(filter)
    , _inputFlushBarrier(filter._inputThreads)
    , _outputFlushBarrier(filter._outputThreads)
    , _stopWorkerThreads(true) {
    Reset();
}

FrameHandler::~FrameHandler() {
    // not all upstreams call BeginFlush/EndFlush at the end of stream, we need to always cleanup

    BeginFlush();
    EndFlush();
}

auto FrameHandler::AddInputSample(IMediaSample *inSample) -> void {
    std::unique_lock<std::mutex> srcLock(_sourceFramesMutex);

    while (true) {
        if (_isFlushing || _stopWorkerThreads) {
            break;
        }

        // need at least 2 source frames for stop time calculation
        if (_sourceFrames.size() <= 1) {
            break;
        }

        // block upstream for flooding in samples until AviSynth actually requests them
        // +1 for headroom to avoid GetSourceFrame() being blocked
        if (_nextSourceFrameNb <= _maxRequestedFrameNb + 1) {
            break;
        }

        _addInputSampleCv.wait(srcLock);
    }

    if (_isFlushing || _stopWorkerThreads) {
        return;
    }

    inSample->AddRef();
    _sourceFrames.emplace(_nextSourceFrameNb, SourceFrameInfo { _nextSourceFrameNb, inSample });

    Log("Add input sample %6i", _nextSourceFrameNb);

    _nextSourceFrameNb += 1;

    srcLock.unlock();
    _newSourceFrameCv.notify_one();
}

auto FrameHandler::GetSourceFrame(int frameNb) -> PVideoFrame {
    _maxRequestedFrameNb = max(frameNb, _maxRequestedFrameNb);
    _addInputSampleCv.notify_one();

    std::unique_lock<std::mutex> srcLock(_sourceFramesMutex);

    std::map<int, SourceFrameInfo>::const_iterator iter;
    while (true) {
        if (_isFlushing) {
            break;
        }

        iter = _sourceFrames.find(frameNb);
        if (iter != _sourceFrames.cend() && iter->second.avsFrame != nullptr) {
            break;
        }

        _sourceFrameAvailCv.wait(srcLock);
    }

    if (_isFlushing) {
        Log("Drain for frame: %6i", frameNb);
        return _sourceFrames.crbegin()->second.avsFrame;
    }

    Log("Get source frame: frameNb %6i Input queue size %2u Front %6i Back %6i",
        frameNb, _sourceFrames.size(), _sourceFrames.empty() ? -1 : _sourceFrames.cbegin()->first, _sourceFrames.empty() ? -1 : _sourceFrames.crbegin()->first);

    return iter->second.avsFrame;
}

auto FrameHandler::BeginFlush() -> void {
    Log("Frame handler begin flush");

    _isFlushing = true;

    _addInputSampleCv.notify_all();
    _newSourceFrameCv.notify_all();
    _sourceFrameAvailCv.notify_all();
    _outputFramesCv.notify_all();
    _deliveryCv.notify_all();
}

auto FrameHandler::EndFlush() -> void {
    _filter.StopAviSynthScript();

    if (_stopWorkerThreads) {
        Log("Frame handler end flush after stop threads");

        for (std::thread &t : _inputWorkerThreads) {
            t.join();
        }
        _inputWorkerThreads.clear();

        for (std::thread &t : _outputWorkerThreads) {
            t.join();
        }
        _outputWorkerThreads.clear();
    } else {
        Log("Frame handler wait for barriers");

        _inputFlushBarrier.Wait();
        _outputFlushBarrier.Wait();
    }

    Log("Frame handler done synchronization");

    std::unique_lock<std::mutex> srcLock(_sourceFramesMutex);

    for (auto &[frameNb, srcFrame]: _sourceFrames) {
        if (srcFrame.sample != nullptr) {
            srcFrame.sample->Release();
        }
    }
    _sourceFrames.clear();

    srcLock.unlock();

    std::unique_lock<std::mutex> outLock(_outputFramesMutex);
    _outputFrames.clear();
    outLock.unlock();

    if (_stopWorkerThreads) {
        _filter.DeleteAviSynth();
    }

    Reset();

    _inputFlushBarrier.Unlock();
    _outputFlushBarrier.Unlock();
}

auto FrameHandler::GetInputBufferSize() const -> int {
    const std::unique_lock<std::mutex> srcLock(_sourceFramesMutex);

    return static_cast<int>(_sourceFrames.size());
}

auto FrameHandler::GetOutputBufferSize() const -> int {
    const std::unique_lock<std::mutex> srcLock(_outputFramesMutex);

    return static_cast<int>(_outputFrames.size());
}

auto FrameHandler::GetSourceFrameNb() const -> int {
    return _nextSourceFrameNb;
}

auto FrameHandler::GetOutputFrameNb() const -> int {
    return _nextOutputFrameNb;
}

auto FrameHandler::GetDeliveryFrameNb() const -> int {
    return _deliveryFrameNb;
}

auto FrameHandler::GetCurrentInputFrameRate() const -> int {
    return _currentInputFrameRate;
}

auto FrameHandler::GetCurrentOutputFrameRate() const -> int {
    return _currentOutputFrameRate;
}

auto FrameHandler::GetInputWorkerThreadCount() const -> int {
    return static_cast<int>(_inputWorkerThreads.size());
}

auto FrameHandler::GetOutputWorkerThreadCount() const -> int {
    return static_cast<int>(_outputWorkerThreads.size());
}

auto FrameHandler::StartWorkerThreads() -> void {
    ASSERT(_inputWorkerThreads.empty());
    ASSERT(_outputWorkerThreads.empty());

    _stopWorkerThreads = false;

    for (int i = 0; i < _filter._inputThreads; ++i) {
        _inputWorkerThreads.emplace_back(&FrameHandler::ProcessInputSamples, this);
    }

    for (int i = 0; i < _filter._outputThreads; ++i) {
        _outputWorkerThreads.emplace_back(&FrameHandler::ProcessOutputSamples, this);
    }

    _filter.CreateAviSynth();
}

auto FrameHandler::StopWorkerThreads() -> void {
    _stopWorkerThreads = true;

    _addInputSampleCv.notify_all();
}

auto FrameHandler::Reset() -> void {
    _maxRequestedFrameNb = 0;
    _nextSourceFrameNb = 0;
    _processInputFrameNb = 0;
    _nextOutputFrameNb = 0;
    _deliveryFrameNb = 0;
    _nextOutputFrameStartTime = 0;
    _isFlushing = false;

    _frameRateCheckpointInputSampleNb = 0;
    _frameRateCheckpointInputSampleStartTime = 0;
    _frameRateCheckpointOutputFrameNb = 0;
    _frameRateCheckpointOutputFrameStartTime = 0;
    _currentInputFrameRate = 0;
    _currentOutputFrameRate = 0;
}

auto FrameHandler::ProcessInputSamples() -> void {
    Log("Start input sample worker thread %6i", std::this_thread::get_id());

    while (!_stopWorkerThreads) {
        if (_isFlushing) {
            Log("Input sample worker thread %6i arrive at barrier", std::this_thread::get_id());

            _inputFlushBarrier.Arrive();
        }

        std::unique_lock<std::mutex> srcLock(_sourceFramesMutex);

        std::map<int, SourceFrameInfo>::iterator iter;
        while (true) {
            if (_isFlushing) {
                break;
            }

            iter = _sourceFrames.find(_processInputFrameNb);
            if (iter != _sourceFrames.cend()) {
                break;
            }

            _newSourceFrameCv.wait(srcLock);
        }

        if (_isFlushing) {
            continue;
        }

        SourceFrameInfo &currSrcFrameInfo = iter->second;

        REFERENCE_TIME inSampleStopTime = 0;
        if (currSrcFrameInfo.sample->GetTime(&currSrcFrameInfo.startTime, &inSampleStopTime) == VFW_E_SAMPLE_TIME_NOT_SET) {
            // for samples without start time, always treat as fixed frame rate
            currSrcFrameInfo.startTime = currSrcFrameInfo.frameNb * _filter._sourceAvgFrameTime;
        }

        RefreshInputFrameRates(currSrcFrameInfo.frameNb, currSrcFrameInfo.startTime);

        BYTE *sampleBuffer;
        ContinueHr(currSrcFrameInfo.sample->GetPointer(&sampleBuffer));
        currSrcFrameInfo.avsFrame = Format::CreateFrame(_filter._inputFormat, sampleBuffer, _filter._avsEnv);

        IMediaSideData *inSampleSideData;

        if (SUCCEEDED(currSrcFrameInfo.sample->QueryInterface(&inSampleSideData))) {
            currSrcFrameInfo.hdrSideData.Read(inSampleSideData);
            inSampleSideData->Release();

            if (auto hdr = currSrcFrameInfo.hdrSideData.GetHDRData()) {
                _filter._inputFormat.hdrType = 1;

                if (auto hdrCll = currSrcFrameInfo.hdrSideData.GetContentLightLevelData()) {
                    _filter._inputFormat.hdrLuminance = reinterpret_cast<const MediaSideDataHDRContentLightLevel *>(*hdrCll)->MaxCLL;
                } else {
                    _filter._inputFormat.hdrLuminance = static_cast<int>(reinterpret_cast<const MediaSideDataHDR *>(*hdr)->max_display_mastering_luminance);
                }
            }
        }

        currSrcFrameInfo.sample->Release();
        currSrcFrameInfo.sample = nullptr;

        Log("Processed source frame: next %6i process %6i at %10lli ~ %10lli, nextOutputFrameStartTime %10lli",
            _nextSourceFrameNb, _processInputFrameNb, currSrcFrameInfo.startTime, inSampleStopTime, _nextOutputFrameStartTime);

        if ((iter = _sourceFrames.find(_processInputFrameNb - 1)) != _sourceFrames.cend()) {
            /*
             * Some video decoders set the correct start time but the wrong stop time (stop time always being start time + average frame time).
             * Therefore instead of directly using the stop time from the current sample, we use the start time of the next sample.
             */

            SourceFrameInfo &preSrcFrameInfo = iter->second;
            const REFERENCE_TIME prevSrcFrameTime = static_cast<REFERENCE_TIME>((currSrcFrameInfo.startTime - preSrcFrameInfo.startTime) * _filter._frameTimeScaling);

            if (_nextOutputFrameStartTime < preSrcFrameInfo.startTime) {
                _nextOutputFrameStartTime = preSrcFrameInfo.startTime;
            }

            std::unique_lock<std::mutex> outLock(_outputFramesMutex);

            while (currSrcFrameInfo.startTime - _nextOutputFrameStartTime > 0) {
                const REFERENCE_TIME outStartTime = _nextOutputFrameStartTime;
                const REFERENCE_TIME outStopTime = outStartTime + prevSrcFrameTime;
                _nextOutputFrameStartTime = outStopTime;

                _outputFrames.emplace_back(OutputFrameInfo { _nextOutputFrameNb, outStartTime, outStopTime, &preSrcFrameInfo });
                _nextOutputFrameNb += 1;
                preSrcFrameInfo.refCount += 1;
            }

            outLock.unlock();
            _outputFramesCv.notify_one();
        }

        srcLock.unlock();
        _sourceFrameAvailCv.notify_all();
        _processInputFrameNb += 1;
    }

    Log("Stop input sample worker thread %6i", std::this_thread::get_id());
}

auto FrameHandler::ProcessOutputSamples() -> void {
    Log("Start output sample worker thread %6i", std::this_thread::get_id());

    while (!_stopWorkerThreads) {
        if (_isFlushing) {
            Log("Output sample worker thread %6i arrive at barrier", std::this_thread::get_id());

            _outputFlushBarrier.Arrive();
        }

        std::unique_lock<std::mutex> outLock(_outputFramesMutex);

        while (!_isFlushing && _outputFrames.empty()) {
            _outputFramesCv.wait(outLock);
        }

        if (_isFlushing) {
            continue;
        }

        OutputFrameInfo outFrameInfo = _outputFrames.front();
        _outputFrames.pop_front();
        outLock.unlock();

        const int srcFrameNb = outFrameInfo.srcFrameInfo->frameNb;

        Log("Start processing output frame %6i at %10lli ~ %10lli frameTime %10lli for source %6i Output queue size %2u Front %6i Back %6i",
            outFrameInfo.frameNb, outFrameInfo.startTime, outFrameInfo.stopTime, outFrameInfo.stopTime - outFrameInfo.startTime, srcFrameNb,
            _outputFrames.size(), _outputFrames.empty() ? -1 : _outputFrames.front().frameNb, _outputFrames.empty() ? -1 : _outputFrames.back().frameNb);

        RefreshOutputFrameRates(outFrameInfo.frameNb, outFrameInfo.startTime);

        PVideoFrame scriptFrame;
        try {
            scriptFrame = _filter._avsScriptClip->GetFrame(outFrameInfo.frameNb, _filter._avsEnv);
        } catch (AvisynthError) {
            Log("AviSynth GetFrame() exception");
            continue;
        }

        IMediaSample *outSample;
        ContinueHr(_filter.InitializeOutputSample(nullptr, &outSample));

        if (FAILED(outSample->SetTime(&outFrameInfo.startTime, &outFrameInfo.stopTime))) {
            outSample->Release();
            continue;
        }

        if (_filter._confirmNewOutputFormat) {
            outSample->SetMediaType(&_filter.m_pOutput->CurrentMediaType());
            _filter._confirmNewOutputFormat = false;
        }

        BYTE *outBuffer;
        outSample->GetPointer(&outBuffer);
        Format::WriteSample(_filter._outputFormat, scriptFrame, outBuffer, _filter._avsEnv);

        IMediaSideData *outSampleSideData;
        if (SUCCEEDED(outSample->QueryInterface(&outSampleSideData))) {
            outFrameInfo.srcFrameInfo->hdrSideData.Write(outSampleSideData);
            outSampleSideData->Release();
        }

        std::unique_lock<std::mutex> delLock(_deliveryQueueMutex);

        while (!_isFlushing && outFrameInfo.frameNb != _deliveryFrameNb) {
            _deliveryCv.wait(delLock);
        }

        if (_isFlushing) {
            outSample->Release();
            continue;
        }

        ContinueHr(_filter.m_pOutput->Deliver(outSample));
        outSample->Release();
        _deliveryFrameNb += 1;

        delLock.unlock();
        _deliveryCv.notify_all();

        Log("Delivered frame %6i", outFrameInfo.frameNb);

        GarbageCollect(srcFrameNb);
    }

    Log("Stop output sample worker thread %6i", std::this_thread::get_id());
}

auto FrameHandler::GarbageCollect(int srcFrameNb) -> void {
    std::unique_lock<std::mutex> srcLock(_sourceFramesMutex);

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

    Log("GarbageCollect frame %6i pre refcount %4i post queue size %2u", srcFrameNb, dbgPreRefCount, dbgPreQueueSize);
}

auto FrameHandler::RefreshInputFrameRatesTemplate(int sampleNb, REFERENCE_TIME startTime,
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

auto FrameHandler::RefreshInputFrameRates(int sampleNb, REFERENCE_TIME startTime) -> void {
    RefreshInputFrameRatesTemplate(sampleNb, startTime, _frameRateCheckpointInputSampleNb, _frameRateCheckpointInputSampleStartTime, _currentInputFrameRate);
}

auto FrameHandler::RefreshOutputFrameRates(int sampleNb, REFERENCE_TIME startTime) -> void {
    RefreshInputFrameRatesTemplate(sampleNb, startTime, _frameRateCheckpointOutputFrameNb, _frameRateCheckpointOutputFrameStartTime, _currentOutputFrameRate);
}

}
