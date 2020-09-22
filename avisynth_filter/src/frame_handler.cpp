#include "pch.h"
#include "frame_handler.h"
#include "constants.h"
#include "filter.h"
#include "format.h"
#include "logging.h"
#include "media_sample.h"


namespace AvsFilter {

FrameHandler::FrameHandler(CAviSynthFilter &filter)
    : _filter(filter)
    , _maxRequestedFrameNb(1)
    , _nextSourceFrameNb(0)
    , _processInputFrameNb(0)
    , _nextOutputFrameNb(0)
    , _nextOutputFrameStartTime(0)
    , _deliveryFrameNb(0)
    , _inputFlushBarrier(INPUT_SAMPLE_WORKER_THREAD_COUNT)
    , _outputFlushBarrier(OUTPUT_SAMPLE_WORKER_THREAD_COUNT)
    , _stopWorkerThreads(false)
    , _isFlushing(false) {
}

auto FrameHandler::AddInputSample(IMediaSample *inSample) -> void {
    std::unique_lock<std::mutex> srcLock(_sourceFramesMutex);

    while (!_isFlushing && !_stopWorkerThreads && _nextSourceFrameNb > _maxRequestedFrameNb) {
        _addInputSampleCv.wait(srcLock);
    }

    if (_isFlushing || _stopWorkerThreads) {
        return;
    }

    inSample->AddRef();
    _sourceFrames.emplace(_nextSourceFrameNb, SourceFrameInfo { _nextSourceFrameNb, inSample });

    Log("Add input sample %6i", _nextSourceFrameNb);

    _nextSourceFrameNb += 1;
    _newSourceFrameCv.notify_one();
}

auto FrameHandler::GetSourceFrame(int frameNb) -> PVideoFrame {
    std::unique_lock<std::mutex> srcLock(_sourceFramesMutex);

    _maxRequestedFrameNb = max(frameNb, _maxRequestedFrameNb);
    _addInputSampleCv.notify_one();

    std::map<int, SourceFrameInfo>::const_iterator iter;
    while (!_isFlushing && ((iter = _sourceFrames.find(frameNb)) == _sourceFrames.cend() || iter->second.avsFrame == nullptr)) {
        _sourceFrameAvailCv.wait(srcLock);
    }

    if (_isFlushing) {
        Log("Drain for frame: %6i", frameNb);
        return _sourceFrames.crbegin()->second.avsFrame;
    }

    Log("Get source frame: frameNb %6i Input queue size %2u Front %6i Back %6i",
        frameNb, _sourceFrames.size(), _sourceFrames.cbegin()->first, _sourceFrames.crbegin()->first);

    return iter->second.avsFrame;
}

auto FrameHandler::BeginFlush() -> void {
    _isFlushing = true;

    _addInputSampleCv.notify_all();
    _newSourceFrameCv.notify_all();
    _sourceFrameAvailCv.notify_all();
    _outputFramesCv.notify_all();
    _deliveryCv.notify_all();
}

auto FrameHandler::EndFlush() -> void {
    if (_stopWorkerThreads) {
        for (std::thread &t : _inputWorkerThreads) {
            t.join();
        }
        for (std::thread &t : _outputWorkerThreads) {
            t.join();
        }
    } else {
        _inputFlushBarrier.Wait();
        _outputFlushBarrier.Wait();
    }

    {
        std::unique_lock<std::mutex> srcLock(_sourceFramesMutex);

        for (auto &info: _sourceFrames) {
            if (info.second.sample != nullptr) {
                info.second.sample->Release();
            }
        }

        _sourceFrames.clear();
    }
    {
        std::unique_lock<std::mutex> outLock(_outputFramesMutex);

        _outputFrames.clear();
    }

    _maxRequestedFrameNb = 1;
    _nextSourceFrameNb = 0;
    _processInputFrameNb = 0;
    _nextOutputFrameNb = 0;
    _nextOutputFrameStartTime = 0;
    _deliveryFrameNb = 0;
    _isFlushing = false;

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

auto FrameHandler::StartWorkerThreads() -> void {
    ASSERT(_inputWorkerThreads.empty());
    ASSERT(_outputWorkerThreads.empty());

    _stopWorkerThreads = false;

    for (int i = 0; i < INPUT_SAMPLE_WORKER_THREAD_COUNT; ++i) {
        _inputWorkerThreads.emplace_back(&FrameHandler::ProcessInputSamples, this);
    }

    for (int i = 0; i < OUTPUT_SAMPLE_WORKER_THREAD_COUNT; ++i) {
        _outputWorkerThreads.emplace_back(&FrameHandler::ProcessOutputSamples, this);
    }
}

auto FrameHandler::StopWorkerThreads() -> void {
    _stopWorkerThreads = true;

    _addInputSampleCv.notify_all();
}

auto FrameHandler::ProcessInputSamples() -> void {
    Log("Start input sample worker thread %6i", std::this_thread::get_id());

    while (true) {
        if (_stopWorkerThreads) {
            break;
        } else if (_isFlushing) {
            _inputFlushBarrier.Arrive();
        }

        std::unique_lock<std::mutex> srcLock(_sourceFramesMutex);

        std::map<int, SourceFrameInfo>::iterator iter;
        while (!_isFlushing && (iter = _sourceFrames.find(_processInputFrameNb)) == _sourceFrames.cend()) {
            _newSourceFrameCv.wait(srcLock);
        }

        if (_isFlushing) {
            continue;
        }

        SourceFrameInfo &currSrcFrameInfo = iter->second;

        REFERENCE_TIME inSampleStopTime = 0;
        const HRESULT hr = currSrcFrameInfo.sample->GetTime(&currSrcFrameInfo.startTime, &inSampleStopTime);
        ASSERT(hr != VFW_E_SAMPLE_TIME_NOT_SET);

        _filter.RefreshInputFrameRates(currSrcFrameInfo.frameNb, currSrcFrameInfo.startTime);

        BYTE *sampleBuffer;
        currSrcFrameInfo.sample->GetPointer(&sampleBuffer);
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

        Log("Processed source frame: next %6i process %6i startTime %10lli, nextOutputFrameStartTime %10lli",
            _nextSourceFrameNb, _processInputFrameNb, currSrcFrameInfo.startTime, _nextOutputFrameStartTime);

        if ((iter = _sourceFrames.find(_processInputFrameNb - 1)) != _sourceFrames.cend()) {
            SourceFrameInfo &preSrcFrameInfo = iter->second;
            const REFERENCE_TIME prevSrcFrameTime = static_cast<REFERENCE_TIME>((currSrcFrameInfo.startTime - preSrcFrameInfo.startTime) * _filter._frameTimeScaling);

            if (_nextOutputFrameStartTime < preSrcFrameInfo.startTime) {
                _nextOutputFrameStartTime = preSrcFrameInfo.startTime;
            }

            const std::unique_lock<std::mutex> outLock(_outputFramesMutex);

            while (currSrcFrameInfo.startTime - _nextOutputFrameStartTime > 0) {
                const REFERENCE_TIME outStartTime = _nextOutputFrameStartTime;
                const REFERENCE_TIME outStopTime = outStartTime + prevSrcFrameTime;
                _nextOutputFrameStartTime = outStopTime;

                _outputFrames.emplace_back(OutputFrameInfo { _nextOutputFrameNb, outStartTime, outStopTime, &preSrcFrameInfo });
                _nextOutputFrameNb += 1;
                preSrcFrameInfo.refCount += 1;
            }

            _outputFramesCv.notify_one();
        }

        _sourceFrameAvailCv.notify_one();
        _processInputFrameNb += 1;
    }

    Log("Stop input sample worker thread %6i", std::this_thread::get_id());
}

auto FrameHandler::ProcessOutputSamples() -> void {
    Log("Start output sample worker thread %6i", std::this_thread::get_id());

    while (true) {
        if (_stopWorkerThreads) {
            break;
        } else if (_isFlushing) {
            _outputFlushBarrier.Arrive();
        }

        OutputFrameInfo outFrameInfo;
        {
            std::unique_lock<std::mutex> outLock(_outputFramesMutex);

            while (!_isFlushing && _outputFrames.empty()) {
                _outputFramesCv.wait(outLock);
            }

            if (_isFlushing) {
                continue;
            }

            outFrameInfo = _outputFrames.front();
            _outputFrames.pop_front();

            Log("Got OutFrameInfo %6i for %6i",
                outFrameInfo.frameNb, outFrameInfo.srcFrameInfo->frameNb);
        }

        _filter.RefreshOutputFrameRates(outFrameInfo.frameNb, outFrameInfo.startTime);

        const int srcFrameNb = outFrameInfo.srcFrameInfo->frameNb;

        Log("Start processing output frame: frameNb %6i at %10lli ~ %10lli frameTime %10lli from source frame %6i",
            outFrameInfo.frameNb, outFrameInfo.startTime, outFrameInfo.stopTime, outFrameInfo.stopTime - outFrameInfo.startTime, srcFrameNb);

        const PVideoFrame scriptFrame = _filter._avsScriptClip->GetFrame(outFrameInfo.frameNb, _filter._avsEnv);

        IMediaSample *outSample = nullptr;
        if (FAILED(_filter.InitializeOutputSample(nullptr, &outSample))) {
            break;
        }

        if (_filter._confirmNewOutputFormat) {
            outSample->SetMediaType(&_filter.m_pOutput->CurrentMediaType());
            _filter._confirmNewOutputFormat = false;
        }

        outSample->SetTime(&outFrameInfo.startTime, &outFrameInfo.stopTime);

        BYTE *outBuffer;
        outSample->GetPointer(&outBuffer);
        Format::WriteSample(_filter._outputFormat, scriptFrame, outBuffer, _filter._avsEnv);

        IMediaSideData *outSampleSideData;
        if (SUCCEEDED(outSample->QueryInterface(&outSampleSideData))) {
            outFrameInfo.srcFrameInfo->hdrSideData.Write(outSampleSideData);
            outSampleSideData->Release();
        }

        {
            std::unique_lock<std::mutex> delLock(_deliveryQueueMutex);

            while (!_isFlushing && outFrameInfo.frameNb != _deliveryFrameNb) {
                _deliveryCv.wait(delLock);
            }

            if (_isFlushing) {
                continue;
            }

            _filter.m_pOutput->Deliver(outSample);
            outSample->Release();

            Log("Delivered frame %6i", outFrameInfo.frameNb);

            _deliveryFrameNb += 1;
            _deliveryCv.notify_all();
        }

        GarbageCollect(srcFrameNb);
    }

    Log("Stop output sample worker thread %6i", std::this_thread::get_id());
}

auto FrameHandler::GarbageCollect(int srcFrameNb) -> void {
    const std::unique_lock<std::mutex> srcLock(_sourceFramesMutex);

    auto srcFrameIter = _sourceFrames.find(srcFrameNb);
    ASSERT(srcFrameIter != _sourceFrames.end());

    Log("GarbageCollect %6i pre-refcount %4i", srcFrameNb, srcFrameIter->second.refCount);

    if (srcFrameIter->second.refCount <= 1) {
        _sourceFrames.erase(srcFrameIter);
    } else {
        srcFrameIter->second.refCount -= 1;
    }
}

}