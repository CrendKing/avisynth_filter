#include "pch.h"
#include "filter.h"
#include "api.h"
#include "constants.h"
#include "environment.h"
#include "input_pin.h"
#include "source_clip.h"
#include "util.h"
#include "version.h"


namespace AvsFilter {

#define CheckHr(expr) { hr = (expr); if (FAILED(hr)) { return hr; } }

auto __cdecl Create_AvsFilterSource(AVSValue args, void *user_data, IScriptEnvironment *env) -> AVSValue {
    return static_cast<IClip *>(user_data);
}

auto __cdecl Create_AvsFilterDisconnect(AVSValue args, void *user_data, IScriptEnvironment *env) -> AVSValue {
    // the void type is internal in AviSynth and cannot be instantiated by user script, ideal for disconnect heuristic
    return AVSValue();
}

CAviSynthFilter::CAviSynthFilter(LPUNKNOWN pUnk, HRESULT *phr)
    : CVideoTransformFilter(NAME(FILTER_NAME_FULL), pUnk, CLSID_AviSynthFilter)
    , frameHandler(*this)
    , _disconnectFilter(false)
    , _acceptableInputTypes(Format::DEFINITIONS.size())
    , _acceptableOutputTypes(Format::DEFINITIONS.size())
    , _inputFormat()
    , _outputFormat()
    , _confirmNewOutputFormat(false)
    , _effectiveAvsFile(g_env.GetAvsFile())
    , _avsVersionString(nullptr)
    , _avsSourceClip(nullptr)
    , _avsScriptClip(nullptr)
    , _avsSourceVideoInfo()
    , _avsScriptVideoInfo()
    , _sourceAvgFrameTime(0)
    , _scriptAvgFrameTime(0)
    , _sourceAvgFrameRate(0)
    , _reloadAvsSource(false) {
    g_env.Log("CAviSynthFilter::CAviSynthFilter()");

    if (g_env.IsRemoteControlEnabled()) {
        _remoteControl.emplace(*this);
    }
}

CAviSynthFilter::~CAviSynthFilter() {
    StopAviSynthScript();
    DeletePinTypes();
}

auto STDMETHODCALLTYPE CAviSynthFilter::NonDelegatingQueryInterface(REFIID riid, __deref_out void **ppv) -> HRESULT {
    CheckPointer(ppv, E_POINTER);

    if (riid == IID_IAvsFilter) {
        return GetInterface(reinterpret_cast<IUnknown *>(this), ppv);
    }
    if (riid == IID_ISpecifyPropertyPages) {
        return GetInterface(static_cast<ISpecifyPropertyPages *>(this), ppv);
    }

    return __super::NonDelegatingQueryInterface(riid, ppv);
}

auto STDMETHODCALLTYPE CAviSynthFilter::NonDelegatingRelease() -> ULONG {
    const ULONG ret = __super::NonDelegatingRelease();

    if (ret == 0) {
        g_env.Destroy();
    }

    return ret;
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
    HRESULT hr;

    if (!_disconnectFilter) {
        if (!InitAviSynth()) {
            _disconnectFilter = true;
            return E_FAIL;
        }

        if (direction == PINDIR_INPUT) {
            ATL::CComPtr<IEnumMediaTypes> enumTypes;
            CheckHr(pPin->EnumMediaTypes(&enumTypes));

            AM_MEDIA_TYPE *nextType;
            while (true) {
                hr = enumTypes->Next(1, &nextType, nullptr);
                if (hr == S_OK) {
                    // for each group of formats with the same avs type, only add the first one that upstream supports.
                    // this one will be the preferred media type for potential pin reconnection

                    const std::optional<int> optInputDefinition = GetInputDefinition(nextType);

                    if (optInputDefinition && IsInputUniqueByAvsType(*optInputDefinition)) {
                        const int inputDefinition = *optInputDefinition;

                        // invoke AviSynth script with each supported input definition, and observe the output avs type
                        if (!ReloadAviSynthScript(*nextType)) {
                            g_env.Log("Disconnect due to AvsFilterDisconnect()");
                            DeleteMediaType(nextType);
                            _disconnectFilter = true;
                            break;
                        }

                        _acceptableInputTypes[inputDefinition] = nextType;
                        g_env.Log("Add acceptable input definition: %2i", inputDefinition);

                        // all media types that share the same avs type are acceptable for output pin connection
                        for (int outputDefinition : Format::LookupAvsType(_avsScriptVideoInfo.pixel_type)) {
                            if (_acceptableOutputTypes[outputDefinition] == nullptr) {
                                AM_MEDIA_TYPE *outputType = GenerateMediaType(outputDefinition, nextType);
                                _acceptableOutputTypes[outputDefinition] = outputType;
                                g_env.Log("Add acceptable output definition: %2i", outputDefinition);

                                _compatibleDefinitions.emplace_back(DefinitionPair { inputDefinition, outputDefinition });
                                g_env.Log("Add compatible definitions: input %2i output %2i", inputDefinition, outputDefinition);
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
        }
    }

    return _disconnectFilter ? VFW_E_TYPE_NOT_ACCEPTED : S_OK;
}

auto CAviSynthFilter::CheckInputType(const CMediaType *mtIn) -> HRESULT {
    const std::optional<int> optInputDefinition = GetInputDefinition(mtIn);

    if (optInputDefinition && _acceptableInputTypes[*optInputDefinition] != nullptr) {
        g_env.Log("Accept input definition: %2i", *optInputDefinition);
        return S_OK;
    }

    return VFW_E_TYPE_NOT_ACCEPTED;
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
    *pMediaType = *_acceptableOutputTypes[definition];

    g_env.Log("Offer output definition: %2i", definition);

    return S_OK;
}

auto CAviSynthFilter::CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut) -> HRESULT {
    const std::optional<int> optOutputDefinition = MediaTypeToDefinition(mtOut);

    if (optOutputDefinition && _acceptableOutputTypes[*optOutputDefinition] != nullptr) {
        g_env.Log("Accept transform: out %2i", *optOutputDefinition);
        return S_OK;
    }

    return VFW_E_TYPE_NOT_ACCEPTED;
}

auto CAviSynthFilter::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProperties) -> HRESULT {
    HRESULT hr;

    pProperties->cBuffers = max(g_env.GetOutputThreads() + 1, pProperties->cBuffers);

    BITMAPINFOHEADER *bih = Format::GetBitmapInfo(m_pOutput->CurrentMediaType());
    pProperties->cbBuffer = max(static_cast<long>(bih->biSizeImage), pProperties->cbBuffer);

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

    if (const std::optional<int> optInputDefiniton = Format::LookupMediaSubtype(m_pInput->CurrentMediaType().subtype)) {
        const int inputDefiniton = *optInputDefiniton;

        if (!m_pOutput->IsConnected()) {
            g_env.Log("Connected input pin with definition: %2i", inputDefiniton);
        } else if (const std::optional<int> optOutputDefinition = MediaTypeToDefinition(&m_pOutput->CurrentMediaType())) {
            const int outputDefinition = *optOutputDefinition;

            if (const std::optional<int> optCompatibleInput = FindCompatibleInputByOutput(outputDefinition)) {
                const int compatibleInput = *optCompatibleInput;

                if (inputDefiniton != compatibleInput) {
                    g_env.Log("Reconnect with types: old in %2i new in %2i out %2i", inputDefiniton, compatibleInput, outputDefinition);
                    CheckHr(ReconnectPin(m_pInput, _acceptableInputTypes[compatibleInput]));
                } else {
                    g_env.Log("Connected with types: in %2i out %2i", inputDefiniton, outputDefinition);
                }
            } else {
                g_env.Log("Unexpected lookup result for compatible definition");
                return E_UNEXPECTED;
            }
        } else {
            g_env.Log("Unexpected lookup result for output definition");
            return E_UNEXPECTED;
        }
    } else {
        g_env.Log("Unexpected lookup result for input definition");
        return E_UNEXPECTED;
    }

    return __super::CompleteConnect(direction, pReceivePin);
}

auto CAviSynthFilter::Receive(IMediaSample *pSample) -> HRESULT {
    HRESULT hr;
    AM_MEDIA_TYPE *pmtOut, *pmt;

    pSample->GetMediaType(&pmt);
    const bool inputFormatChanged = (pmt != nullptr && pmt->pbFormat != nullptr);

    if (inputFormatChanged || _reloadAvsSource) {
        StopStreaming();

        if (inputFormatChanged) {
            m_pInput->SetMediaType(reinterpret_cast<CMediaType *>(pmt));
        }

        frameHandler.Flush();
        ReloadAviSynthScript(m_pInput->CurrentMediaType());
        _reloadAvsSource = false;

        m_csReceive.Unlock();
        hr = UpdateOutputFormat(m_pInput->CurrentMediaType());
        m_csReceive.Lock();

        if (FAILED(hr)) {
            return AbortPlayback(hr);
        }

        hr = StartStreaming();
        if (FAILED(hr)) {
            return AbortPlayback(hr);
        }
    }

    IMediaSample *pOutSample;
    CheckHr(m_pOutput->GetDeliveryBuffer(&pOutSample, nullptr, nullptr, 0));
    pOutSample->GetMediaType(&pmtOut);
    pOutSample->Release();

    if (pmtOut != nullptr && pmtOut->pbFormat != nullptr) {
        StopStreaming();
        m_pOutput->SetMediaType(reinterpret_cast<CMediaType *>(pmtOut));

        HandleOutputFormatChange(pmtOut);
        _confirmNewOutputFormat = true;

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
        hr = frameHandler.AddInputSample(pSample);

        if (m_nWaitForKey) {
            m_nWaitForKey--;
        }
        if (m_nWaitForKey && pSample->IsSyncPoint() == S_OK) {
            m_nWaitForKey = 0;
        }

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

    return S_OK;
}

auto CAviSynthFilter::EndFlush() -> HRESULT {
    frameHandler.Flush();
    ReloadAviSynthScript(m_pInput->CurrentMediaType());

    return __super::EndFlush();
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetPages(__RPC__out CAUUID *pPages) -> HRESULT {
    CheckPointer(pPages, E_POINTER);

    InitAviSynth();

    pPages->pElems = static_cast<GUID *>(CoTaskMemAlloc(2 * sizeof(GUID)));
    if (pPages->pElems == nullptr) {
        return E_OUTOFMEMORY;
    }

    pPages->cElems = 1;
    pPages->pElems[0] = CLSID_AvsPropSettings;

    if (m_State != State_Stopped) {
        pPages->cElems += 1;
        pPages->pElems[1] = CLSID_AvsPropStatus;
    }

    return S_OK;
}

auto CAviSynthFilter::GetAvsVersionString() const -> const char * {
    return _avsVersionString == nullptr ? "unknown AviSynth version" : _avsVersionString;
}

auto CAviSynthFilter::GetInputFormat() const->Format::VideoFormat {
    return _inputFormat;
}

auto CAviSynthFilter::GetOutputFormat() const->Format::VideoFormat {
    return _outputFormat;
}

auto CAviSynthFilter::GetEffectiveAvsFile() const -> std::wstring {
    return _effectiveAvsFile;
}

auto CAviSynthFilter::GetSourceAvgFrameRate() const -> int {
    return _sourceAvgFrameRate;
}

auto CAviSynthFilter::ReloadAvsFile(const std::wstring &avsFile) -> void {
    _effectiveAvsFile = avsFile;
    _reloadAvsSource = true;
}

auto CAviSynthFilter::GetVideoSourcePath() const -> std::wstring {
    return _videoSourcePath;
}

auto CAviSynthFilter::GetVideoFilterNames() const -> std::vector<std::wstring> {
    return _videoFilterNames;
}

auto CAviSynthFilter::GetAvsState() const -> AvsState {
    if (m_State == State_Stopped || !_avsScriptClip) {
        return AvsState::Stopped;
    }

    if (!_avsError.empty()) {
        return AvsState::Error;
    }

    if (m_State == State_Running) {
        return AvsState::Running;
    }

    return AvsState::Paused;
}

auto CAviSynthFilter::GetAvsError() const -> std::optional<std::string> {
    if (_avsError.empty()) {
        return std::nullopt;
    }

    return _avsError;
}

/**
 * Check if the media type has valid VideoInfo * definition block.
 */
auto CAviSynthFilter::MediaTypeToDefinition(const AM_MEDIA_TYPE *mediaType) -> std::optional<int> {
    if (mediaType->majortype != MEDIATYPE_Video) {
        return std::nullopt;
    }

    if (FAILED(CheckVideoInfoType(mediaType)) && FAILED(CheckVideoInfo2Type(mediaType))) {
        return std::nullopt;
    }

    return Format::LookupMediaSubtype(mediaType->subtype);
}

auto CAviSynthFilter::GetInputDefinition(const AM_MEDIA_TYPE *mediaType) -> std::optional<int> {
    if (const std::optional<int> optInputDefinition = MediaTypeToDefinition(mediaType)) {
        if ((g_env.GetInputFormatBits() & (1 << *optInputDefinition)) != 0) {
            return optInputDefinition;
        }

        g_env.Log("Reject input definition due to settings: %2i", *optInputDefinition);
    }

    return std::nullopt;
}

/**
 * Whenever the media type on the input pin is changed, we need to run the avs script against
 * the new type to generate corresponding output type. Then we need to update the downstream with it.
 *
 * returns S_OK if the avs script is reloaded due to format change.
 */
auto CAviSynthFilter::UpdateOutputFormat(const AM_MEDIA_TYPE &inputMediaType) -> HRESULT {
    HRESULT hr;

    _inputFormat = Format::GetVideoFormat(inputMediaType);

    g_env.Log("Update output format using input format: definition %i, width %5li, height %5li, codec %s",
        _inputFormat.definition, _inputFormat.bmi.biWidth, _inputFormat.bmi.biHeight, _inputFormat.GetCodecName().c_str());

    AM_MEDIA_TYPE *newOutputType = GenerateMediaType(Format::LookupAvsType(_avsScriptVideoInfo.pixel_type)[0], &inputMediaType);
    if (m_pOutput->GetConnected()->QueryAccept(newOutputType) != S_OK) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }
    CheckHr(m_pOutput->GetConnected()->ReceiveConnection(m_pOutput, newOutputType));
    DeleteMediaType(newOutputType);

    return S_OK;
}

/**
 * returns S_OK if the next media sample should carry the media type on the output pin.
 */
auto CAviSynthFilter::HandleOutputFormatChange(const AM_MEDIA_TYPE *pmtOut) -> HRESULT {
    _outputFormat = Format::GetVideoFormat(*pmtOut);

    g_env.Log("New output format: definition %i, width %5li, height %5li, codec %s",
        _outputFormat.definition, _outputFormat.bmi.biWidth, _outputFormat.bmi.biHeight, _outputFormat.GetCodecName().c_str());

    return S_OK;
}

auto CAviSynthFilter::TraverseFiltersInGraph() -> void {
    _videoSourcePath.clear();
    _videoFilterNames.clear();

    ATL::CComPtr<IEnumFilters> enumFilters;
    if (FAILED(m_pGraph->EnumFilters(&enumFilters))) {
        return;
    }

    IBaseFilter *currFilter;
    while (true) {
        const HRESULT hr = enumFilters->Next(1, &currFilter, nullptr);

        if (hr == S_OK) {
            ATL::CComQIPtr<IFileSourceFilter> source(currFilter);
            if (source != nullptr) {
                LPOLESTR filename;
                if (SUCCEEDED(source->GetCurFile(&filename, nullptr))) {
                    _videoSourcePath = std::wstring(filename);
                }
            }

            currFilter->Release();
        } else if (hr == VFW_E_ENUM_OUT_OF_SYNC) {
            enumFilters->Reset();
        } else {
            break;
        }
    }

    // DO NOT call currFilter->Release() any more for the rest of the function

    if (!_videoSourcePath.empty()) {
        return;
    }

    while (true) {
        FILTER_INFO filterInfo;
        if (SUCCEEDED(currFilter->QueryFilterInfo(&filterInfo))) {
            QueryFilterInfoReleaseGraph(filterInfo);
            _videoFilterNames.push_back(filterInfo.achName);
            g_env.Log("Visiting filter: %S", filterInfo.achName);
        }

        const std::optional<IPin *> optOutputPin = FindFirstVideoOutputPin(currFilter);
        if (!optOutputPin) {
            break;
        }
        ATL::CComPtr<IPin> outputPin = *optOutputPin;

        ATL::CComPtr<IPin> nextInputPin;
        if (FAILED(outputPin->ConnectedTo(&nextInputPin))) {
            break;
        }

        PIN_INFO pinInfo;
        if (SUCCEEDED(nextInputPin->QueryPinInfo(&pinInfo))) {
            QueryPinInfoReleaseFilter(pinInfo);
            currFilter = pinInfo.pFilter;
        }
    }
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
        newBmi = &newVih2->bmiHeader;

        // generate new DAR if the new SAR differs from the old one
        // because AviSynth does not tell us anything about DAR, use SAR as the DAR
        if (newBmi->biWidth * _avsScriptVideoInfo.height != _avsScriptVideoInfo.width * std::abs(newBmi->biHeight)) {
            const int gcd = std::gcd(_avsScriptVideoInfo.width, _avsScriptVideoInfo.height);
            newVih2->dwPictAspectRatioX = _avsScriptVideoInfo.width / gcd;
            newVih2->dwPictAspectRatioY = _avsScriptVideoInfo.height / gcd;
        }
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
    for (AM_MEDIA_TYPE *&type : _acceptableInputTypes) {
        if (type != nullptr) {
            DeleteMediaType(type);
            type = nullptr;
        }
    }

    for (AM_MEDIA_TYPE *&type : _acceptableOutputTypes) {
        if (type != nullptr) {
            DeleteMediaType(type);
            type = nullptr;
        }
    }

    _compatibleDefinitions.clear();
}

auto CAviSynthFilter::InitAviSynth() -> bool {
    if (_avsVersionString == nullptr) {
        try {
            _avsVersionString = g_env.GetAvsEnv()->Invoke("Eval", AVSValue("VersionString()")).AsString();
        } catch (AvisynthError) {
            return false;
        }
        g_env.Log("Filter version: %s", FILTER_VERSION_STRING);
        g_env.Log("AviSynth version: %s", GetAvsVersionString());

        _avsSourceClip = new SourceClip(frameHandler, _avsSourceVideoInfo);
        g_env.GetAvsEnv()->AddFunction("AvsFilterSource", "", Create_AvsFilterSource, _avsSourceClip);
        g_env.GetAvsEnv()->AddFunction("AvsFilterDisconnect", "", Create_AvsFilterDisconnect, nullptr);
    }

    return true;
}

/**
 * Create new AviSynth script clip with specified media type.
 */
auto CAviSynthFilter::ReloadAviSynthScript(const AM_MEDIA_TYPE &mediaType) -> bool {
    g_env.Log("ReloadAviSynthScript");

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

    _avsError.clear();

    AVSValue invokeResult;

    if (!_effectiveAvsFile.empty()) {
        const std::string utf8File = ConvertWideToUtf8(_effectiveAvsFile);
        const AVSValue args[2] = { utf8File.c_str(), true };
        const char *const argNames[2] = { nullptr, "utf8" };

        try {
            invokeResult = g_env.GetAvsEnv()->Invoke("Import", AVSValue(args, 2), argNames);
        } catch (AvisynthError &err) {
            _avsError = err.msg;
        }
    }

    if (_avsError.empty()) {
        if (!invokeResult.Defined()) {
            if (m_State == State_Stopped && !_remoteControl) {
                return false;
            }

            invokeResult = _avsSourceClip;
        } else if (!invokeResult.IsClip()) {
            _avsError = "Error: Script does not return a clip.";
        }
    }

    if (!_avsError.empty()) {
        std::string errorScript = _avsError;
        ReplaceSubstring(errorScript, "\"", "\'");
        ReplaceSubstring(errorScript, "\n", "\\n");
        errorScript.insert(0, "return Subtitle(AvsFilterSource(), \"");
        errorScript.append("\", lsp=0, utf8=true)");

        invokeResult = g_env.GetAvsEnv()->Invoke("Eval", AVSValue(errorScript.c_str()));
    }

    _avsScriptClip = invokeResult.AsClip();
    _avsScriptVideoInfo = _avsScriptClip->GetVideoInfo();
    _sourceAvgFrameRate = static_cast<int>(llMulDiv(_avsSourceVideoInfo.fps_numerator, FRAME_RATE_SCALE_FACTOR, _avsScriptVideoInfo.fps_denominator, 0));
    _sourceAvgFrameTime = llMulDiv(_avsSourceVideoInfo.fps_denominator, UNITS, _avsSourceVideoInfo.fps_numerator, 0);
    _scriptAvgFrameTime = llMulDiv(_avsScriptVideoInfo.fps_denominator, UNITS, _avsScriptVideoInfo.fps_numerator, 0);

    return true;
}

auto CAviSynthFilter::StopAviSynthScript() -> void {
    if (_avsScriptClip != nullptr) {
        _avsScriptClip = nullptr;
    }
}

auto CAviSynthFilter::IsInputUniqueByAvsType(int inputDefinition) const -> bool {
    for (size_t d = 0; d < _acceptableInputTypes.size(); ++d) {
        if (_acceptableInputTypes[d] != nullptr && Format::DEFINITIONS[d].avsType == Format::DEFINITIONS[inputDefinition].avsType) {
            return false;
        }
    }

    return true;
}

auto CAviSynthFilter::FindCompatibleInputByOutput(int outputDefinition) const -> std::optional<int> {
    for (const auto &[input, output] : _compatibleDefinitions) {
        if (output == outputDefinition) {
            return input;
        }
    }
    return std::nullopt;
}

}
