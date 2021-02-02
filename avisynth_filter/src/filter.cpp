// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "filter.h"
#include "api.h"
#include "avs_handler.h"
#include "constants.h"
#include "environment.h"
#include "input_pin.h"
#include "version.h"


namespace AvsFilter {

#define CheckHr(expr) { hr = (expr); if (FAILED(hr)) { return hr; } }

CAviSynthFilter::CAviSynthFilter(LPUNKNOWN pUnk, HRESULT *phr)
    : CVideoTransformFilter(NAME(FILTER_NAME_FULL), pUnk, CLSID_AviSynthFilter)
    , frameHandler(*this)
    , _disconnectFilter(false)
    , _mediaTypeReconnectionWatermark(0)
    , _inputFormat()
    , _outputFormat()
    , _sendOutputFormatInNextSample(false)
	, _reloadAvsSource(false) {
    g_env.Log("CAviSynthFilter(): %p", this);

    if (g_env.IsRemoteControlEnabled()) {
        _remoteControl.emplace(*this);
    }
}

CAviSynthFilter::~CAviSynthFilter() {
    g_env.Log("Destroy CAviSynthFilter: %p", this);
    g_avs.Release();
}

auto STDMETHODCALLTYPE CAviSynthFilter::NonDelegatingQueryInterface(REFIID riid, __deref_out void **ppv) -> HRESULT {
    CheckPointer(ppv, E_POINTER);

    if (riid == IID_IAvsFilter) {
        return GetInterface(reinterpret_cast<IBaseFilter *>(this), ppv);
    }
    if (riid == IID_ISpecifyPropertyPages) {
        return GetInterface(static_cast<ISpecifyPropertyPages *>(this), ppv);
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
    HRESULT hr;

    if (_disconnectFilter) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    if (direction == PINDIR_INPUT) {
        ATL::CComPtr<IEnumMediaTypes> enumTypes;
        CheckHr(pPin->EnumMediaTypes(&enumTypes));

        AM_MEDIA_TYPE *nextType;
        while (true) {
            hr = enumTypes->Next(1, &nextType, nullptr);
            if (hr == S_OK) {
                const UniqueMediaTypePtr nextTypePtr(nextType);

                const std::optional<std::wstring> optInputFormatName = GetInputFormatName(nextType);
                if (optInputFormatName && std::ranges::find(_compatibleMediaTypes, *static_cast<CMediaType *>(nextType), &MediaTypePair::input) == _compatibleMediaTypes.cend()) {
                    // invoke AviSynth script with each supported input definition, and observe the output avs type
                    if (!g_avs->ReloadScript(*nextType, _remoteControl.has_value())) {
                        g_env.Log("Disconnect due to AvsFilterDisconnect()");
                        _disconnectFilter = true;
                        return VFW_E_TYPE_NOT_ACCEPTED;
                    }

                    // all media types that share the same avs type are acceptable for output pin connection
                    for (const std::wstring &outputFormatName: Format::LookupAvsType(g_avs->GetScriptPixelType())) {
                        _compatibleMediaTypes.emplace_back(*nextType, g_avs->GenerateMediaType(outputFormatName, nextType));
                        g_env.Log("Add compatible definitions: input %S output %S", optInputFormatName->c_str(), outputFormatName.c_str());
                    }
                }
            } else if (hr == VFW_E_ENUM_OUT_OF_SYNC) {
                CheckHr(enumTypes->Reset());
            } else {
                break;
            }
        }
    }

    return S_OK;
}

auto CAviSynthFilter::CheckInputType(const CMediaType *mtIn) -> HRESULT {
    bool isInputCompatible = false;

    if (const std::optional<std::wstring> optInputFormatName = MediaTypeToFormatName(mtIn)) {
        isInputCompatible = std::ranges::any_of(_compatibleMediaTypes,
                                               [&optInputFormatName](const std::optional<std::wstring> &optCompatibleFormatName) -> bool { return *optInputFormatName == optCompatibleFormatName; },
                                               [](const MediaTypePair &pair) -> std::optional<std::wstring> { return MediaTypeToFormatName(&pair.input); });
    }

    if (!isInputCompatible) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    if (m_pOutput->IsConnected()) {
        const CMediaType newOutputType = g_avs->GenerateMediaType(Format::LookupAvsType(g_avs->GetScriptPixelType())[0], mtIn);
        if (m_pOutput->GetConnected()->QueryAccept(&newOutputType) != S_OK) {
            return VFW_E_TYPE_NOT_ACCEPTED;
        }
    }

    return S_OK;
}

auto CAviSynthFilter::GetMediaType(int iPosition, CMediaType *pMediaType) -> HRESULT {
    if (iPosition < 0) {
        return E_INVALIDARG;
    }

    if (!m_pInput->IsConnected()) {
        return E_UNEXPECTED;
    }

    if (iPosition >= static_cast<int>(_compatibleMediaTypes.size())) {
        return VFW_S_NO_MORE_ITEMS;
    }

    *pMediaType = _compatibleMediaTypes[iPosition].output;

    return S_OK;
}

auto CAviSynthFilter::CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut) -> HRESULT {
    if (const std::optional<std::wstring> optOutputFormatName = MediaTypeToFormatName(mtOut)) {
        const auto &iter = std::ranges::find_if(_compatibleMediaTypes,
                                                [&optOutputFormatName](const std::optional<std::wstring> &optCompatibleFormatName) -> bool { return *optOutputFormatName == optCompatibleFormatName; },
                                                [](const MediaTypePair &pair) -> std::optional<std::wstring> { return MediaTypeToFormatName(&pair.output); });
        if (iter != _compatibleMediaTypes.cend()) {
            g_env.Log("Accept transform: output %S Offered input: %S Compatible input: %S",
                      optOutputFormatName->c_str(), MediaTypeToFormatName(mtIn)->c_str(), MediaTypeToFormatName(&iter->input)->c_str());
            return S_OK;
        }
    }

    return VFW_E_TYPE_NOT_ACCEPTED;
}

auto CAviSynthFilter::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProperties) -> HRESULT {
    HRESULT hr;

    pProperties->cBuffers = max(g_env.GetOutputThreads() + 1, pProperties->cBuffers);

    BITMAPINFOHEADER *bmi = Format::GetBitmapInfo(m_pOutput->CurrentMediaType());
    pProperties->cbBuffer = max(static_cast<long>(bmi->biSizeImage + OUTPUT_MEDIA_SAMPLE_BUFFER_PADDING), pProperties->cbBuffer);

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
     * Suppose the upstream's output pin supports N1 media types (a set). The AviSynth script could convert those N1 input types to N2 output types.
     * The downstream's input pin supports N3 media types.
     *
     * The goal is to find a pair of input/output types where
     *   1) the output type is in the intersection of N2 and N3.
     *   2) the input type has a N1 -> N2 mapping to the output type.
     * There could be multiple such pairs exist. It is up to the downstream to choose which to use.
     *
     * The problem is that the graph manager may not query all N1 media types during CheckInputType(). It may stop querying once found a valid transform
     * input/output pair, where the output media type is not acceptable with the downstream.
     *
     * The solution is to enumerate all N1 media types from upstream, generate the whole N2 by calling AvsHandler::GenerateMediaType(). During GetMediaType()
     * and CheckTransform(), we offer and accept any output media type that is in N2. This allows downstream to choose the best media type it wants to use.
     *
     * Once both input and output pins are connected, we check if the pins' media types are valid transform. If yes, we are lucky and the pin connection completes.
     * If not, we keep the connection to the downstream, reverse lookup the compatible input media type and reconnect input pin with that.
     * Because the reconnect media type is selected from upstream's enumerated media type, the connection should always succeed at the second time.
     *
     * Since there could be multiple compatible input/output pair, if first input media type fails to reconnect, we raise water mark and try the next candidate
     * input media types, until either a successful reconnection happens, or we exhaust all candidates.
     */

    HRESULT hr;

    if (m_pInput->IsConnected() && m_pOutput->IsConnected()) {
        const std::optional<std::wstring> optConnectionInputFormatName = MediaTypeToFormatName(&m_pInput->CurrentMediaType());
        const std::optional<std::wstring> optConnectionOutputFormatName = MediaTypeToFormatName(&m_pOutput->CurrentMediaType());
        if (!optConnectionInputFormatName || !optConnectionOutputFormatName) {
            g_env.Log("Unexpected input or output definition");
            return E_UNEXPECTED;
        }

        bool isMediaTypesCompatible = false;
        int mediaTypeReconnectionIndex = 0;
        const CMediaType *reconnectInputMediaType = nullptr;

        for (const auto &[compatibleInput, compatibleOutput] : _compatibleMediaTypes) {
            if (*optConnectionOutputFormatName == MediaTypeToFormatName(&compatibleOutput)) {
                if (*optConnectionInputFormatName == MediaTypeToFormatName(&compatibleInput)) {
                    g_env.Log("Connected with types: in %S out %S", optConnectionInputFormatName->c_str(), optConnectionOutputFormatName->c_str());
                    isMediaTypesCompatible = true;
                    break;
                }

                if (mediaTypeReconnectionIndex >= _mediaTypeReconnectionWatermark) {
                    reconnectInputMediaType = &compatibleInput;
                    _mediaTypeReconnectionWatermark += 1;
                    break;
                }

                mediaTypeReconnectionIndex += 1;
            }
        }

        if (!isMediaTypesCompatible) {
            if (reconnectInputMediaType == nullptr) {
                g_env.Log("Failed to reconnect with any of the %i candidate input media types", _mediaTypeReconnectionWatermark);
                return E_UNEXPECTED;
            }

            CheckHr(ReconnectPin(m_pInput, reconnectInputMediaType));
        }
    }

    return __super::CompleteConnect(direction, pReceivePin);
}

auto CAviSynthFilter::Receive(IMediaSample *pSample) -> HRESULT {
    HRESULT hr;

    AM_MEDIA_TYPE *pmtIn;
    pSample->GetMediaType(&pmtIn);
    const UniqueMediaTypePtr pmtInPtr(pmtIn);

    const bool inputFormatChanged = (pmtIn != nullptr && pmtIn->pbFormat != nullptr);

    if (inputFormatChanged || _reloadAvsSource) {
        StopStreaming();

        if (inputFormatChanged) {
            m_pInput->SetMediaType(static_cast<CMediaType *>(pmtIn));
        }

        frameHandler.BeginFlush();
        g_avs->ReloadScript(m_pInput->CurrentMediaType(), true);
        frameHandler.EndFlush();
        _reloadAvsSource = false;

        m_csReceive.Unlock();
        hr = UpdateOutputFormat(m_pInput->CurrentMediaType());
        m_csReceive.Lock();

        if (FAILED(hr)) {
            return AbortPlayback(hr);
        }

        StartStreaming();
    }

    IMediaSample *pOutSample;
    CheckHr(m_pOutput->GetDeliveryBuffer(&pOutSample, nullptr, nullptr, 0));

    AM_MEDIA_TYPE *pmtOut;
    pOutSample->GetMediaType(&pmtOut);
    const UniqueMediaTypePtr pmtOutPtr(pmtOut);

    pOutSample->Release();

    if (pmtOut != nullptr && pmtOut->pbFormat != nullptr) {
        StopStreaming();
        m_pOutput->SetMediaType(static_cast<CMediaType *>(pmtOut));

        HandleOutputFormatChange(*pmtOut);
        _sendOutputFormatInNextSample = true;

        StartStreaming();
        m_nWaitForKey = 30;
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
    if (IsActive()) {
        frameHandler.BeginFlush();
        g_avs->ReloadScript(m_pInput->CurrentMediaType(), true);
        frameHandler.EndFlush();
    }

    return __super::EndFlush();
}

auto STDMETHODCALLTYPE CAviSynthFilter::GetPages(__RPC__out CAUUID *pPages) -> HRESULT {
    CheckPointer(pPages, E_POINTER);

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

auto CAviSynthFilter::GetInputFormat() const->Format::VideoFormat {
    return _inputFormat;
}

auto CAviSynthFilter::GetOutputFormat() const->Format::VideoFormat {
    return _outputFormat;
}

auto CAviSynthFilter::ReloadAvsFile(const std::wstring &avsFile) -> void {
    g_avs->SetScriptFile(avsFile);
    _reloadAvsSource = true;
}

auto CAviSynthFilter::GetVideoSourcePath() const -> std::wstring {
    return _videoSourcePath;
}

auto CAviSynthFilter::GetVideoFilterNames() const -> std::vector<std::wstring> {
    return _videoFilterNames;
}

auto CAviSynthFilter::GetAvsState() const -> AvsState {
    if (g_avs->GetErrorString()) {
        return AvsState::Error;
    }

    if (m_State == State_Stopped || !g_avs->GetScriptClip()) {
        return AvsState::Stopped;
    }

    if (m_State == State_Running) {
        return AvsState::Running;
    }

    return AvsState::Paused;
}

/**
 * Check if the media type has valid VideoInfo * definition block.
 */
auto CAviSynthFilter::MediaTypeToFormatName(const AM_MEDIA_TYPE *mediaType) -> std::optional<std::wstring> {
    if (mediaType->majortype != MEDIATYPE_Video) {
        return std::nullopt;
    }

    if (FAILED(CheckVideoInfoType(mediaType)) && FAILED(CheckVideoInfo2Type(mediaType))) {
        return std::nullopt;
    }

    return Format::LookupMediaSubtype(mediaType->subtype);
}

auto CAviSynthFilter::GetInputFormatName(const AM_MEDIA_TYPE *mediaType) -> std::optional<std::wstring> {
    if (const std::optional<std::wstring> optInputFormatName = MediaTypeToFormatName(mediaType)) {
        if (g_env.IsInputFormatEnabled(*optInputFormatName)) {
            return optInputFormatName;
        }

        g_env.Log("Reject input definition due to settings: %S", optInputFormatName->c_str());
    }

    return std::nullopt;
}

auto CAviSynthFilter::FindFirstVideoOutputPin(IBaseFilter *pFilter) -> std::optional<IPin *> {
    ATL::CComPtr<IEnumPins> enumPins;
    if (FAILED(pFilter->EnumPins(&enumPins))) {
        return std::nullopt;
    }

    while (true) {
        ATL::CComPtr<IPin> currPin;
        if (enumPins->Next(1, &currPin, nullptr) != S_OK) {
            break;
        }

        PIN_DIRECTION dir;
        if (FAILED(currPin->QueryDirection(&dir))) {
            break;
        }

        if (dir == PINDIR_OUTPUT) {
            CMediaType mediaType;
            if (SUCCEEDED(currPin->ConnectionMediaType(&mediaType)) &&
                (*mediaType.Type() == MEDIATYPE_Video || *mediaType.Type() == MEDIATYPE_Stream)) {
                return currPin;
            }
        }
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
    g_env.Log("Upstream propose to change input format: name %S, width %5li, height %5li",
              _inputFormat.name.c_str(), _inputFormat.bmi.biWidth, _inputFormat.bmi.biHeight);

    const CMediaType newOutputType = g_avs->GenerateMediaType(Format::LookupAvsType(g_avs->GetScriptPixelType())[0], &inputMediaType);
    if (m_pOutput->GetConnected()->QueryAccept(&newOutputType) != S_OK) {
        g_env.Log("Downstream does not accept new output format");
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    // even though the new VideoFormat may seem the same as the old, some properties (e.g. VIDEOINFOHEADER2::dwControlFlags which controls HDR colorspace)
    // may have changed. it is safe to always send out the new media type
    const Format::VideoFormat newOutputFormat = Format::GetVideoFormat(newOutputType);
    g_env.Log("Downstream accepts new output format: name %S, width %5li, height %5li",
              newOutputFormat.name.c_str(), newOutputFormat.bmi.biWidth, newOutputFormat.bmi.biHeight);

    CheckHr(m_pOutput->GetConnected()->ReceiveConnection(m_pOutput, &newOutputType));

    return S_OK;
}

/**
 * returns S_OK if the next media sample should carry the media type on the output pin.
 */
auto CAviSynthFilter::HandleOutputFormatChange(const AM_MEDIA_TYPE &outputMediaType) -> HRESULT {
    _outputFormat = Format::GetVideoFormat(outputMediaType);

    g_env.Log("New output format: name %S, width %5li, height %5li",
              _outputFormat.name.c_str(), _outputFormat.bmi.biWidth, _outputFormat.bmi.biHeight);

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

            if (source != nullptr) {
                break;
            }
        } else if (hr == VFW_E_ENUM_OUT_OF_SYNC) {
            enumFilters->Reset();
        } else {
            break;
        }
    }

    while (true) {
        FILTER_INFO filterInfo;
        if (SUCCEEDED(currFilter->QueryFilterInfo(&filterInfo))) {
            QueryFilterInfoReleaseGraph(filterInfo);
            _videoFilterNames.emplace_back(filterInfo.achName);
            g_env.Log("Filter in graph: %S", filterInfo.achName);
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

}
