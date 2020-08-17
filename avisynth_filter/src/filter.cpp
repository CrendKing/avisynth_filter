#include "pch.h"
#include "filter.h"
#include "prop_settings.h"
#include "constants.h"
#include "source_clip.h"
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

auto STDMETHODCALLTYPE CAviSynthFilterInputPin::ReceiveConnection(IPin *pConnector, const AM_MEDIA_TYPE *pmt) -> HRESULT {
    HRESULT hr;
    CAutoLock cObjectLock(m_pLock);

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
    , _upstreamPin(nullptr)
    , _stableBufferAhead(0)
    , _stableBufferBack(0) {
    LoadSettings();
    Log("CAviSynthFilter::CAviSynthFilter()");
}

CAviSynthFilter::~CAviSynthFilter() {
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
        if (_upstreamPin != pPin) {
            _upstreamPin = pPin;
            DeletePinTypes();
        }

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
                    if (!ReloadAviSynth(*nextType)) {
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
    if (pmt != nullptr && pmt->pbFormat != nullptr) {
        StopStreaming();
        m_pInput->SetMediaType(reinterpret_cast<CMediaType *>(pmt));

        hr = HandleInputFormatChange(pmt);
        if (FAILED(hr)) {
            return AbortPlayback(hr);
        }
        reloadedAvsForFormatChange = hr == S_OK;

        DeleteMediaType(pmt);
        hr = StartStreaming();
        if (FAILED(hr)) {
            return AbortPlayback(hr);
        }
    }

    if (ShouldSkipFrame(pSample)) {
        MSR_NOTE(m_idSkip);
        m_bSampleSkipped = TRUE;
        return NOERROR;
    }

    CheckHr(m_pOutput->GetDeliveryBuffer(&pOutSample, nullptr, nullptr, 0));
    m_bSampleSkipped = FALSE;

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
        m_tDecodeStart = timeGetTime();
        MSR_START(m_idTransform);

        hr = TransformAndDeliver(pSample, reloadedAvsForFormatChange, confirmNewOutputFormat);

        MSR_STOP(m_idTransform);
        m_tDecodeStart = timeGetTime() - m_tDecodeStart;
        m_itrAvgDecode = m_tDecodeStart * (10000 / 16) + 15 * (m_itrAvgDecode / 16);

        if (m_nWaitForKey)
            m_nWaitForKey--;
        if (m_nWaitForKey && pSample->IsSyncPoint() == S_OK)
            m_nWaitForKey = FALSE;

        if (m_nWaitForKey) {
            hr = S_FALSE;
        }
    }

    if (S_FALSE == hr) {
        m_bSampleSkipped = TRUE;
        if (!m_bQualityChanged) {
            m_bQualityChanged = TRUE;
            NotifyEvent(EC_QUALITY_CHANGE, 0, 0);
        }
        return NOERROR;
    }

    return hr;
}

auto CAviSynthFilter::TransformAndDeliver(IMediaSample *pIn, bool reloadedAvsForFormatChange, bool confirmNewOutputFormat) -> HRESULT {
    HRESULT hr;

    CRefTime streamTime;
    CheckHr(StreamTime(streamTime));
    streamTime = min(streamTime, m_tStart);

    REFERENCE_TIME inStartTime, inStopTime;
    if (pIn->GetTime(&inStartTime, &inStopTime) == VFW_E_SAMPLE_TIME_NOT_SET) {
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
    }

    const int inSampleFrameNb = static_cast<int>(inStartTime / _timePerFrame);

    Log("late: %10i timePerFrame: %lli streamTime: %10lli streamFrameNb: %4lli sampleTime: %10lli sampleFrameNb: %4i",
        m_itrLate, _timePerFrame, static_cast<REFERENCE_TIME>(streamTime), static_cast<REFERENCE_TIME>(streamTime) / _timePerFrame, inStartTime, inSampleFrameNb);

    if (_reloadAvsFile) {
        if (!reloadedAvsForFormatChange) {
            ReloadAviSynth(m_pInput->CurrentMediaType());
        }
        _deliveryFrameNb = inSampleFrameNb;
        _reloadAvsFile = false;
    }

    const REFERENCE_TIME gcMinTime = (static_cast<REFERENCE_TIME>(_deliveryFrameNb) - _bufferBack) * _timePerFrame;
    _frameHandler.GarbageCollect(gcMinTime, inStartTime);

    BYTE *inputBuffer;
    CheckHr(pIn->GetPointer(&inputBuffer));
    _frameHandler.CreateFrame(_inputFormat, inStartTime, inputBuffer, _avsEnv);

    bool refreshedOvertime = false;
    while (_deliveryFrameNb + _bufferAhead <= inSampleFrameNb) {
        IMediaSample *outSample = nullptr;
        CheckHr(InitializeOutputSample(nullptr, &outSample));

        if (confirmNewOutputFormat) {
            CheckHr(outSample->SetMediaType(&m_pOutput->CurrentMediaType()));
            confirmNewOutputFormat = false;
        }

        // even if missing sample times from upstream, we always set times for output samples in case downstream needs them
        REFERENCE_TIME outStartTime = _deliveryFrameNb * _timePerFrame;
        REFERENCE_TIME outStopTime = outStartTime + _timePerFrame;
        CheckHr(outSample->SetTime(&outStartTime, &outStopTime));

        BYTE *outBuffer;
        CheckHr(outSample->GetPointer(&outBuffer));

        const PVideoFrame clipFrame = _avsScriptClip->GetFrame(_deliveryFrameNb, _avsEnv);
        FrameHandler::WriteSample(_outputFormat, clipFrame, outBuffer, _avsEnv);
        refreshedOvertime = true;

        CheckHr(m_pOutput->Deliver(outSample));

        outSample->Release();
        outSample = nullptr;

        Log("Deliver frameNb: %4i at %10lli inSampleFrameNb: %4i", _deliveryFrameNb, outStartTime, inSampleFrameNb);

        _deliveryFrameNb += 1;
    }

    if (refreshedOvertime) {
        const bool hasAheadOvertime = _frameHandler.GetAheadOvertime() > 0;
        const bool hasBackOvertime = _frameHandler.GetBackOvertime() > 0;
        if (hasAheadOvertime) {
            _bufferAhead += 1;
        }
        if (hasBackOvertime) {
            _bufferBack += 1;
        }

        // calibrate for optimal buffer sizes if we don't have one
        if (_stableBufferAhead == 0 && _stableBufferBack == 0) {
            if (!hasAheadOvertime && !hasBackOvertime) {
                _consecutiveStableFrames += 1;
            } else {
                _consecutiveStableFrames = 0;
            }

            // consider 1 second of video stream without overtime "stable"
            if (_consecutiveStableFrames >= _avsSourceVideoInfo.fps_numerator / _avsSourceVideoInfo.fps_denominator) {
                if (_stableBufferAhead != _bufferAhead) {
                    _stableBufferAhead = _bufferAhead;
                }
                if (_stableBufferBack != _bufferBack) {
                    _stableBufferBack = _bufferBack;
                }
            }
        }
    }

    return S_OK;
}

auto CAviSynthFilter::BeginFlush() -> HRESULT {
    Reset();
    return __super::BeginFlush();
}

auto STDMETHODCALLTYPE CAviSynthFilter::Pause() -> HRESULT {
    _inputFormat = Format::GetVideoFormat(m_pInput->CurrentMediaType());
    _outputFormat = Format::GetVideoFormat(m_pOutput->CurrentMediaType());
    Reset();
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
    _stableBufferAhead = 0;
    _stableBufferBack = 0;
    Reset();
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

auto STDMETHODCALLTYPE CAviSynthFilter::GetBufferAhead() const -> int {
    return _bufferAhead;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetBufferAheadOvertime() -> int {
    return static_cast<int>(_frameHandler.GetAheadOvertime() / _timePerFrame);
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetBufferBack() const -> int {
    return _bufferBack;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetBufferBackOvertime() -> int {
    return static_cast<int>(_frameHandler.GetBackOvertime() / _timePerFrame);
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetSampleTimeOffset() const -> int {
    return _sampleTimeOffset;
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

/**
 * Whenever the media type on the input pin is changed, we need to run the avs script against
 * the new type to generate corresponding output type. Then we need to update the downstream with it.
 *
 * returns S_OK if the avs script is reloaded due to format change.
 */
auto CAviSynthFilter::HandleInputFormatChange(const AM_MEDIA_TYPE *pmt) -> HRESULT {
    HRESULT hr;

    const Format::VideoFormat receivedInputFormat = Format::GetVideoFormat(*pmt);
    if (_inputFormat != receivedInputFormat) {
        Log("new input format:  definition %i, width %5i, height %5i, codec %#10x",
            receivedInputFormat.definition, receivedInputFormat.bmi.biWidth, receivedInputFormat.bmi.biHeight, receivedInputFormat.bmi.biCompression);
        _inputFormat = receivedInputFormat;

        ReloadAviSynth(*pmt);
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
        Log("new output format: definition %i, width %5i, height %5i, codec %#10x",
            newOutputFormat.definition, newOutputFormat.bmi.biWidth, newOutputFormat.bmi.biHeight, newOutputFormat.bmi.biCompression);
        _outputFormat = newOutputFormat;

        return S_OK;
    }

    return S_FALSE;
}

auto CAviSynthFilter::Reset() -> void {
    _bufferAhead = _stableBufferAhead;
    _bufferBack = _stableBufferBack;
    _sampleTimeOffset = 0;
    _consecutiveStableFrames = 0;
    _reloadAvsFile = true;
}

auto CAviSynthFilter::LoadSettings() -> void {
    _avsFile = _registry.ReadString(REGISTRY_VALUE_NAME_AVS_FILE);
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
    newVih->AvgTimePerFrame = _timePerFrame;

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
    }
}

/**
 * Create new AviSynth script clip with specified media type.
 * If allowDisconnect == true, return false early if no avs script or AvsFilterDisconnect() is returned from script
 */
auto CAviSynthFilter::ReloadAviSynth(const AM_MEDIA_TYPE &mediaType) -> bool {
    _avsSourceVideoInfo = Format::GetVideoFormat(mediaType).videoInfo;

    /*
     * When reloading AviSynth, there are two alternative approaches:
     *     Reload everything (the environment, the scripts).
     *     Only reload the scripts.
     * And for seeking, we could either reload or not reload.
     *
     * Because there is no way to disable internal caching in AviSynth, not reloading for seeking is not working,
     * or after seeking the frames will not catch up for considerable amount of time.
     *
     * If only reload the scripts, the internal states of the environment is not reset. This creates problem for
     * certain filters such as SVP's SVSmoothFps_NVOF().
     */
    /*
    if (m_State != State_Stopped) {
        DeleteAviSynth();
        CreateAviSynth();
    }
    */

    AVSValue invokeResult;
    std::string errorScript;
    try {
        bool isImportSuccess = false;

        if (!_avsFile.empty()) {
            const std::string utf8File = ConvertWideToUtf8(_avsFile);
            AVSValue args[2] = { utf8File.c_str(), true };
            const char* const argNames[2] = { nullptr, "utf8" };
            invokeResult = _avsEnv->Invoke("Import", AVSValue(args, 2), argNames);
            isImportSuccess = invokeResult.Defined();
        }

        if (!isImportSuccess) {
            if (m_State == State_Stopped) {
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
