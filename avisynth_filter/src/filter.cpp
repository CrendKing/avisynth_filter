#include "pch.h"
#include "filter.h"
#include "media_sample.h"
#include "prop_settings.h"
#include "constants.h"
#include "source_clip.h"
#include "remote_control.h"
#include "logging.h"

#define CheckHr(expr) { hr = (expr); if (FAILED(hr)) { return hr; } }

auto ReplaceSubstring(std::string &str, const char *target, const char *rep) -> void {
    const size_t repLen = strlen(rep);
    size_t index = 0;

    while (true) {
        index = str.find(target, index);
        if (index == std::string::npos) {
            break;
        }

        str.replace(index, repLen, rep);
        index += repLen;
    }
}

auto ConvertWideToUtf8(const std::wstring& wstr) -> std::string {
    const int count = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.length()), nullptr, 0, nullptr, nullptr);
    std::string str(count, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], count, nullptr, nullptr);
    return str;
}

auto ConvertUtf8ToWide(const std::string& str) -> std::wstring {
    const int count = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), nullptr, 0);
    std::wstring wstr(count, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), &wstr[0], count);
    return wstr;
}

auto __cdecl Create_AvsFilterSource(AVSValue args, void *user_data, IScriptEnvironment *env) -> AVSValue {
    return static_cast<SourceClip *>(user_data);
}

auto __cdecl Create_AvsFilterDisconnect(AVSValue args, void *user_data, IScriptEnvironment *env) -> AVSValue {
    // the void type is internal in AviSynth and cannot be instantiated by user script, ideal for disconnect heuristic
    return AVSValue();
}

CAviSynthFilterInputPin::CAviSynthFilterInputPin(__in_opt LPCTSTR pObjectName,
                                                 __inout CTransformFilter *pTransformFilter,
                                                 __inout HRESULT *phr,
                                                 __in_opt LPCWSTR pName)
    : CTransformInputPin(pObjectName, pTransformFilter, phr, pName) {
}

// overridden to return out custom CAviSynthFilterAllocator instead of CMemAllocator,
// which allocates media sample with IMedaSideData attached
auto STDMETHODCALLTYPE CAviSynthFilterInputPin::GetAllocator(IMemAllocator** ppAllocator)->HRESULT {
    CheckPointer(ppAllocator, E_POINTER);
    ValidateReadWritePtr(ppAllocator, sizeof(IMemAllocator*));
    CAutoLock cObjectLock(m_pLock);

    Log("CAviSynthFilterInputPin::GetAllocator");

    if (m_pAllocator)
    {
        *ppAllocator = m_pAllocator;
        m_pAllocator->AddRef();
        return S_OK;
    }

    HRESULT hr = S_OK;
    CAviSynthFilterAllocator*pAlloc = new CAviSynthFilterAllocator(&hr);
    if (FAILED(hr)) {
        delete pAlloc;
        return hr;
    }
    return pAlloc->QueryInterface(IID_IMemAllocator, (void**)ppAllocator);
}

auto STDMETHODCALLTYPE CAviSynthFilterInputPin::ReceiveConnection(IPin *pConnector, const AM_MEDIA_TYPE *pmt) -> HRESULT {
    HRESULT hr;

    const CAutoLock lock(m_pLock);

    if (m_Connected) {
        const CMediaType *cmt = static_cast<const CMediaType *>(pmt);

        if (CheckMediaType(cmt) != S_OK) {
            return VFW_E_TYPE_NOT_ACCEPTED;
        }

        ALLOCATOR_PROPERTIES props, actual;

        IMemAllocator *allocator;
        CheckHr(GetAllocator(&allocator));
        CheckHr(allocator->Decommit());
        CheckHr(allocator->GetProperties(&props));

        const BITMAPINFOHEADER *bih = Format::GetBitmapInfo(*pmt);
        props.cbBuffer = bih->biSizeImage;

        CheckHr(allocator->SetProperties(&props, &actual));
        CheckHr(allocator->Commit());

        if (props.cbBuffer != actual.cbBuffer) {
            return E_FAIL;
        }

        CheckHr(SetMediaType(cmt));
        CheckHr(reinterpret_cast<CAviSynthFilter *>(m_pFilter)->HandleInputFormatChange(pmt));

        return S_OK;
    }

    return __super::ReceiveConnection(pConnector, pmt);
}

CAviSynthFilter::CAviSynthFilter(LPUNKNOWN pUnk, HRESULT *phr)
    : CVideoTransformFilter(NAME(FILTER_NAME_FULL), pUnk, CLSID_AviSynthFilter)
    , _avsEnv(nullptr)
    , _avsScriptClip(nullptr)
    , _remoteControl(nullptr)
    , _reloadAvsFileFlag(false)
    , _maxBufferUnderflowAhead(0) {
    LoadSettings();
    Log("CAviSynthFilter::CAviSynthFilter()");
}

CAviSynthFilter::~CAviSynthFilter() {
    if (_remoteControl) {
        delete _remoteControl;
        _remoteControl = nullptr;
    }

    DeletePinTypes();
    DeleteAviSynth();
}

auto STDMETHODCALLTYPE CAviSynthFilter::NonDelegatingQueryInterface(REFIID riid, void **ppv) -> HRESULT {
    CheckPointer(ppv, E_POINTER);

    if (riid == IID_ISpecifyPropertyPages) {
        return GetInterface(static_cast<ISpecifyPropertyPages *>(this), ppv);
    }
    if (riid == IID_IAvsFilterSettings) {
        return GetInterface(static_cast<IAvsFilterSettings *>(this), ppv);
    }
    if (riid == IID_IAvsFilterStatus) {
        return GetInterface(static_cast<IAvsFilterStatus *>(this), ppv);
    }

    return __super::NonDelegatingQueryInterface(riid, ppv);
}

auto CAviSynthFilter::GetPin(int n) -> CBasePin * {
    HRESULT hr = S_OK;

    if (n == 0) {
        if (m_pInput == nullptr) {
            m_pInput = new CAviSynthFilterInputPin(NAME("AviSynthFilter input pin"), this, &hr, L"Avs In");
        }
        return m_pInput;
    }

    if (n == 1) {
        if (m_pOutput == nullptr) {
            m_pOutput = new CTransformOutputPin(NAME("AviSynthFilter output pin"), this, &hr, L"Avs Out");
        }
        return m_pOutput;
    }

    return nullptr;
}

auto CAviSynthFilter::CheckConnect(PIN_DIRECTION direction, IPin *pPin) -> HRESULT {
    HRESULT ret = S_OK;
    HRESULT hr;

    CreateAviSynth();

    if (direction == PINDIR_INPUT) {
        IEnumMediaTypes *enumTypes;
        CheckHr(pPin->EnumMediaTypes(&enumTypes));

        AM_MEDIA_TYPE *nextType;
        while (true) {
            hr = enumTypes->Next(1, &nextType, nullptr);
            if (hr == S_OK) {
                const int inputDefinition = GetInputDefinition(nextType);

                // for each group of formats with the same avs type, only add the first one that upstream supports.
                // this one will be the preferred media type for potential pin reconnection
                if (inputDefinition != INVALID_DEFINITION && IsInputUniqueByAvsType(inputDefinition)) {
                    // invoke AviSynth script with each supported input definition, and observe the output avs type
                    if (!ReloadAviSynth(*nextType, false)) {
                        Log("Disconnect due to AvsFilterDisconnect()");

                        DeleteMediaType(nextType);
                        ret = VFW_E_NO_TYPES;
                        break;
                    }

                    _acceptableInputTypes.emplace(inputDefinition, nextType);
                    Log("Add acceptable input definition: %2i", inputDefinition);

                    // all media types that share the same avs type are acceptable for output pin connection
                    for (int outputDefinition : Format::LookupAvsType(_avsScriptVideoInfo.pixel_type)) {
                        if (_acceptableOuputTypes.find(outputDefinition) == _acceptableOuputTypes.end()) {
                            AM_MEDIA_TYPE *outputType = GenerateMediaType(outputDefinition, nextType);
                            _acceptableOuputTypes.emplace(outputDefinition, outputType);
                            Log("Add acceptable output definition: %2i", outputDefinition);

                            _compatibleDefinitions.emplace_back(DefinitionPair { inputDefinition, outputDefinition });
                            Log("Add compatible definitions: input %2i output %2i", inputDefinition, outputDefinition);
                        }
                    }
                } else {
                    DeleteMediaType(nextType);
                }
            } else if (hr == VFW_E_ENUM_OUT_OF_SYNC) {
                CheckHr(enumTypes->Reset());
                DeletePinTypes();
            } else {
                break;
            }
        }

        enumTypes->Release();
    }

    return ret;
}

auto CAviSynthFilter::CheckInputType(const CMediaType *mtIn) -> HRESULT {
    const int definition = GetInputDefinition(mtIn);

    if (_acceptableInputTypes.find(definition) == _acceptableInputTypes.end()) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    Log("Accept input definition: %2i", definition);

    return S_OK;
}

auto CAviSynthFilter::GetMediaType(int iPosition, CMediaType *pMediaType) -> HRESULT {
    if (iPosition < 0) {
        return E_INVALIDARG;
    }

    if (m_pInput->IsConnected() == FALSE) {
        return E_UNEXPECTED;
    }

    if (iPosition >= static_cast<int>(_compatibleDefinitions.size())) {
        return VFW_S_NO_MORE_ITEMS;
    }

    const int definition = _compatibleDefinitions[iPosition].output;
    *pMediaType = *_acceptableOuputTypes[definition];

    Log("Offer output definition: %2i", definition);

    return S_OK;
}

auto CAviSynthFilter::CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut) -> HRESULT {
    const int outputDefinition = MediaTypeToDefinition(mtOut);
    if (outputDefinition == INVALID_DEFINITION) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    if (_acceptableOuputTypes.find(outputDefinition) == _acceptableOuputTypes.end()) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    Log("Accept transform: out %2i", outputDefinition);

    return S_OK;
}

auto CAviSynthFilter::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProperties) -> HRESULT {
    HRESULT hr;

    // we need at least 2 buffers so that we can hold pOut while preparing another extra sample
    pProperties->cBuffers = max(2, pProperties->cBuffers);

    BITMAPINFOHEADER *bih = Format::GetBitmapInfo(m_pOutput->CurrentMediaType());
    pProperties->cbBuffer = max(bih->biSizeImage, static_cast<unsigned long>(pProperties->cbBuffer));

    ALLOCATOR_PROPERTIES actual;
    CheckHr(pAlloc->SetProperties(pProperties, &actual));

    if (pProperties->cBuffers > actual.cBuffers || pProperties->cbBuffer > actual.cbBuffer) {
        return E_FAIL;
    }

    return S_OK;
}

auto CAviSynthFilter::CompleteConnect(PIN_DIRECTION direction, IPin *pReceivePin) -> HRESULT {
    /*
     * The media type negotiation logic
     *
     * Suppose the upstream's output pin supports N1 media types. The AviSynth script could convert those N1 input types to N2 output types.
     * The downstream's input pin supports N3 media types. Our Format class supports N4 media types.
     *
     * We need to find a pair of input/output types that, by feeding the input type to the avs script, the script's equivalent output media type
     * equals to the filter's output pin media type.
     *
     * The filter graph connects upstream's output pin to our input pin first. We We enumerate the input pin for its N1 media types and calculate
     * acceptable output types by pass the media subtypes to the avs script.
     *
     * During type checking, we accept every Format class compatible media type (minus those excluded from settings), so that both upstream and
     * downstream can choose their preferred connection type.
     *
     * During the output side of CompleteConnect(), both pins have connected. We check if the postcondition holds. If yes, the pin connection completes.
     * If not, we keep the connection to the downstream, reverse lookup the preferred input media type and reconnect input pin.
     *
     * Because the reconnect media type is selected from upstream's enumerated media type, the connection should always succeed at the second time.
     */

    HRESULT hr;

    if (!m_pInput->IsConnected()) {
        return E_UNEXPECTED;
    }

    const int inputDefiniton = Format::LookupMediaSubtype(m_pInput->CurrentMediaType().subtype);
    ASSERT(inputDefiniton != INVALID_DEFINITION);

    if (!m_pOutput->IsConnected()) {
        Log("Connected input pin with definition: %2i", inputDefiniton);
    } else {
        const int outputDefinition = MediaTypeToDefinition(&m_pOutput->CurrentMediaType());
        ASSERT(outputDefinition != INVALID_DEFINITION);

        const int compatibleInput = FindCompatibleInputByOutput(outputDefinition);
        if (inputDefiniton != compatibleInput) {
            Log("Reconnect with types: old in %2i new in %2i out %2i", inputDefiniton, compatibleInput, outputDefinition);
            CheckHr(ReconnectPin(m_pInput, _acceptableInputTypes[compatibleInput]));
        } else {
            Log("Connected with types: in %2i out %2i", inputDefiniton, outputDefinition);
            _sourcePath = RetrieveSourcePath(m_pGraph);
        }
    }

    return __super::CompleteConnect(direction, pReceivePin);
}

auto CAviSynthFilter::Receive(IMediaSample *pSample) -> HRESULT {
    HRESULT hr;
    AM_MEDIA_TYPE *pmtOut, *pmt;
    IMediaSample *pOutSample;
    bool reloadedAvsForFormatChange = false;
    bool confirmNewOutputFormat = false;

    pSample->GetMediaType(&pmt);
    bool mediaTypeChanged = pmt != nullptr && pmt->pbFormat != nullptr;
    if (mediaTypeChanged || _reloadAvsFileFlag) {
        StopStreaming();

        if(_reloadAvsFileFlag)
            Reset(false);

        if(mediaTypeChanged)
            m_pInput->SetMediaType(reinterpret_cast<CMediaType *>(pmt));
        else
            pmt = reinterpret_cast<AM_MEDIA_TYPE*>(&m_pInput->CurrentMediaType());
        
        hr = HandleInputFormatChange(pmt, _reloadAvsFileFlag);
        _reloadAvsFileFlag = false;

        if (FAILED(hr)) {
            return AbortPlayback(hr);
        }
        reloadedAvsForFormatChange = hr == S_OK;
        
        if(mediaTypeChanged)
            DeleteMediaType(pmt);

        hr = StartStreaming();
        if (FAILED(hr)) {
            return AbortPlayback(hr);
        }
    }

    CheckHr(m_pOutput->GetDeliveryBuffer(&pOutSample, nullptr, nullptr, 0));

    pOutSample->GetMediaType(&pmtOut);
    pOutSample->Release();
    if (pmtOut != nullptr && pmtOut->pbFormat != nullptr) {
        StopStreaming();
        m_pOutput->SetMediaType(reinterpret_cast<CMediaType *>(pmtOut));

        hr = HandleOutputFormatChange(pmtOut);
        if (FAILED(hr)) {
            return AbortPlayback(hr);
        }
        confirmNewOutputFormat = hr == S_OK;

        DeleteMediaType(pmtOut);
        hr = StartStreaming();

        if (SUCCEEDED(hr)) {
            m_nWaitForKey = 30;
        } else {
            return AbortPlayback(hr);
        }
    }

    if (pSample->IsDiscontinuity() == S_OK) {
        m_nWaitForKey = 30;
    }

    if (SUCCEEDED(hr)) {
        hr = TransformAndDeliver(pSample, reloadedAvsForFormatChange, confirmNewOutputFormat);

        if (m_nWaitForKey)
            m_nWaitForKey--;
        if (m_nWaitForKey && pSample->IsSyncPoint() == S_OK)
            m_nWaitForKey = 0;

        if (m_nWaitForKey) {
            hr = S_FALSE;
        }
    }

    if (S_FALSE == hr) {
        if (!m_bQualityChanged) {
            m_bQualityChanged = TRUE;
            NotifyEvent(EC_QUALITY_CHANGE, 0, 0);
        }
        return NOERROR;
    }

    return hr;
}

static auto GetAverageFps(std::list<REFERENCE_TIME>& samples, REFERENCE_TIME sample) -> double {
    if (samples.size() > 0 && samples.front() > sample)
        samples.clear();
    else {
        samples.push_back(sample);
        if (samples.size() > 10)
            samples.pop_front();

        if (samples.size() >= 2)
            return std::round(static_cast<double>((samples.size() - 1) * UNITS) / (samples.back() - samples.front()) * 1000.0) / 1000.0;
    }
    return 0.0;
}

auto CAviSynthFilter::TransformAndDeliver(IMediaSample *pIn, bool reloadedAvsForFormatChange, bool confirmNewOutputFormat) -> HRESULT {
    /*
     * Unlike normal Transform() where one input sample is transformed to one output sample, our filter may not have that 1:1 relationship.
     * This function supports the following situations:
     * 1) Flexible input:output mapping, by using a input frame ordered map.
     * 2) Aggressive caching from AviSynth (e.g. prefetcher), by using buffering.
     * 3) Variable frame rate, by frame counting and respecting individual frame time.
     * 4) Samples without time, by referring to the stream time and average frame time.
     *
     * TODO: further decouple and input processing and delivery by moving the delivery logic into a worker thread.
     * That way we can put the thread into waiting until enough input samples have been gathered.
     */

    HRESULT hr;

    if (_reloadAvsEnvFlag) {
        // small optimization where avs may already be loaded during format change handling thus skipping here
        if (!reloadedAvsForFormatChange) {
            ReloadAviSynth(m_pInput->CurrentMediaType(), false);
        }
        _inputSampleNb = 0;
        _deliveryFrameNb = 0;
        _reloadAvsEnvFlag = false;        
    }

    /*
     * We process each input sample by creating an avs source frame from it and put its times in an ordered map.
     * If we detect cache miss in frame handler, we will accumulate enough frames before delivering new output.
     */

    CRefTime streamTime;
    CheckHr(StreamTime(streamTime));
    streamTime = min(streamTime, m_tStart);

    REFERENCE_TIME inStartTime, inStopTime;
    hr = pIn->GetTime(&inStartTime, &inStopTime);
   
    if (hr == VFW_E_SAMPLE_TIME_NOT_SET) {
        /*
        Even when the upstream does not set sample time, we fill up the time in reference to the stream time.
        1) Some downstream (e.g. BlueskyFRC) needs sample time to function.
        2) If the start time is too close to the stream time, the renderer may drop the frame as being late.
           We add offset by time-per-frame until the lateness is gone
        */
        if (m_itrLate > 0) {
            _sampleTimeOffset += 1;
        }
        inStartTime = streamTime + _sampleTimeOffset * _timePerFrame;
        inStopTime = inStartTime + _timePerFrame;
    } else if (hr == VFW_S_NO_STOP_TIME) {
        inStopTime = inStartTime + _timePerFrame;
    }

    Log("late: %10i streamTime: %10lli sampleTime: %10lli ~ %10lli frameTime: %lli sourceFrameNb: %4i, maxBufferAhead: %2i",
        m_itrLate, static_cast<REFERENCE_TIME>(streamTime), inStartTime, inStopTime, inStopTime - inStartTime, _inputSampleNb, _maxBufferUnderflowAhead);

    BYTE *inputBuffer;
    CheckHr(pIn->GetPointer(&inputBuffer));
    _frameHandler.CreateFrame(_inputFormat, _inputSampleNb, inputBuffer, _avsEnv);
    _sampleTimes[_inputSampleNb] = { inStartTime, inStopTime };

    if (_bufferUnderflowAhead > 0) {
        _bufferUnderflowAhead -= 1;
    } else {
        while (!_sampleTimes.empty()) {
            const SampleTimeInfo &info = _sampleTimes.begin()->second;

            if (_deliveryFrameStartTime == 0) {
                _deliveryFrameStartTime = info.startTime;
            }

            while (_deliveryFrameStartTime < info.stopTime) {
                const PVideoFrame clipFrame = _avsScriptClip->GetFrame(_deliveryFrameNb, _avsEnv);
                _bufferUnderflowAhead = _frameHandler.GetMaxAccessedFrameNb() - _inputSampleNb;
                _bufferUnderflowBack = _inputSampleNb - _frameHandler.GetMinAccessedFrameNb();
                _maxBufferUnderflowAhead = max(_bufferUnderflowAhead, _maxBufferUnderflowAhead);
                if (_bufferUnderflowAhead >= 0) {
                    goto endOfDelivery;
                }

                IMediaSample *outSample = nullptr;
                CheckHr(InitializeOutputSample(nullptr, &outSample));

                if (confirmNewOutputFormat) {
                    CheckHr(outSample->SetMediaType(&m_pOutput->CurrentMediaType()));
                    confirmNewOutputFormat = false;
                }

                // even if missing sample times from upstream, we always set times for output samples in case downstream needs them
                REFERENCE_TIME outStartTime = _deliveryFrameStartTime;
                REFERENCE_TIME outStopTime = outStartTime + static_cast<REFERENCE_TIME>((info.stopTime - info.startTime) * _frameTimeScaling);
                _deliveryFrameStartTime = outStopTime;
                CheckHr(outSample->SetTime(&outStartTime, &outStopTime));

                _outputFrameRate = GetAverageFps(_samplesOut, outStartTime);

                BYTE *outBuffer;
                CheckHr(outSample->GetPointer(&outBuffer));

                FrameHandler::WriteSample(_outputFormat, clipFrame, outBuffer, _avsEnv);
                CheckHr(m_pOutput->Deliver(outSample));

                outSample->Release();
                outSample = nullptr;

                Log("Deliver frameNb: %6i at %10lli ~ %10lli", _deliveryFrameNb, outStartTime, outStopTime);

                _deliveryFrameNb += 1;
            }

            _sampleTimes.erase(_sampleTimes.begin());
        }

endOfDelivery:
        _frameHandler.GarbageCollect(_inputSampleNb - _bufferUnderflowBack, _inputSampleNb + _bufferUnderflowAhead);
    }

    _inputSampleNb += 1;
    _inputFrameRate = GetAverageFps(_samplesIn, inStartTime);

    return S_OK;
}

auto CAviSynthFilter::EndFlush() -> HRESULT {
    Reset();
    return __super::EndFlush();
}

auto STDMETHODCALLTYPE CAviSynthFilter::Pause() -> HRESULT {
    if (m_State == State_Stopped) {
        _inputFormat = Format::GetVideoFormat(m_pInput->CurrentMediaType());
        _outputFormat = Format::GetVideoFormat(m_pOutput->CurrentMediaType());
        Reset();
    }
    return __super::Pause();
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetPages(CAUUID *pPages) -> HRESULT {
    CheckPointer(pPages, E_POINTER);

    pPages->pElems = static_cast<GUID *>(CoTaskMemAlloc(2 * sizeof(GUID)));
    if (pPages->pElems == nullptr) {
        return E_OUTOFMEMORY;
    }

    pPages->pElems[0] = CLSID_AvsPropSettings;
    pPages->pElems[1] = CLSID_AvsPropStatus;

    if (_avsEnv == nullptr) {
        pPages->cElems = 1;
    } else {
        pPages->cElems = 2;
    }

    return S_OK;
}

auto STDMETHODCALLTYPE CAviSynthFilter::SaveSettings() const -> void {
    _registry.WriteString(REGISTRY_VALUE_NAME_AVS_FILE, _avsFile);
    _registry.WriteNumber(REGISTRY_VALUE_NAME_FORMATS, _inputFormatBits);
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetAvsFile() const -> const std::wstring & {
    return _avsFile;
}

auto STDMETHODCALLTYPE CAviSynthFilter::SetAvsFile(const std::wstring &avsFile) -> void {
    _avsFile = avsFile;
}

auto STDMETHODCALLTYPE CAviSynthFilter::ReloadAvsFile() -> void {
    _reloadAvsFileFlag = true;
    _maxBufferUnderflowAhead = 0;
}

auto STDMETHODCALLTYPE CAviSynthFilter::IsRemoteControlled() -> bool {
    return _remoteControl != nullptr;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetInputFormats() const -> DWORD {
    return _inputFormatBits;
}

auto STDMETHODCALLTYPE CAviSynthFilter::SetInputFormats(DWORD formatBits) -> void {
    _inputFormatBits = formatBits;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetBufferSize() -> int {
    return _frameHandler.GetBufferSize();
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetBufferUnderflowAhead() const -> int {
    return _bufferUnderflowAhead;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetBufferUnderflowBack() const -> int {
    return _bufferUnderflowBack;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetSampleTimeOffset() const -> int {
    return _sampleTimeOffset;
}

auto STDMETHODCALLTYPE  CAviSynthFilter::GetFrameNumbers() const -> std::pair<int, int> {
    return { _inputSampleNb, _deliveryFrameNb };
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetInputFrameRate() const -> double {
    return _inputFrameRate;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetOutputFrameRate() const -> double {
    return _outputFrameRate;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetSourcePath() const -> std::wstring {
    if (_sourcePath.empty()) {
        return L"N/A";
    }
    return _sourcePath;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetMediaInfo() const -> const Format::VideoFormat * {
    return &_inputFormat;
}

/**
 * Check if the media type has valid VideoInfo * definition block.
 */
auto CAviSynthFilter::MediaTypeToDefinition(const AM_MEDIA_TYPE *mediaType) -> int {
    if (mediaType->majortype != MEDIATYPE_Video) {
        return INVALID_DEFINITION;
    }

    if (FAILED(CheckVideoInfoType(mediaType)) && FAILED(CheckVideoInfo2Type(mediaType))) {
        return INVALID_DEFINITION;
    }

    return Format::LookupMediaSubtype(mediaType->subtype);
}

auto CAviSynthFilter::RetrieveSourcePath(IFilterGraph *graph) -> std::wstring {
    std::wstring ret;

    IEnumFilters *filters;
    if (FAILED(graph->EnumFilters(&filters))) {
        return ret;
    }

    IBaseFilter *filter;
    ULONG n;
    while (true) {
        const HRESULT hr = filters->Next(1, &filter, &n);
        if (hr == S_OK) {
            FILTER_INFO	info;
            if (FAILED(filter->QueryFilterInfo(&info))) {
                continue;
            }

            QueryFilterInfoReleaseGraph(info);

            Log("Filter: '%ls'", info.achName);

            IFileSourceFilter *source;
            if (!FAILED(filter->QueryInterface(&source))) {
                LPOLESTR filename;
                if (SUCCEEDED(source->GetCurFile(&filename, nullptr))) {
                    Log("Source path: '%ls'", filename);
                    ret = std::wstring(filename);
                    break;
                }
                source->Release();
            }

            filter->Release();
        } else if (hr == VFW_E_ENUM_OUT_OF_SYNC) {
            filters->Reset();
        } else {
            break;
        }
    }
    filters->Release();

    return ret;
}

/**
 * Whenever the media type on the input pin is changed, we need to run the avs script against
 * the new type to generate corresponding output type. Then we need to update the downstream with it.
 *
 * returns S_OK if the avs script is reloaded due to format change.
 */
auto CAviSynthFilter::HandleInputFormatChange(const AM_MEDIA_TYPE *pmt, bool force) -> HRESULT {
    HRESULT hr;

    const Format::VideoFormat receivedInputFormat = Format::GetVideoFormat(*pmt);
    if ((_inputFormat != receivedInputFormat) || force) {
        Log("new input format:  definition %i, width %5i, height %5i, codec %s",
            receivedInputFormat.definition, receivedInputFormat.bmi.biWidth, receivedInputFormat.bmi.biHeight, receivedInputFormat.GetCodecName().c_str());
        _inputFormat = receivedInputFormat;

        ReloadAviSynth(*pmt, true);
        AM_MEDIA_TYPE *replaceOutputType = GenerateMediaType(Format::LookupAvsType(_avsScriptVideoInfo.pixel_type)[0], pmt);
        if (m_pOutput->GetConnected()->QueryAccept(replaceOutputType) != S_OK) {
            return VFW_E_TYPE_NOT_ACCEPTED;
        }
        CheckHr(m_pOutput->GetConnected()->ReceiveConnection(m_pOutput, replaceOutputType));
        DeleteMediaType(replaceOutputType);

        return S_OK;
    }

    return S_FALSE;
}

/**
 * returns S_OK if the next media sample should carry the media type on the output pin.
 */
auto CAviSynthFilter::HandleOutputFormatChange(const AM_MEDIA_TYPE *pmtOut) -> HRESULT {
    const Format::VideoFormat newOutputFormat = Format::GetVideoFormat(*pmtOut);
    if (_outputFormat != newOutputFormat) {
        Log("new output format: definition %i, width %5i, height %5i, codec %s",
            newOutputFormat.definition, newOutputFormat.bmi.biWidth, newOutputFormat.bmi.biHeight, newOutputFormat.GetCodecName().c_str());
        _outputFormat = newOutputFormat;

        return S_OK;
    }

    return S_FALSE;
}

auto CAviSynthFilter::Reset(bool lock) -> void {
    if (lock)
        m_csReceive.Lock();

    _frameHandler.FlushOnNextFrame();
    _sampleTimes.clear();
    _bufferUnderflowAhead = _maxBufferUnderflowAhead;
    _bufferUnderflowBack = 0;
    _sampleTimeOffset = 0;
    _deliveryFrameStartTime = 0;
    _inputFrameRate = 0.0;
    _outputFrameRate = 0.0;
    _samplesIn.clear();
    _samplesOut.clear();
    _reloadAvsEnvFlag = true;

    if (lock)
        m_csReceive.Unlock();
}

auto CAviSynthFilter::LoadSettings() -> void {
    if (_registry.ReadNumber(REGISTRY_VALUE_NAME_REMOTE, 0))
        _remoteControl = new RemoteControl(this, this);
    else
        SetAvsFile(_registry.ReadString(REGISTRY_VALUE_NAME_AVS_FILE));

    _inputFormatBits = _registry.ReadNumber(REGISTRY_VALUE_NAME_FORMATS, (1 << Format::DEFINITIONS.size()) - 1);
}

auto CAviSynthFilter::GetInputDefinition(const AM_MEDIA_TYPE *mediaType) const -> int {
    const int inputDefinition = MediaTypeToDefinition(mediaType);

    if (inputDefinition == INVALID_DEFINITION) {
        return INVALID_DEFINITION;
    }

    if ((_inputFormatBits & (1 << inputDefinition)) == 0) {
        Log("Reject input definition due to settings: %2i", inputDefinition);
        return INVALID_DEFINITION;
    }

    return inputDefinition;
}

/**
 * Create media type based on a template while changing its subtype. Also change fields in definition if necessary.
 *
 * For example, when the original subtype has 8-bit samples and new subtype has 16-bit,
 * all "size" and FourCC values will be adjusted.
 */
auto CAviSynthFilter::GenerateMediaType(int definition, const AM_MEDIA_TYPE *templateMediaType) const -> AM_MEDIA_TYPE * {
    const Format::Definition &def = Format::DEFINITIONS[definition];
    FOURCCMap fourCC(&def.mediaSubtype);

    AM_MEDIA_TYPE *newMediaType = CreateMediaType(templateMediaType);
    newMediaType->subtype = def.mediaSubtype;

    VIDEOINFOHEADER *newVih = reinterpret_cast<VIDEOINFOHEADER *>(newMediaType->pbFormat);
    BITMAPINFOHEADER *newBmi;

    if (SUCCEEDED(CheckVideoInfo2Type(newMediaType))) {
        VIDEOINFOHEADER2 *newVih2 = reinterpret_cast<VIDEOINFOHEADER2 *>(newMediaType->pbFormat);
        const int gcd = std::gcd(_avsScriptVideoInfo.width, _avsScriptVideoInfo.height);
        newVih2->dwPictAspectRatioX = _avsScriptVideoInfo.width / gcd;
        newVih2->dwPictAspectRatioY = _avsScriptVideoInfo.height / gcd;
        newBmi = &newVih2->bmiHeader;
    } else {
        newBmi = &newVih->bmiHeader;
    }

    newVih->rcSource = { 0, 0, _avsScriptVideoInfo.width, _avsScriptVideoInfo.height };
    newVih->rcTarget = newVih->rcSource;
    newVih->AvgTimePerFrame = llMulDiv(_avsScriptVideoInfo.fps_denominator, UNITS, _avsScriptVideoInfo.fps_numerator, 0);

    newBmi->biWidth = _avsScriptVideoInfo.width;
    newBmi->biHeight = _avsScriptVideoInfo.height;
    newBmi->biBitCount = def.bitCount;
    newBmi->biSizeImage = GetBitmapSize(newBmi);
    newMediaType->lSampleSize = newBmi->biSizeImage;

    if (IsEqualGUID(fourCC, def.mediaSubtype)) {
        // uncompressed formats (such as RGB32) have different GUIDs
        newBmi->biCompression = fourCC.GetFOURCC();
    } else {
        newBmi->biCompression = BI_RGB;
    }

    return newMediaType;
}

auto CAviSynthFilter::DeletePinTypes() -> void {
    for (const auto &value : _acceptableInputTypes) {
        DeleteMediaType(value.second);
    }
    _acceptableInputTypes.clear();

    for (const auto &value : _acceptableOuputTypes) {
        DeleteMediaType(value.second);
    }
    _acceptableOuputTypes.clear();

    _compatibleDefinitions.clear();
}

auto CAviSynthFilter::CreateAviSynth() -> void {
    if (_avsEnv == nullptr) {
        _avsEnv = CreateScriptEnvironment2();
        _avsEnv->AddFunction("AvsFilterSource", "", Create_AvsFilterSource, new SourceClip(_avsSourceVideoInfo, _frameHandler));
        _avsEnv->AddFunction("AvsFilterDisconnect", "", Create_AvsFilterDisconnect, nullptr);
        //TODO: remove me!
        _avsEnv->AddFunction("potplayer_source", "", Create_AvsFilterSource, new SourceClip(_avsSourceVideoInfo, _frameHandler));
    }
}

/**
 * Create new AviSynth script clip with specified media type.
 * If allowDisconnect == true, return false early if no avs script or AvsFilterDisconnect() is returned from script
 */
auto CAviSynthFilter::ReloadAviSynth(const AM_MEDIA_TYPE &mediaType, bool recreateEnv) -> bool {
    _avsSourceVideoInfo = Format::GetVideoFormat(mediaType).videoInfo;

    /*
     * When reloading AviSynth, there are two alternative approaches:
     *     Reload everything (the environment, the scripts, which also flushes avs frame cache).
     *     Only reload the scripts (which does not flush frame cache).
     * And for seeking, we could either reload or not reload.
     *
     * Recreating the AviSynth environment guarantees a clean start, free of picture artifacts or bugs,
     * at the cost of noticable lag.
     *
     * Usually we do not recreate to favor performance. There are cases where recreating is necessary:
     *
     * 1) Dynamic format change. This happens after playback has started, thus there will be cached frames in
     * the avs environment. After format change, reusing the cached frames may either cause artifacts or outright crash
     * (due to buffer size change).
     *
     * 2) Certain AviSynth filters and functions are not compatible, such as SVP's SVSmoothFps_NVOF().
     */
    
    if (recreateEnv) {
        DeleteAviSynth();
        CreateAviSynth();
    }

    AVSValue invokeResult;
    std::string errorScript;
    try {
        bool isImportSuccess = false;

        if (!_avsFile.empty()) {
            const std::string utf8File = ConvertWideToUtf8(_avsFile);
            AVSValue args[2] = { utf8File.c_str(), true };
            const char* const argNames[2] = { nullptr, "utf8" };
            if(PathFileExists(_avsFile.c_str()))                
                invokeResult = _avsEnv->Invoke("Import", AVSValue(args, 2), argNames);
            else
                invokeResult = _avsEnv->Invoke("Eval", AVSValue(args, 1), nullptr);
            isImportSuccess = invokeResult.Defined();
        }

        if (!isImportSuccess) {
            if (m_State == State_Stopped && !IsRemoteControlled()) {
                return false;
            }

            AVSValue evalArgs[] = { AVSValue("return AvsFilterSource()")
                                  , AVSValue(EVAL_FILENAME) };
            invokeResult = _avsEnv->Invoke("Eval", AVSValue(evalArgs, 2), nullptr);
        }

        if (!invokeResult.IsClip()) {
            errorScript = "Unrecognized return value from script. Invalid script.";
        }
    } catch (AvisynthError &err) {
        errorScript = err.msg;
        ReplaceSubstring(errorScript, "\"", "\'");
        ReplaceSubstring(errorScript, "\n", "\\n");
    }

    if (!errorScript.empty()) {
        errorScript.insert(0, "return AvsFilterSource().Subtitle(\"");
        errorScript.append("\", lsp=0, utf8=true)");
        AVSValue evalArgs[] = { AVSValue(errorScript.c_str())
                              , AVSValue(EVAL_FILENAME) };
        invokeResult = _avsEnv->Invoke("Eval", AVSValue(evalArgs, 2), nullptr);
    }

    _avsScriptClip = invokeResult.AsClip();
    _avsScriptVideoInfo = _avsScriptClip->GetVideoInfo();
    _frameTimeScaling = static_cast<double>(llMulDiv(_avsSourceVideoInfo.fps_numerator, _avsScriptVideoInfo.fps_denominator, _avsSourceVideoInfo.fps_denominator, 0)) / _avsScriptVideoInfo.fps_numerator;
    _timePerFrame = llMulDiv(_avsScriptVideoInfo.fps_denominator, UNITS, _avsScriptVideoInfo.fps_numerator, 0);

    return true;
}

/**
 * Delete the AviSynth environment and all its cache
 */
auto CAviSynthFilter::DeleteAviSynth() -> void {
    if (_avsScriptClip != nullptr) {
        _avsScriptClip = nullptr;
    }

    _frameHandler.Flush();

    if (_avsEnv != nullptr) {
        _avsEnv->DeleteScriptEnvironment();
        _avsEnv = nullptr;
    }
}

auto CAviSynthFilter::IsInputUniqueByAvsType(int inputDefinition) const -> bool {
    for (const auto &value : _acceptableInputTypes) {
        if (Format::DEFINITIONS[value.first].avsType == Format::DEFINITIONS[inputDefinition].avsType) {
            return false;
        }
    }
    return true;
}

auto CAviSynthFilter::FindCompatibleInputByOutput(int outputDefinition) const -> int {
    for (const DefinitionPair &pair : _compatibleDefinitions) {
        if (pair.output == outputDefinition) {
            return pair.input;
        }
    }
    return INVALID_DEFINITION;
}