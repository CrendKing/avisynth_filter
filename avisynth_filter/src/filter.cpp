#include "pch.h"
#include "filter.h"
#include "api.h"
#include "constants.h"
#include "input_pin.h"
#include "logging.h"
#include "source_clip.h"
#include "util.h"


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
    , _inputFormatBits(0)
    , _inputThreads(DEFAULT_INPUT_SAMPLE_WORKER_THREAD_COUNT)
    , _outputThreads(DEFAULT_OUTPUT_SAMPLE_WORKER_THREAD_COUNT)
    , _remoteControl(nullptr)
    , _settingsLoader(LoadSettings())
    , _frameHandler(*this)
    , _acceptableInputTypes(Format::DEFINITIONS.size())
    , _acceptableOutputTypes(Format::DEFINITIONS.size())
    , _avsEnv(nullptr)
    , _avsEnvAtLeastV8(false)
    , _avsSourceClip(nullptr)
    , _avsScriptClip(nullptr)
    , _avsVersionString(nullptr)
    , _reloadAvsSource(false)
    , _avsSourceVideoInfo()
    , _avsScriptVideoInfo()
    , _sourceAvgFrameRate(0)
    , _sourceAvgFrameTime(0)
    , _frameTimeScaling(0)
    , _inputFormat()
    , _outputFormat()
    , _confirmNewOutputFormat(false) {
    Log("CAviSynthFilter::CAviSynthFilter()");
}

CAviSynthFilter::~CAviSynthFilter() {
    if (_remoteControl != nullptr) {
        delete _remoteControl;
        _remoteControl = nullptr;
    }

    DeleteAviSynth();
    DeletePinTypes();
}

auto STDMETHODCALLTYPE CAviSynthFilter::NonDelegatingQueryInterface(REFIID riid, __deref_out void **ppv) -> HRESULT {
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

    if (!CreateAviSynth()) {
        return E_FAIL;
    }

    if (direction == PINDIR_INPUT) {
        IEnumMediaTypes *enumTypes;
        CheckHr(pPin->EnumMediaTypes(&enumTypes));

        AM_MEDIA_TYPE *nextType;
        while (true) {
            hr = enumTypes->Next(1, &nextType, nullptr);
            if (hr == S_OK) {
                const auto inputDefinition = GetInputDefinition(nextType);

                // for each group of formats with the same avs type, only add the first one that upstream supports.
                // this one will be the preferred media type for potential pin reconnection
                if (inputDefinition && IsInputUniqueByAvsType(*inputDefinition)) {
                    // invoke AviSynth script with each supported input definition, and observe the output avs type
                    if (!ReloadAviSynthScript(*nextType)) {
                        Log("Disconnect due to AvsFilterDisconnect()");

                        DeleteMediaType(nextType);
                        ret = VFW_E_NO_TYPES;
                        break;
                    }

                    _acceptableInputTypes[*inputDefinition] = nextType;
                    Log("Add acceptable input definition: %2i", *inputDefinition);

                    // all media types that share the same avs type are acceptable for output pin connection
                    for (int outputDefinition : Format::LookupAvsType(_avsScriptVideoInfo.pixel_type)) {
                        if (_acceptableOutputTypes[outputDefinition] == nullptr) {
                            AM_MEDIA_TYPE *outputType = GenerateMediaType(outputDefinition, nextType);
                            _acceptableOutputTypes[outputDefinition] = outputType;
                            Log("Add acceptable output definition: %2i", outputDefinition);

                            _compatibleDefinitions.emplace_back(DefinitionPair { *inputDefinition, outputDefinition });
                            Log("Add compatible definitions: input %2i output %2i", *inputDefinition, outputDefinition);
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
    const auto definition = GetInputDefinition(mtIn);

    if (!definition || _acceptableInputTypes[*definition] == nullptr) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    Log("Accept input definition: %2i", *definition);

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
    *pMediaType = *_acceptableOutputTypes[definition];

    Log("Offer output definition: %2i", definition);

    return S_OK;
}

auto CAviSynthFilter::CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut) -> HRESULT {
    const auto outputDefinition = MediaTypeToDefinition(mtOut);

    if (!outputDefinition || _acceptableOutputTypes[*outputDefinition] == nullptr) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    Log("Accept transform: out %2i", *outputDefinition);

    return S_OK;
}

auto CAviSynthFilter::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProperties) -> HRESULT {
    HRESULT hr;

    pProperties->cBuffers = max(_outputThreads, pProperties->cBuffers);

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

    if (const auto inputDefiniton = Format::LookupMediaSubtype(m_pInput->CurrentMediaType().subtype)) {
        if (!m_pOutput->IsConnected()) {
            Log("Connected input pin with definition: %2i", *inputDefiniton);
        } else if (const auto outputDefinition = MediaTypeToDefinition(&m_pOutput->CurrentMediaType())) {
            if (const auto compatibleInput = FindCompatibleInputByOutput(*outputDefinition)) {
                if (*inputDefiniton != *compatibleInput) {
                    Log("Reconnect with types: old in %2i new in %2i out %2i", *inputDefiniton, *compatibleInput, *outputDefinition);
                    CheckHr(ReconnectPin(m_pInput, _acceptableInputTypes[*compatibleInput]));
                } else {
                    Log("Connected with types: in %2i out %2i", *inputDefiniton, *outputDefinition);
                }
            } else {
                Log("Unexpected lookup result for compatible definition");
                return E_UNEXPECTED;
            }
        } else {
            Log("Unexpected lookup result for output definition");
            return E_UNEXPECTED;
        }
    } else {
        Log("Unexpected lookup result for input definition");
        return E_UNEXPECTED;
    }

    return __super::CompleteConnect(direction, pReceivePin);
}

auto CAviSynthFilter::Receive(IMediaSample *pSample) -> HRESULT {
    HRESULT hr;
    AM_MEDIA_TYPE *pmtOut, *pmt;
    IMediaSample *pOutSample;

    pSample->GetMediaType(&pmt);
    const bool inputFormatChanged = (pmt != nullptr && pmt->pbFormat != nullptr);

    if (inputFormatChanged || _reloadAvsSource) {
        StopStreaming();

        if (inputFormatChanged) {
            m_pInput->SetMediaType(reinterpret_cast<CMediaType *>(pmt));
        }

        _frameHandler.BeginFlush();
        _frameHandler.EndFlush();
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
        _frameHandler.AddInputSample(pSample);

        if (m_nWaitForKey) {
            m_nWaitForKey--;
        }
        if (m_nWaitForKey && pSample->IsSyncPoint() == S_OK) {
            m_nWaitForKey = 0;
        }

        if (m_nWaitForKey && !m_bQualityChanged) {
            m_bQualityChanged = TRUE;
            NotifyEvent(EC_QUALITY_CHANGE, 0, 0);
        }
    }

    return S_OK;
}

auto CAviSynthFilter::BeginFlush() -> HRESULT {
    _frameHandler.BeginFlush();

    return __super::BeginFlush();
}

auto CAviSynthFilter::EndFlush() -> HRESULT {
    _frameHandler.EndFlush();
    _reloadAvsSource = true;

    return __super::EndFlush();
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetPages(__RPC__out CAUUID *pPages) -> HRESULT {
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
    _registry.WriteString(REGISTRY_VALUE_NAME_AVS_FILE, _prefAvsFile);
    _registry.WriteNumber(REGISTRY_VALUE_NAME_FORMATS, _inputFormatBits);
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetPrefAvsFile() const -> std::wstring {
    return _prefAvsFile;
}

auto STDMETHODCALLTYPE CAviSynthFilter::SetPrefAvsFile(const std::wstring &avsFile) -> void {
    _prefAvsFile = avsFile;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetEffectiveAvsFile() const -> std::wstring {
    return _effectiveAvsFile;
}

auto STDMETHODCALLTYPE CAviSynthFilter::SetEffectiveAvsFile(const std::wstring &avsFile) -> void {
    _effectiveAvsFile = avsFile;
}

auto STDMETHODCALLTYPE CAviSynthFilter::ReloadAvsSource() -> void {
    _reloadAvsSource = true;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetInputFormats() const -> DWORD {
    return _inputFormatBits;
}

auto STDMETHODCALLTYPE CAviSynthFilter::SetInputFormats(DWORD formatBits) -> void {
    _inputFormatBits = formatBits;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetAvsVersionString() -> const char * {
    if (_avsVersionString == nullptr) {
        return "unknown AviSynth version";
    } else {
        return _avsVersionString;
    }
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetInputBufferSize() const -> int {
    return _frameHandler.GetInputBufferSize();
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetOutputBufferSize() const -> int {
    return _frameHandler.GetOutputBufferSize();
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetSourceSampleNumber() const -> int {
    return _frameHandler.GetSourceFrameNb();
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetOutputSampleNumber() const -> int {
    return _frameHandler.GetOutputFrameNb();
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetDeliveryFrameNumber() const -> int {
    return _frameHandler.GetDeliveryFrameNb();
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetCurrentInputFrameRate() const -> int {
    return _frameHandler.GetCurrentInputFrameRate();
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetCurrentOutputFrameRate() const -> int {
    return _frameHandler.GetCurrentOutputFrameRate();
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetInputWorkerThreadCount() const -> int {
    return _frameHandler.GetInputWorkerThreadCount();
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetOutputWorkerThreadCount() const -> int {
    return _frameHandler.GetOutputWorkerThreadCount();
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetVideoSourcePath() const -> std::wstring {
    return _videoSourcePath;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetInputMediaInfo() const -> Format::VideoFormat {
    return _inputFormat;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetVideoFilterNames() const -> std::vector<std::wstring> {
    return _videoFilterNames;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetSourceAvgFrameRate() const -> int {
    return _sourceAvgFrameRate;
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetAvsState() const -> AvsState {
    if (!_avsScriptClip) {
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

auto STDMETHODCALLTYPE CAviSynthFilter::GetAvsError() const -> std::optional<std::string> {
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

/**
 * Whenever the media type on the input pin is changed, we need to run the avs script against
 * the new type to generate corresponding output type. Then we need to update the downstream with it.
 *
 * returns S_OK if the avs script is reloaded due to format change.
 */
auto CAviSynthFilter::UpdateOutputFormat(const AM_MEDIA_TYPE &inputMediaType) -> HRESULT {
    HRESULT hr;

    _inputFormat = Format::GetVideoFormat(inputMediaType);

    Log("Update output format using input format: definition %i, width %5i, height %5i, codec %s",
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

    Log("New output format: definition %i, width %5i, height %5i, codec %s",
        _outputFormat.definition, _outputFormat.bmi.biWidth, _outputFormat.bmi.biHeight, _outputFormat.GetCodecName().c_str());

    return S_OK;
}

auto CAviSynthFilter::TraverseFiltersInGraph() -> void {
    _videoSourcePath.clear();
    _videoFilterNames.clear();

    IEnumFilters *filters;
    if (FAILED(m_pGraph->EnumFilters(&filters))) {
        return;
    }

    IBaseFilter *filter;
    while (true) {
        const HRESULT hr = filters->Next(1, &filter, nullptr);
        if (hr == S_OK) {
            IFileSourceFilter *source;
            if (!FAILED(filter->QueryInterface(&source))) {
                LPOLESTR filename;
                if (SUCCEEDED(source->GetCurFile(&filename, nullptr))) {
                    Log("Source path: %S", filename);
                    _videoSourcePath = std::wstring(filename);
                }
                source->Release();
                break;
            }

            filter->Release();
        } else if (hr == VFW_E_ENUM_OUT_OF_SYNC) {
            filters->Reset();
        } else {
            break;
        }
    }
    filters->Release();

    if (_videoSourcePath.empty()) {
        return;
    }

    while (true) {
        FILTER_INFO filterInfo;
        if (SUCCEEDED(filter->QueryFilterInfo(&filterInfo))) {
            QueryFilterInfoReleaseGraph(filterInfo);

            _videoFilterNames.push_back(filterInfo.achName);
            Log("Visiting filter: %S", filterInfo.achName);
        }

        IPin *outputPin = FindFirstVideoOutputPin(filter);
        filter->Release();
        if (outputPin == nullptr) {
            break;
        }

        IPin *nextInputPin;
        if (FAILED(outputPin->ConnectedTo(&nextInputPin))) {
            outputPin->Release();
            break;
        }

        PIN_INFO pinInfo;
        if (SUCCEEDED(nextInputPin->QueryPinInfo(&pinInfo))) {
            filter = pinInfo.pFilter;
        }

        nextInputPin->Release();
        outputPin->Release();
    }
}

auto CAviSynthFilter::LoadSettings() -> void * {
    const std::wstring avsFile = _registry.ReadString(REGISTRY_VALUE_NAME_AVS_FILE);
    SetPrefAvsFile(avsFile);
    SetEffectiveAvsFile(avsFile);

    _inputFormatBits = _registry.ReadNumber(REGISTRY_VALUE_NAME_FORMATS, (1 << Format::DEFINITIONS.size()) - 1);
    _inputThreads = _registry.ReadNumber(REGISTRY_VALUE_NAME_INPUT_THREADS, DEFAULT_INPUT_SAMPLE_WORKER_THREAD_COUNT);
    _outputThreads = _registry.ReadNumber(REGISTRY_VALUE_NAME_OUTPUT_THREADS, DEFAULT_OUTPUT_SAMPLE_WORKER_THREAD_COUNT);

    if (_registry.ReadNumber(REGISTRY_VALUE_NAME_REMOTE_CONTROL, 0) != 0) {
        _remoteControl = new RemoteControl(this, this);
    }

    Log("Configured script file: %S", avsFile.c_str());
    Log("Configured input formats: %i", _inputFormatBits);
    Log("Configured input threads: %i", _inputThreads);
    Log("Configured output threads: %i", _outputThreads);

    return nullptr;
}

auto CAviSynthFilter::GetInputDefinition(const AM_MEDIA_TYPE *mediaType) const -> std::optional<int> {
    if (const auto inputDefinition = MediaTypeToDefinition(mediaType)) {
        if ((_inputFormatBits & (1 << *inputDefinition)) != 0) {
            return inputDefinition;
        }

        Log("Reject input definition due to settings: %2i", *inputDefinition);
    }

    return std::nullopt;
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

auto CAviSynthFilter::CreateAviSynth() -> bool {
    if (_avsEnv == nullptr) {
        _avsEnv = CreateScriptEnvironment2();
        if (_avsEnv == nullptr) {
            Log("CreateScriptEnvironment2 FAILED!");
            return false;
        }

        try {
            _avsEnv->CheckVersion(8);
            _avsEnvAtLeastV8 = true;
        } catch (const AvisynthError &) {
            _avsEnvAtLeastV8 = false;
        }

        _avsVersionString = _avsEnv->Invoke("Eval", AVSValue("VersionString()")).AsString();
        _avsSourceClip = new SourceClip(_frameHandler, _avsSourceVideoInfo);
        _avsEnv->AddFunction("AvsFilterSource", "", Create_AvsFilterSource, _avsSourceClip);
        _avsEnv->AddFunction("AvsFilterDisconnect", "", Create_AvsFilterDisconnect, nullptr);
    }

    return true;
}

/**
 * Create new AviSynth script clip with specified media type.
 */
auto CAviSynthFilter::ReloadAviSynthScript(const AM_MEDIA_TYPE &mediaType) -> bool {
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
    if (_effectiveAvsFile.empty()) {
        if (_remoteControl == nullptr) {
            return false;
        }

        invokeResult = _avsSourceClip;
    } else {
        const std::string utf8File = ConvertWideToUtf8(_effectiveAvsFile);
        const AVSValue args[2] = { utf8File.c_str(), true };
        const char *const argNames[2] = { nullptr, "utf8" };

        try {
            if (!_avsEnvAtLeastV8 && _avsScriptClip != nullptr) {
                // delete the associated prefetcher for avs+ < v8, which does not support multiple prefetchers
                _avsScriptClip = nullptr;
            }
            
            invokeResult = _avsEnv->Invoke("Import", AVSValue(args, 2), argNames);

            if (!invokeResult.Defined() && m_State == State_Stopped && _remoteControl == nullptr) {
                return false;
            }
            if (!invokeResult.IsClip()) {
                _avsError = "Error: Script does not return a clip.";
            }
        } catch (AvisynthError &err) {
            _avsError = err.msg;
        }
    }

    if (!_avsError.empty()) {
        std::string errorScript = _avsError;
        ReplaceSubstring(errorScript, "\"", "\'");
        ReplaceSubstring(errorScript, "\n", "\\n");
        errorScript.insert(0, "return Subtitle(AvsFilterSource(), \"");
        errorScript.append("\", lsp=0, utf8=true)");

        invokeResult = _avsEnv->Invoke("Eval", AVSValue(errorScript.c_str()));
    }

    _avsScriptClip = invokeResult.AsClip();
    _avsScriptVideoInfo = _avsScriptClip->GetVideoInfo();
    _sourceAvgFrameRate = static_cast<int>(llMulDiv(_avsSourceVideoInfo.fps_numerator, FRAME_RATE_SCALE_FACTOR, _avsScriptVideoInfo.fps_denominator, 0));
    _sourceAvgFrameTime = llMulDiv(_avsSourceVideoInfo.fps_denominator, UNITS, _avsSourceVideoInfo.fps_numerator, 0);
    _frameTimeScaling = static_cast<double>(llMulDiv(_avsSourceVideoInfo.fps_numerator, _avsScriptVideoInfo.fps_denominator, _avsSourceVideoInfo.fps_denominator, 0)) / _avsScriptVideoInfo.fps_numerator;

    return true;
}

auto CAviSynthFilter::DeleteAviSynth() -> void {
    if (_avsScriptClip != nullptr) {
        _avsScriptClip = nullptr;
    }

    if (_avsEnv != nullptr) {
        _avsEnv->DeleteScriptEnvironment();
        _avsEnv = nullptr;
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
