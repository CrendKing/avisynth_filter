#include "pch.h"
#include "filter.h"
#include "filter_prop.h"
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
    : CVideoTransformFilter(NAME(FILTER_NAME), pUnk, CLSID_AviSynthFilter)
    , _avsEnv(nullptr)
    , _avsScriptClip(nullptr)
    , _isConnectingPins(true) {
    LoadSettings();
}

CAviSynthFilter::~CAviSynthFilter() {
    DeleteIndexedMediaTypes();
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

    return CVideoTransformFilter::NonDelegatingQueryInterface(riid, ppv);
}

auto CAviSynthFilter::CheckInputType(const CMediaType *mtIn) -> HRESULT {
    return ValidateMediaType(PINDIR_INPUT, mtIn);
}

auto CAviSynthFilter::GetMediaType(int iPosition, CMediaType *pMediaType) -> HRESULT {
    if (iPosition < 0) {
        return E_INVALIDARG;
    }

    if (m_pInput->IsConnected() == FALSE) {
        return E_UNEXPECTED;
    }

    if (iPosition >= static_cast<int>(_acceptableOutputTypes.size())) {
        return VFW_S_NO_MORE_ITEMS;
    }

    *pMediaType = *_acceptableOutputTypes[iPosition].mediaType;

    return S_OK;
}

auto CAviSynthFilter::CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut) -> HRESULT {
    // the actual check for compatible subtype is in CompleteConnect(). we want to allow all video media types for the output pin

    return ValidateMediaType(PINDIR_OUTPUT, mtOut);
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
    HRESULT hr;

    // set to true when avsType script returns avsfilter_disconnect()
    if (!_isConnectingPins) {
        return E_ABORT;
    }

    /*
     * The media type negotiation logic
     *
     * Suppose the upstream's output pin supports N1 media types. The AviSynth script converts those N1 input format to N2 output types.
     * The downstream's input pin supports N3 media types. Our Format class supports N4 media types.
     *
     * We need to find a common media type that is common in all the 4 arrays.
     * The postcondition of the pin connection process is that the input media type's avs type equals the output media type's avs type.
     *
     * The filter graph connects upstream's output pin to our input pin first. We accept every media type (minus those excluded from settings).
     * We enumerate the input pin for its N1 media types. We pass the media subtypes to the avs script and collect the N2 output avs type.
     * These avs types maps to some media types via the Format class. In GetMediaType(), we offer these types to downstream.
     * The downstream will select its preferred type and call CheckTransform(). We also accept every media type.
     *
     * At CompleteConnect(PINDIR_OUTPUT), both pins have connected. We check if the postcondition holds. If yes, the pin connection completes.
     * If not, we keep the connection to the downstream, reverse lookup the preferred input media type according to its avs type, and reconnect input pin.
     *
     * Because the reconnect media type is selected from upstream's enumerated media type, the connection should always succeed at the second time.
     */

    if (!m_pInput->IsConnected()) {
        return E_UNEXPECTED;
    }

    if (!m_pOutput->IsConnected()) {
        ASSERT(_indexedInputTypes.empty() && _acceptableOutputTypes.empty());

        IEnumMediaTypes *enumTypes;
        CheckHr(pReceivePin->EnumMediaTypes(&enumTypes));

        AM_MEDIA_TYPE *nextType;
        while (true) {
            hr = enumTypes->Next(1, &nextType, nullptr);
            if (hr == S_OK) {
                const int formatIndex = Format::LookupMediaSubtype(nextType->subtype);
                // only add unique formats. the enumerated media types may have the same subtype where their pbFormat field vary
                if (formatIndex != INVALID_FORMAT_INDEX && !IsTypeExistForIndex(_indexedInputTypes, formatIndex)) {
                    _indexedInputTypes.emplace_back(IndexedMediaType { formatIndex, nextType });
                } else {
                    DeleteMediaType(nextType);
                }
            } else if (hr == VFW_E_ENUM_OUT_OF_SYNC) {
                CheckHr(enumTypes->Reset());
                DeleteIndexedMediaTypes();
            } else {
                break;
            }
        }

        enumTypes->Release();

        for (const IndexedMediaType &type : _indexedInputTypes) {
            ReloadAviSynth(type.formatIndex);
            if (!_isConnectingPins) {
                return E_ABORT;
            }

            for (int formatIndex : Format::LookupAvsType(_avsScriptVideoInfo.pixel_type)) {
                if (!IsTypeExistForIndex(_acceptableOutputTypes, formatIndex)) {
                    AM_MEDIA_TYPE *outputType = GenerateMediaType(formatIndex, &m_pInput->CurrentMediaType());
                    _acceptableOutputTypes.emplace_back(IndexedMediaType { formatIndex, outputType });
                }
            }
        }
    } else {
        const int inputFormatIndex = Format::LookupMediaSubtype(m_pInput->CurrentMediaType().subtype);
        const int outputFormatIndex = Format::LookupMediaSubtype(m_pOutput->CurrentMediaType().subtype);
        ASSERT(inputFormatIndex != INVALID_FORMAT_INDEX && outputFormatIndex != INVALID_FORMAT_INDEX);

        if (Format::FORMATS[inputFormatIndex].avsType != Format::FORMATS[outputFormatIndex].avsType) {
            const IndexedMediaType *reconnectType = FindFirstMatchingType(_indexedInputTypes, outputFormatIndex);
            CheckHr(ReconnectPin(m_pInput, reconnectType->mediaType));
        } else {
            DeleteIndexedMediaTypes();
            _inputMediaTypeInfo = Format::GetMediaTypeInfo(m_pInput->CurrentMediaType());
            _outputMediaTypeInfo = Format::GetMediaTypeInfo(m_pOutput->CurrentMediaType());
            _isConnectingPins = false;
            _reloadAvsFile = true;

#ifdef LOGGING
            printf("Connected with media types: in %2i out %2i\n", inputFormatIndex, outputFormatIndex);
#endif
        }
    }

    return S_OK;
}

auto CAviSynthFilter::StartStreaming() -> HRESULT {
    const Format::MediaTypeInfo newInputType = Format::GetMediaTypeInfo(m_pInput->CurrentMediaType());
    const Format::MediaTypeInfo newOutputType = Format::GetMediaTypeInfo(m_pOutput->CurrentMediaType());

#ifdef LOGGING
    printf("new input type:  format %i, width %5i, height %5i, codec %#8x\n",
           newInputType.formatIndex, newInputType.bmi.biWidth, newInputType.bmi.biHeight, newInputType.bmi.biCompression);
    printf("new output type: format %i, width %5i, height %5i, codec %#8x\n",
           newOutputType.formatIndex, newOutputType.bmi.biWidth, newOutputType.bmi.biHeight, newOutputType.bmi.biCompression);
#endif

    if (_inputMediaTypeInfo != newInputType) {
        _inputMediaTypeInfo = newInputType;
        _reloadAvsFile = true;
    }

    if (_outputMediaTypeInfo != newOutputType) {
        _outputMediaTypeInfo = newOutputType;
        _reloadAvsFile = true;
    }

    return CVideoTransformFilter::StartStreaming();
}

auto CAviSynthFilter::Transform(IMediaSample *pIn, IMediaSample *pOut) -> HRESULT {
    HRESULT hr;

    if (_reloadAvsFile) {
        _reloadAvsFile = false;
        ReloadAviSynth();
    }

    CRefTime streamTime;
    CheckHr(StreamTime(streamTime));
    streamTime = min(streamTime, m_tStart);

    REFERENCE_TIME inStartTime, inStopTime;
    hr = pIn->GetTime(&inStartTime, &inStopTime);
    if (FAILED(hr)) {
        inStartTime = streamTime;
    }

#ifdef LOGGING
    printf("late: %10i timePerFrame: %lli streamTime: %10lli streamFrameNb: %4lli sampleTime: %10lli sampleFrameNb: %4i\n",
           m_itrLate, _timePerFrame, static_cast<REFERENCE_TIME>(streamTime), static_cast<REFERENCE_TIME>(streamTime) / _timePerFrame, inStartTime, _inSampleFrameNb);
#endif

    _inSampleFrameNb = static_cast<int>(inStartTime / _timePerFrame);

    if (_deliveryFrameNb == DELIVER_FRAME_NB_RESET) {
        _deliveryFrameNb = _inSampleFrameNb;
    }

    const REFERENCE_TIME gcMinTime = (static_cast<REFERENCE_TIME>(_deliveryFrameNb) - _bufferBack) * _timePerFrame;
   _bufferHandler.GarbageCollect(gcMinTime, inStartTime);

    BYTE *inputBuffer;
    CheckHr(pIn->GetPointer(&inputBuffer));
    _bufferHandler.CreateFrame(_inputMediaTypeInfo, inStartTime, inputBuffer, _avsEnv);

    {
        // need mutex to protect the entire AviSynth environment in case a seeking happens in the meantime
        std::unique_lock<std::mutex> lock(_avsMutex);

        while (_deliveryFrameNb + _bufferAhead <= _inSampleFrameNb) {
            IMediaSample *outSample = nullptr;
            CheckHr(InitializeOutputSample(nullptr, &outSample));

            REFERENCE_TIME outStartTime = _deliveryFrameNb * _timePerFrame;
            REFERENCE_TIME outStopTime = outStartTime + _timePerFrame;
            CheckHr(outSample->SetTime(&outStartTime, &outStopTime));

            BYTE *outBuffer;
            CheckHr(outSample->GetPointer(&outBuffer));

            const PVideoFrame clipFrame = _avsScriptClip->GetFrame(_deliveryFrameNb, _avsEnv);
            BufferHandler::WriteSample(_outputMediaTypeInfo, clipFrame, outBuffer, _avsEnv);

            CheckHr(m_pOutput->Deliver(outSample));

            outSample->Release();
            outSample = nullptr;

#ifdef LOGGING
            printf("Deliver frameNb: %4i at %10lli inSampleFrameNb: %4i\n", _deliveryFrameNb, outStartTime, _inSampleFrameNb);
#endif

            _deliveryFrameNb += 1;
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
    HRESULT hr;

    CheckHr(CVideoTransformFilter::BeginFlush());

    {
        // BeginFlush() is called in the main thread, while Transform() is on a worker thread

        std::unique_lock<std::mutex> lock(_avsMutex);
        ReloadAviSynth();
    }

    return hr;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetPages(CAUUID *pPages) -> HRESULT {
    CheckPointer(pPages, E_POINTER);

    pPages->pElems = static_cast<GUID *>(CoTaskMemAlloc(sizeof(GUID)));
    if (pPages->pElems == nullptr) {
        return E_OUTOFMEMORY;
    }

    pPages->cElems = 1;
    pPages->pElems[0] = CLSID_AvsPropertyPage;

    return S_OK;
}

auto STDMETHODCALLTYPE CAviSynthFilter::SaveSettings() const -> void {
    _registry.WriteString(REGISTRY_VALUE_NAME_AVS_FILE, _avsFile);
    _registry.WriteNumber(REGISTRY_VALUE_NAME_BUFFER_BACK, _bufferBack);
    _registry.WriteNumber(REGISTRY_VALUE_NAME_BUFFER_AHEAD, _bufferAhead);
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

auto STDMETHODCALLTYPE CAviSynthFilter::GetBufferBack() const -> int {
    return _bufferBack;
}

auto STDMETHODCALLTYPE CAviSynthFilter::SetBufferBack(int bufferBack) -> void {
    _bufferBack = bufferBack;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetBufferAhead() const -> int {
    return _bufferAhead;
}

auto STDMETHODCALLTYPE CAviSynthFilter::SetBufferAhead(int bufferAhead) -> void {
    _bufferAhead = bufferAhead;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetInputFormats() const -> DWORD {
    return _inputFormatBits;
}

auto STDMETHODCALLTYPE CAviSynthFilter::SetInputFormats(DWORD formatBits) -> void {
    _inputFormatBits = formatBits;
}

/**
 * check if any IndexedMediaType in the container has the same format index as the specified
 */
auto CAviSynthFilter::IsTypeExistForIndex(const std::vector<IndexedMediaType> &container, int formatIndex) -> bool {
    return std::find_if(container.begin(), container.end(), [&formatIndex](const IndexedMediaType &value) {
        return value.formatIndex == formatIndex;
    }) != container.end();
}

/**
 * find the first IndexedMediaType that shares the avsType with the specified format
 */
auto CAviSynthFilter::FindFirstMatchingType(const std::vector<IndexedMediaType> &container, int formatIndex) -> const IndexedMediaType * {
    const int acceptableAvsType = Format::FORMATS[formatIndex].avsType;
    const std::vector<int> candidates = Format::LookupAvsType(acceptableAvsType);

    for (int c : candidates) {
        const auto iter = std::find_if(container.begin(), container.end(), [&c](const IndexedMediaType &value) {
            return value.formatIndex == c;
        });
        if (iter != container.end()) {
            return &*iter;
        }
    }

    return nullptr;
}

auto CAviSynthFilter::LoadSettings() -> void {
    _avsFile = _registry.ReadString(REGISTRY_VALUE_NAME_AVS_FILE);

    _bufferBack = _registry.ReadNumber(REGISTRY_VALUE_NAME_BUFFER_BACK);
    if (_bufferBack == INVALID_REGISTRY_NUMBER || _bufferBack < BUFFER_FRAMES_MIN || _bufferBack > BUFFER_FRAMES_MAX) {
        _bufferBack = BUFFER_BACK_DEFAULT;
    }

    _bufferAhead = _registry.ReadNumber(REGISTRY_VALUE_NAME_BUFFER_AHEAD);
    if (_bufferAhead == INVALID_REGISTRY_NUMBER || _bufferAhead < BUFFER_FRAMES_MIN || _bufferAhead > BUFFER_FRAMES_MAX) {
        _bufferAhead = BUFFER_AHEAD_DEFAULT;
    }

    _inputFormatBits = _registry.ReadNumber(REGISTRY_VALUE_NAME_FORMATS);
    if (_inputFormatBits == INVALID_REGISTRY_NUMBER) {
        _inputFormatBits = (1 << Format::FORMATS.size()) - 1;
    }
}

/**
 * Check if the media type has valid VideoInfo * format block.
 */
auto CAviSynthFilter::ValidateMediaType(PIN_DIRECTION direction, const AM_MEDIA_TYPE *mediaType) const -> HRESULT {
    if (mediaType->majortype != MEDIATYPE_Video) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    if (FAILED(CheckVideoInfoType(mediaType)) && FAILED(CheckVideoInfo2Type(mediaType))) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    const int formatIndex = Format::LookupMediaSubtype(mediaType->subtype);

    if (formatIndex == INVALID_FORMAT_INDEX) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    if (direction == PINDIR_INPUT && (_inputFormatBits & (1 << formatIndex)) == 0) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    return S_OK;
}

/**
 * Create media type based on a template while changing its subtype. Also change fields in format if necessary.
 *
 * For example, when the original subtype has 8-bit samples and new subtype has 16-bit,
 * all "size" and FourCC values will be adjusted.
 */
auto CAviSynthFilter::GenerateMediaType(int formatIndex, const AM_MEDIA_TYPE *templateMediaType) const -> AM_MEDIA_TYPE * {
    const Format::FormatInfo &format = Format::FORMATS[formatIndex];

    AM_MEDIA_TYPE * newMediaType = CreateMediaType(templateMediaType);
    newMediaType->subtype = format.mediaSubtype;

    VIDEOINFOHEADER *vih = reinterpret_cast<VIDEOINFOHEADER *>(newMediaType->pbFormat);
    vih->AvgTimePerFrame = _timePerFrame;

    const Format::MediaTypeInfo info = Format::GetMediaTypeInfo(*newMediaType);
    BITMAPINFOHEADER *bmi = Format::GetBitmapInfo(*newMediaType);
    bmi->biWidth = info.videoInfo.width;
    bmi->biHeight = info.videoInfo.height;
    bmi->biBitCount = format.bitCount;
    bmi->biSizeImage = GetBitmapSize(bmi);

    FOURCCMap fourCC(&format.mediaSubtype);
    if (IsEqualGUID(fourCC, format.mediaSubtype)) {
        // uncompressed formats (such as RGB32) have different GUIDs
        bmi->biCompression = fourCC.GetFOURCC();
    } else {
        bmi->biCompression = 0;
    }

#ifdef LOGGING
    printf("Acceptable output type: format %2i timePerFrame %10lli\n", formatIndex, _timePerFrame);
#endif

    return newMediaType;
}

auto CAviSynthFilter::DeleteIndexedMediaTypes() -> void {
    for (const IndexedMediaType &type : _indexedInputTypes) {
        DeleteMediaType(type.mediaType);
    }
    _indexedInputTypes.clear();

    for (const IndexedMediaType &type : _acceptableOutputTypes) {
        DeleteMediaType(type.mediaType);
    }
    _acceptableOutputTypes.clear();
}

/**
 * Delete the AviSynth environment and all its cache
 */
auto CAviSynthFilter::DeleteAviSynth() -> void {
    if (_avsScriptClip != nullptr) {
        _avsScriptClip = nullptr;
    }

    _bufferHandler.Flush();

    if (_avsEnv != nullptr) {
        _avsEnv->DeleteScriptEnvironment();
    }
}

auto CAviSynthFilter::ReloadAviSynth() -> void {
    ReloadAviSynth(INVALID_FORMAT_INDEX);
}

/**
 * Create new AviSynth script clip with current input media type.
 * During pin connection phase, if avsfilter_disconnect() is returned from script, teminate the phase
 */
auto CAviSynthFilter::ReloadAviSynth(int forceFormatIndex) -> void {
    DeleteAviSynth();

    _avsSourceVideoInfo = Format::GetMediaTypeInfo(m_pInput->CurrentMediaType()).videoInfo;
    if (forceFormatIndex != INVALID_FORMAT_INDEX) {
        _avsSourceVideoInfo.pixel_type = Format::FORMATS[forceFormatIndex].avsType;
    }

    _avsEnv = CreateScriptEnvironment2();
    _avsEnv->AddFunction("avsfilter_source", "", CreateAvsFilterSource, new SourceClip(_avsSourceVideoInfo, _bufferHandler));
    _avsEnv->AddFunction("avsfilter_disconnect", "", CreateAvsFilterDisconnect, nullptr);

    AVSValue invokeResult;
    std::string errorScript;
    try {
        bool isInvokeSuccess = false;

        if (!_avsFile.empty()) {
            invokeResult = _avsEnv->Invoke("Import", AVSValue(_avsFile.c_str()), nullptr);
            isInvokeSuccess = invokeResult.Defined();
        }

        if (!isInvokeSuccess) {
            if (_isConnectingPins) {
                _isConnectingPins = false;
                return;
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
    _deliveryFrameNb = DELIVER_FRAME_NB_RESET;
}