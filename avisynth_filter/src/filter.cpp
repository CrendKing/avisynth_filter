// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "filter.h"
#include "api.h"
#include "constants.h"
#include "environment.h"
#include "input_pin.h"
#include "version.h"


namespace AvsFilter {

#define CheckHr(expr) { hr = (expr); if (FAILED(hr)) { return hr; } }

CAviSynthFilter::CAviSynthFilter(LPUNKNOWN pUnk, HRESULT *phr)
    : CVideoTransformFilter(FILTER_NAME_FULL, pUnk, CLSID_AviSynthFilter)
    , frameHandler(*this)
    , _disconnectFilter(false)
    , _mediaTypeReconnectionWatermark(0)
    , _inputVideoFormat()
    , _outputVideoFormat() {
    g_env.Log(L"CAviSynthFilter(): %p", this);

    if (g_env.IsRemoteControlEnabled()) {
        _remoteControl.emplace(*this);
    }
}

CAviSynthFilter::~CAviSynthFilter() {
    g_env.Log(L"Destroy CAviSynthFilter: %p", this);

    // RemoteControl depends on AvsHandler. destroy in order
    _remoteControl.reset();
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

                if (const Format::PixelFormat *optInputPixelFormat = GetInputPixelFormat(nextType);
                    optInputPixelFormat && std::ranges::find(_compatibleMediaTypes, *static_cast<CMediaType *>(nextType), &MediaTypePair::input) == _compatibleMediaTypes.cend()) {
                    // invoke AviSynth script with each supported input pixel format, and observe the output avs type
                    if (!g_avs->ReloadScript(*nextType, _remoteControl.has_value())) {
                        g_env.Log(L"Disconnect due to AvsFilterDisconnect()");
                        _disconnectFilter = true;
                        return VFW_E_TYPE_NOT_ACCEPTED;
                    }

                    // all media types that share the same avs type are acceptable for output pin connection
                    for (const Format::PixelFormat &avsPixelFormat : Format::LookupAvsType(g_avs->GetScriptPixelType())) {
                        _compatibleMediaTypes.emplace_back(*nextType, g_avs->GenerateMediaType(avsPixelFormat, nextType));
                        g_env.Log(L"Add compatible formats: input %s output %s", optInputPixelFormat->name.c_str(), avsPixelFormat.name.c_str());
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
    if (const Format::PixelFormat *optInputPixelFormat = MediaTypeToPixelFormat(mtIn);
        optInputPixelFormat && std::ranges::any_of(_compatibleMediaTypes,
                                                   [optInputPixelFormat](const Format::PixelFormat *optCompatibleFormatName) -> bool { return optInputPixelFormat == optCompatibleFormatName; },
                                                   [](const MediaTypePair &pair) -> const Format::PixelFormat * { return MediaTypeToPixelFormat(&pair.input); })) {
        return S_OK;
    }

    return VFW_E_TYPE_NOT_ACCEPTED;
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
    g_env.Log(L"GetMediaType() offer media type %d with %s", iPosition, MediaTypeToPixelFormat(pMediaType)->name.c_str());

    return S_OK;
}

auto CAviSynthFilter::CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut) -> HRESULT {
    bool isInputTypeOk = mtIn == &m_pInput->CurrentMediaType();
    bool isOutputTypeOk = mtOut == &m_pOutput->CurrentMediaType();

    if (!isInputTypeOk) {
        g_avs->ReloadScript(*mtIn, true);
        isInputTypeOk = std::ranges::any_of(InputToOutputMediaType(mtIn), [this](const CMediaType &newOutputType) -> bool {
            return m_pOutput->GetConnected()->QueryAccept(&newOutputType) == S_OK;
        });

        g_env.Log(L"Downstream QueryAccept in CheckTransform(): result %i input %s", isInputTypeOk, MediaTypeToPixelFormat(mtIn)->name.c_str());
    }

    if (!isOutputTypeOk) {
        if (const Format::PixelFormat *optOutputPixelFormat = MediaTypeToPixelFormat(mtOut)) {
            const auto &iter = std::ranges::find_if(_compatibleMediaTypes,
                                                    [&optOutputPixelFormat](const Format::PixelFormat *optCompatiblePixelFormat) -> bool {
                                                        return optOutputPixelFormat == optCompatiblePixelFormat;
                                                    },
                                                    [](const MediaTypePair &pair) -> const Format::PixelFormat * { return MediaTypeToPixelFormat(&pair.output); });
            isOutputTypeOk = iter != _compatibleMediaTypes.cend();
            g_env.Log(L"CheckTransform() for output type: result: %i output %s offered input: %s compatible input: %s",
                      isOutputTypeOk, optOutputPixelFormat->name.c_str(), MediaTypeToPixelFormat(mtIn)->name.c_str(), MediaTypeToPixelFormat(&iter->input)->name.c_str());
        }
    }

    return isInputTypeOk && isOutputTypeOk ? S_OK : VFW_E_TYPE_NOT_ACCEPTED;
}

auto CAviSynthFilter::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProperties) -> HRESULT {
    HRESULT hr;

    pProperties->cBuffers = max(2, pProperties->cBuffers);

    BITMAPINFOHEADER *bmi = Format::GetBitmapInfo(m_pOutput->CurrentMediaType());
    pProperties->cbBuffer = max(static_cast<long>(bmi->biSizeImage + Format::OUTPUT_MEDIA_SAMPLE_BUFFER_PADDING), pProperties->cbBuffer);

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
        const Format::PixelFormat *optConnectionInputPixelFormat = MediaTypeToPixelFormat(&m_pInput->CurrentMediaType());
        const Format::PixelFormat *optConnectionOutputPixelFormat = MediaTypeToPixelFormat(&m_pOutput->CurrentMediaType());
        if (!optConnectionInputPixelFormat || !optConnectionOutputPixelFormat) {
            g_env.Log(L"Unexpected input or output format");
            return E_UNEXPECTED;
        }

        bool isMediaTypesCompatible = false;
        int mediaTypeReconnectionIndex = 0;
        const CMediaType *reconnectInputMediaType = nullptr;

        for (const auto &[compatibleInput, compatibleOutput] : _compatibleMediaTypes) {
            if (optConnectionOutputPixelFormat == MediaTypeToPixelFormat(&compatibleOutput)) {
                if (optConnectionInputPixelFormat == MediaTypeToPixelFormat(&compatibleInput)) {
                    g_env.Log(L"Connected with types: in %s out %s", optConnectionInputPixelFormat->name.c_str(), optConnectionOutputPixelFormat->name.c_str());
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
                g_env.Log(L"Failed to reconnect with any of the %i candidate input media types", _mediaTypeReconnectionWatermark);
                return E_UNEXPECTED;
            }

            CheckHr(ReconnectPin(m_pInput, reconnectInputMediaType));
        }
    }

    return __super::CompleteConnect(direction, pReceivePin);
}

auto CAviSynthFilter::Receive(IMediaSample *pSample) -> HRESULT {
    HRESULT hr;

    if (pSample->IsDiscontinuity() == S_OK) {
        m_nWaitForKey = 30;
    }

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
        frameHandler.Flush([this]() -> void {
            g_avs->ReloadScript(m_pInput->CurrentMediaType(), true);
        });
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

auto CAviSynthFilter::GetInputFormat() const -> Format::VideoFormat {
    return _inputVideoFormat;
}

auto CAviSynthFilter::GetOutputFormat() const -> Format::VideoFormat {
    return _outputVideoFormat;
}

auto CAviSynthFilter::ReloadAvsFile(const std::filesystem::path &avsPath) -> void {
    g_avs->SetScriptPath(avsPath);
    _reloadAvsSource = true;
}

auto CAviSynthFilter::GetVideoSourcePath() const -> const std::filesystem::path & {
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
 * Check if the media type has valid VideoInfo data.
 */
auto CAviSynthFilter::MediaTypeToPixelFormat(const AM_MEDIA_TYPE *mediaType) -> const Format::PixelFormat * {
    if (mediaType->majortype != MEDIATYPE_Video) {
        return nullptr;
    }

    if (FAILED(CheckVideoInfoType(mediaType)) && FAILED(CheckVideoInfo2Type(mediaType))) {
        return nullptr;
    }

    return Format::LookupMediaSubtype(mediaType->subtype);
}

auto CAviSynthFilter::GetInputPixelFormat(const AM_MEDIA_TYPE *mediaType) -> const Format::PixelFormat * {
    if (const Format::PixelFormat *optInputPixelFormat = MediaTypeToPixelFormat(mediaType)) {
        if (g_env.IsInputFormatEnabled(optInputPixelFormat->name)) {
            return optInputPixelFormat;
        }

        g_env.Log(L"Reject input format due to settings: %s", optInputPixelFormat->name.c_str());
    }

    return nullptr;
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

        if (CMediaType mediaType; dir == PINDIR_OUTPUT && SUCCEEDED(currPin->ConnectionMediaType(&mediaType)) &&
            (*mediaType.Type() == MEDIATYPE_Video || *mediaType.Type() == MEDIATYPE_Stream)) {
            return currPin;
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

    _inputVideoFormat = Format::GetVideoFormat(inputMediaType);
    g_env.Log(L"Upstream propose to change input format: name %s, width %5li, height %5li",
              _inputVideoFormat.pixelFormat->name.c_str(), _inputVideoFormat.bmi.biWidth, _inputVideoFormat.bmi.biHeight);

    auto outputTypes = InputToOutputMediaType(&inputMediaType);
    const auto newOutputTypeIter = std::ranges::find_if(outputTypes, [this](const CMediaType &newOutputType) -> bool {
        return m_pOutput->GetConnected()->QueryAccept(&newOutputType) == S_OK;
    });

    if (newOutputTypeIter == outputTypes.end()) {
        g_env.Log(L"Downstream does not accept new output format");
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    const CMediaType &newOutputType = *newOutputTypeIter;

    // even though the new VideoFormat may seem the same as the old, some properties (e.g. VIDEOINFOHEADER2::dwControlFlags which controls HDR colorspace)
    // may have changed. it is safe to always send out the new media type
    const Format::VideoFormat newOutputVideoFormat = Format::GetVideoFormat(newOutputType);
    g_env.Log(L"Downstream accepts new output format: name %s, width %5li, height %5li",
              newOutputVideoFormat.pixelFormat->name.c_str(), newOutputVideoFormat.bmi.biWidth, newOutputVideoFormat.bmi.biHeight);

    CheckHr(m_pOutput->GetConnected()->ReceiveConnection(m_pOutput, &newOutputType));

    return S_OK;
}

/**
 * returns S_OK if the next media sample should carry the media type on the output pin.
 */
auto CAviSynthFilter::HandleOutputFormatChange(const AM_MEDIA_TYPE &outputMediaType) -> HRESULT {
    _outputVideoFormat = Format::GetVideoFormat(outputMediaType);

    g_env.Log(L"New output format: name %s, width %5li, height %5li",
              _outputVideoFormat.pixelFormat->name.c_str(), _outputVideoFormat.bmi.biWidth, _outputVideoFormat.bmi.biHeight);

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
                if (LPOLESTR filename; SUCCEEDED(source->GetCurFile(&filename, nullptr))) {
                    _videoSourcePath = filename;
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
        if (FILTER_INFO filterInfo; SUCCEEDED(currFilter->QueryFilterInfo(&filterInfo))) {
            QueryFilterInfoReleaseGraph(filterInfo);
            _videoFilterNames.emplace_back(filterInfo.achName);
            g_env.Log(L"Filter in graph: %s", filterInfo.achName);
        }

        const std::optional<IPin *> optOutputPin = FindFirstVideoOutputPin(currFilter);
        if (!optOutputPin) {
            break;
        }

        ATL::CComPtr<IPin> nextInputPin;
        if (ATL::CComPtr<IPin> outputPin = *optOutputPin; FAILED(outputPin->ConnectedTo(&nextInputPin))) {
            break;
        }

        if (PIN_INFO pinInfo; SUCCEEDED(nextInputPin->QueryPinInfo(&pinInfo))) {
            QueryPinInfoReleaseFilter(pinInfo);
            currFilter = pinInfo.pFilter;
        }
    }
}

}
