#include "pch.h"
#include "filter.h"
#include "prop_settings.h"
#include "constants.h"
#include "source_clip.h"


#define CheckHr(expr) { hr = (expr); if (FAILED(hr)) { return hr; } }

auto __cdecl CreateAvsFilterSource(AVSValue args, void *user_data, IScriptEnvironment *env) -> AVSValue {
    return reinterpret_cast<SourceClip *>(user_data);
}

auto __cdecl CreateAvsFilterDisconnect(AVSValue args, void *user_data, IScriptEnvironment *env) -> AVSValue {
    // the void type is internal in AviSynth and cannot be instantiated by user script, ideal for disconnect heuristic
    return AVSValue();
}

CAviSynthFilter::CAviSynthFilter(LPUNKNOWN pUnk, HRESULT *phr)
    : CVideoTransformFilter(NAME(FILTER_NAME_FULL), pUnk, CLSID_AviSynthFilter)
    , _avsEnv(nullptr)
    , _avsScriptClip(nullptr) {
    LoadSettings();
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

    return CVideoTransformFilter::NonDelegatingQueryInterface(riid, ppv);
}

auto CAviSynthFilter::CheckConnect(PIN_DIRECTION direction, IPin *pPin) -> HRESULT {
    HRESULT hr;

    if (direction == PINDIR_INPUT) {
        ASSERT(_compatibleDefinitions.empty());

        IEnumMediaTypes *enumTypes;
        CheckHr(pPin->EnumMediaTypes(&enumTypes));

        AM_MEDIA_TYPE *nextType;
        while (true) {
            hr = enumTypes->Next(1, &nextType, nullptr);
            if (hr == S_OK) {
                const int inputDefinition = GetInputDefinition(nextType);

                // for each group of formats with the same avs type, only add the first one that upstream supports.
                // this one will be the preferred media type for potential pin reconnection
                if (inputDefinition != INVALID_DEFINITION && !IsInputUniqueByAvsType(inputDefinition)) {
                    // invoke AviSynth script with each supported input definition, and observe the output avs type
                    if (!ReloadAviSynth(nextType, true)) {
                        DbgLog((LOG_TRACE, 1, "Disconnect due to avsfilter_disconnect()"));
                        return VFW_E_CANNOT_CONNECT;
                    }

                    _acceptableInputTypes.emplace(inputDefinition, nextType);
                    DbgLog((LOG_TRACE, 1, "Add acceptable input definition: %2i", inputDefinition));

                    // all media types that share the same avs type are acceptable for output pin connection
                    for (int outputDefinition : Format::LookupAvsType(_avsScriptVideoInfo.pixel_type)) {
                        if (_acceptableOuputTypes.find(outputDefinition) == _acceptableOuputTypes.end()) {
                            AM_MEDIA_TYPE *outputType = GenerateMediaType(outputDefinition, nextType);
                            _acceptableOuputTypes.emplace(outputDefinition, outputType);
                            DbgLog((LOG_TRACE, 1, "Add acceptable output definition: %2i", outputDefinition));

                            _compatibleDefinitions.emplace_back(DefinitionPair { inputDefinition, outputDefinition });
                            DbgLog((LOG_TRACE, 1, "Add compatible definitions: input %2i output %2i", inputDefinition, outputDefinition));
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

    return CVideoTransformFilter::CheckConnect(direction, pPin);
}

auto CAviSynthFilter::BreakConnect(PIN_DIRECTION direction) -> HRESULT {
    if (direction == PINDIR_INPUT) {
        DeletePinTypes();
    }

    return CVideoTransformFilter::BreakConnect(direction);
}

auto CAviSynthFilter::CheckInputType(const CMediaType *mtIn) -> HRESULT {
    const int definition = GetInputDefinition(mtIn);

    if (_acceptableInputTypes.find(definition) == _acceptableInputTypes.end()) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    DbgLog((LOG_TRACE, 1, "Accept input definition: %2i", definition));

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

    DbgLog((LOG_TRACE, 1, "Offer output definition: %2i", definition));

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

    DbgLog((LOG_TRACE, 1, "Accept transform: out %2i", outputDefinition));

    return S_OK;
}

auto CAviSynthFilter::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProperties) -> HRESULT {
    HRESULT hr;

    // we need at least 2 buffers so that we can hold pOut while preparing another extra sample
    pProperties->cBuffers = max(2, pProperties->cBuffers);
    pProperties->cbBuffer = max(m_pInput->CurrentMediaType().lSampleSize, static_cast<unsigned long>(pProperties->cbBuffer));

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
        DbgLog((LOG_TRACE, 1, "Connected input pin with definition: %2i", inputDefiniton));
    } else {
        const int outputDefinition = Format::LookupMediaSubtype(m_pOutput->CurrentMediaType().subtype);
        ASSERT(outputDefinition != INVALID_DEFINITION);

        const int compatibleInput = FindCompatibleInputByOutput(outputDefinition);
        if (inputDefiniton != compatibleInput) {
            DbgLog((LOG_TRACE, 1, "Reconnect with types: old in %2i new in %2i out %2i", inputDefiniton, compatibleInput, outputDefinition));
            CheckHr(ReconnectPin(m_pInput, _acceptableInputTypes[compatibleInput]));
        } else {
            DbgLog((LOG_TRACE, 1, "Connected with types: in %2i out %2i", inputDefiniton, outputDefinition));
        }
    }

    return CVideoTransformFilter::CompleteConnect(direction, pReceivePin);
}

auto CAviSynthFilter::StartStreaming() -> HRESULT {
    const Format::VideoFormat newInputType = Format::GetVideoFormat(m_pInput->CurrentMediaType());
    const Format::VideoFormat newOutputType = Format::GetVideoFormat(m_pOutput->CurrentMediaType());

    DbgLog((LOG_TRACE, 1, "new input type:  definition %i, width %5i, height %5i, codec %#10x",
           newInputType.definition, newInputType.bmi.biWidth, newInputType.bmi.biHeight, newInputType.bmi.biCompression));
    DbgLog((LOG_TRACE, 1, "new output type: definition %i, width %5i, height %5i, codec %#10x",
           newOutputType.definition, newOutputType.bmi.biWidth, newOutputType.bmi.biHeight, newOutputType.bmi.biCompression));

    if (_inputFormat != newInputType) {
        _inputFormat = newInputType;
        _reloadAvsFile = true;
    }

    if (_outputFormat != newOutputType) {
        _outputFormat = newOutputType;
        _reloadAvsFile = true;
    }

    return CVideoTransformFilter::StartStreaming();
}

auto CAviSynthFilter::Transform(IMediaSample *pIn, IMediaSample *pOut) -> HRESULT {
    HRESULT hr;

    CRefTime streamTime;
    CheckHr(StreamTime(streamTime));
    streamTime = min(streamTime, m_tStart);

    REFERENCE_TIME inputStartTime, inStopTime;
    if (pIn->GetTime(&inputStartTime, &inStopTime) == VFW_E_SAMPLE_TIME_NOT_SET) {
        /*
        Even when the upstream does not set sample time, we fill up the time in reference to the stream time.
        1) Some downstream (e.g. BlueskyFRC) needs sample time to function.
        2) If the start time is too close to the stream time, the renderer may drop the frame as being late.
           We add offset by time-per-frame until the lateness is gone
        */
        if (m_itrLate > 0) {
            _sampleTimeOffset += 1;
        }
        inputStartTime = streamTime + _sampleTimeOffset * _timePerFrame;
    }

    const int inSampleFrameNb = static_cast<int>(inputStartTime / _timePerFrame);

    DbgLog((LOG_TRACE, 2, "late: %10i timePerFrame: %lli streamTime: %10lli streamFrameNb: %4lli sampleTime: %10lli sampleFrameNb: %4i",
           m_itrLate, _timePerFrame, static_cast<REFERENCE_TIME>(streamTime), static_cast<REFERENCE_TIME>(streamTime) / _timePerFrame, inputStartTime, inSampleFrameNb));

    if (_reloadAvsFile) {
        ReloadAviSynth();
        _deliveryFrameNb = inSampleFrameNb;
    }

    const REFERENCE_TIME gcMinTime = (static_cast<REFERENCE_TIME>(_deliveryFrameNb) - _bufferBack) * _timePerFrame;
    _frameHandler.GarbageCollect(gcMinTime, inputStartTime);

    BYTE *inputBuffer;
    CheckHr(pIn->GetPointer(&inputBuffer));
    _frameHandler.CreateFrame(_inputFormat, inputStartTime, inputBuffer, _avsEnv);

    bool refreshedOvertime = false;
    while (_deliveryFrameNb + _bufferAhead <= inSampleFrameNb) {
        IMediaSample *outSample = nullptr;
        CheckHr(InitializeOutputSample(nullptr, &outSample));

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

        DbgLog((LOG_TRACE, 2, "Deliver frameNb: %4i at %10lli inSampleFrameNb: %4i", _deliveryFrameNb, outStartTime, inSampleFrameNb));

        _deliveryFrameNb += 1;
    }

    if (refreshedOvertime) {
        const int aheadOvertimeFrames = GetBufferAheadOvertime();
        const int backOvertimeFrames = GetBufferBackOvertime();

        if (aheadOvertimeFrames > 0) {
            _bufferAhead += 1;
        }
        if (backOvertimeFrames > 0) {
            _bufferBack += 1;
        }
    }

    /*
     * Returning S_FALSE because we deliver (or not deliver if condition not met) output samples ourselves.
     * This will cause parent class to send quality change event (which is undesirable but OK), and returning NOERROR further up.
     * We need that NOERROR in case upstream calls our input pin's ReceiveMultiple().
     * Alternatively, we can reimplement Receive() all by ourselves, which seems overkill at this moment.
     * Another option is to pOut->SetTime with some time that will never be shown, such as LONGLONG_MIN, but seems too hacky.
     */
    return S_FALSE;
}

auto CAviSynthFilter::BeginFlush() -> HRESULT {
    _bufferAhead = 0;
    _bufferBack = 0;
    _sampleTimeOffset = 0;
    _reloadAvsFile = true;

    return CVideoTransformFilter::BeginFlush();
}

auto STDMETHODCALLTYPE CAviSynthFilter::Pause() -> HRESULT {
    _inputFormat = Format::GetVideoFormat(m_pInput->CurrentMediaType());
    _outputFormat = Format::GetVideoFormat(m_pOutput->CurrentMediaType());
    _bufferAhead = 0;
    _bufferBack = 0;
    _sampleTimeOffset = 0;
    _reloadAvsFile = true;

    return CVideoTransformFilter::Pause();   
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

auto STDMETHODCALLTYPE CAviSynthFilter::GetAvsFile() const -> const std::string & {
    return _avsFile;
}

auto STDMETHODCALLTYPE CAviSynthFilter::SetAvsFile(const std::string &avsFile) -> void {
    _avsFile = avsFile;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetReloadAvsFile() const -> bool {
    return _reloadAvsFile;
}

auto STDMETHODCALLTYPE CAviSynthFilter::SetReloadAvsFile(bool reload) -> void {
    _reloadAvsFile = reload;
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

auto CAviSynthFilter::LoadSettings() -> void {
    _avsFile = _registry.ReadString(REGISTRY_VALUE_NAME_AVS_FILE);

    _inputFormatBits = _registry.ReadNumber(REGISTRY_VALUE_NAME_FORMATS);
    if (_inputFormatBits == INVALID_REGISTRY_NUMBER) {
        _inputFormatBits = (1 << Format::DEFINITIONS.size()) - 1;
    }
}

auto CAviSynthFilter::GetInputDefinition(const AM_MEDIA_TYPE *mediaType) const -> int {
    const int inputDefinition = MediaTypeToDefinition(mediaType);

    if (inputDefinition == INVALID_DEFINITION) {
        return INVALID_DEFINITION;
    }

    if ((_inputFormatBits & (1 << inputDefinition)) == 0) {
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
    const Format::VideoFormat format = Format::GetVideoFormat(*templateMediaType);
    FOURCCMap fourCC(&def.mediaSubtype);

    AM_MEDIA_TYPE *newMediaType = CreateMediaType(templateMediaType);
    newMediaType->subtype = def.mediaSubtype;

    VIDEOINFOHEADER *vih = reinterpret_cast<VIDEOINFOHEADER *>(newMediaType->pbFormat);
    vih->AvgTimePerFrame = _timePerFrame;

    BITMAPINFOHEADER *bmi = Format::GetBitmapInfo(*newMediaType);
    bmi->biWidth = format.videoInfo.width;
    bmi->biHeight = format.videoInfo.height;
    bmi->biBitCount = def.bitCount;
    bmi->biSizeImage = GetBitmapSize(bmi);
    newMediaType->lSampleSize = bmi->biSizeImage;

    if (IsEqualGUID(fourCC, def.mediaSubtype)) {
        // uncompressed formats (such as RGB32) have different GUIDs
        bmi->biCompression = fourCC.GetFOURCC();
    } else {
        bmi->biCompression = BI_RGB;
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

auto CAviSynthFilter::ReloadAviSynth() -> void {
    ReloadAviSynth(&m_pInput->CurrentMediaType(), false);
}

/**
 * Create new AviSynth script clip with specified media type.
 * If allowDisconnect == true, return false early if no avs script or avsfilter_disconnect() is returned from script
 */
auto CAviSynthFilter::ReloadAviSynth(const AM_MEDIA_TYPE *mediaType, bool allowDisconnect) -> bool {
    DeleteAviSynth();

    _avsSourceVideoInfo = Format::GetVideoFormat(*mediaType).videoInfo;

    _avsEnv = CreateScriptEnvironment2();
    _avsEnv->AddFunction("avsfilter_source", "", CreateAvsFilterSource, new SourceClip(_avsSourceVideoInfo, _frameHandler));
    _avsEnv->AddFunction("avsfilter_disconnect", "", CreateAvsFilterDisconnect, nullptr);

    AVSValue invokeResult;
    std::string errorScript;
    try {
        bool isImportSuccess = false;

        if (!_avsFile.empty()) {
            invokeResult = _avsEnv->Invoke("Import", AVSValue(_avsFile.c_str()), nullptr);
            isImportSuccess = invokeResult.Defined();
        }

        if (!isImportSuccess) {
            if (allowDisconnect) {
                return false;
            }

            AVSValue evalArgs[] = { AVSValue("return avsfilter_source()")
                                  , AVSValue(EVAL_FILENAME) };
            invokeResult = _avsEnv->Invoke("Eval", AVSValue(evalArgs, 2), nullptr);
        }

        if (!invokeResult.IsClip()) {
            errorScript = "Unrecognized return value from script. Invalid script.";
        }
    } catch (AvisynthError &err) {
        errorScript = err.msg;
        std::replace(errorScript.begin(), errorScript.end(), '"', '\'');
        std::replace(errorScript.begin(), errorScript.end(), '\n', ' ');
    }

    if (!errorScript.empty()) {
        errorScript.insert(0, "return avsfilter_source().Subtitle(\"");
        errorScript.append("\")");
        AVSValue evalArgs[] = { AVSValue(errorScript.c_str())
                              , AVSValue(EVAL_FILENAME) };
        invokeResult = _avsEnv->Invoke("Eval", AVSValue(evalArgs, 2), nullptr);
    }

    _avsScriptClip = invokeResult.AsClip();
    _avsScriptVideoInfo = _avsScriptClip->GetVideoInfo();
    _timePerFrame = llMulDiv(_avsScriptVideoInfo.fps_denominator, UNITS, _avsScriptVideoInfo.fps_numerator, 0);
    _reloadAvsFile = false;

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
            return true;
        }
    }
    return false;
}

auto CAviSynthFilter::FindCompatibleInputByOutput(int outputDefinition) const -> int {
    for (const DefinitionPair &pair : _compatibleDefinitions) {
        if (pair.output == outputDefinition) {
            return pair.input;
        }
    }
    return INVALID_DEFINITION;
}