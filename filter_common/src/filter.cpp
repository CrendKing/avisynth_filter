// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "filter.h"

#include "constants.h"
#include "input_pin.h"
#include "macros.h"
#include "prop_settings.h"
#include "prop_status.h"


namespace SynthFilter {

CSynthFilter::CSynthFilter(LPUNKNOWN pUnk, HRESULT *phr)
    : CVideoTransformFilter(FILTER_NAME_FULL, pUnk, __uuidof(CSynthFilter)) {
    if (_numFilterInstances == 0) {
        Environment::Create();
        FrameServerCommon::Create();
        MainFrameServer::Create()->LinkSynthFilter(this);
        AuxFrameServer::Create();
        Format::Initialize();
    }
    _numFilterInstances += 1;

    Environment::GetInstance().Log(L"CSynthFilter(): %p", this);
}

CSynthFilter::~CSynthFilter() {
    Environment::GetInstance().Log(L"Destroy CSynthFilter: %p", this);

    _numFilterInstances -= 1;
    if (_numFilterInstances == 0) {
        _remoteControl.reset();
        frameHandler.reset();
        AuxFrameServer::Destroy();
        MainFrameServer::Destroy();
        FrameServerCommon::Destroy();
        Environment::Destroy();
    }
}

auto STDMETHODCALLTYPE CSynthFilter::NonDelegatingQueryInterface(REFIID riid, __deref_out void **ppv) -> HRESULT {
    CheckPointer(ppv, E_POINTER);

    if (riid == IID_ISpecifyPropertyPages) {
        return GetInterface(static_cast<ISpecifyPropertyPages *>(this), ppv);
    }

    return __super::NonDelegatingQueryInterface(riid, ppv);
}

auto CSynthFilter::GetPin(int n) -> CBasePin * {
    HRESULT hr = S_OK;

    if (n == 0) {
        if (m_pInput == nullptr) {
            m_pInput = new CSynthFilterInputPin(NAME(FILTER_NAME_BASE " input pin"), this, &hr, L"Input");
        }
        return m_pInput;
    }
    if (n == 1) {
        if (m_pOutput == nullptr) {
            m_pOutput = new CTransformOutputPin(NAME(FILTER_NAME_BASE " output pin"), this, &hr, L"Output");
        }
        return m_pOutput;
    }

    return nullptr;
}

auto CSynthFilter::CheckConnect(PIN_DIRECTION direction, IPin *pPin) -> HRESULT {
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
                const std::shared_ptr<AM_MEDIA_TYPE> nextTypePtr(nextType, &DeleteMediaType);

                if (const Format::PixelFormat *optInputPixelFormat = GetInputPixelFormat(nextType);
                    optInputPixelFormat && std::ranges::find(_compatibleMediaTypes, optInputPixelFormat, &MediaTypePair::inputPixelFormat) == _compatibleMediaTypes.end()) {
                    // invoke the script with each supported input pixel format, and observe the output frameserver format
                    if (!AuxFrameServer::GetInstance().ReloadScript(*nextType, Environment::GetInstance().IsRemoteControlEnabled())) {
                        Environment::GetInstance().Log(L"Disconnect filter by user request");
                        _disconnectFilter = true;
                        return VFW_E_TYPE_NOT_ACCEPTED;
                    }

                    // all media types that share the same frameserver format are acceptable for output pin connection
                    const int scriptFormatId = AuxFrameServer::GetInstance().GetScriptPixelType();
                    for (const Format::PixelFormat &frameServerPixelFormat : Format::LookupFrameServerFormatId(scriptFormatId)) {
                        const CMediaType outputMediaType = AuxFrameServer::GetInstance().GenerateMediaType(frameServerPixelFormat, nextType);
                        _compatibleMediaTypes.emplace_back(nextTypePtr, optInputPixelFormat, outputMediaType, MediaTypeToPixelFormat(&outputMediaType));
                        if (std::ranges::find(_availableOutputMediaTypes, outputMediaType) == _availableOutputMediaTypes.end()) {
                            _availableOutputMediaTypes.emplace_back(outputMediaType);
                        }
                        Environment::GetInstance().Log(L"Add compatible formats: input %5ls output %5ls", optInputPixelFormat->name, frameServerPixelFormat.name);
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

auto CSynthFilter::BreakConnect(PIN_DIRECTION direction) -> HRESULT {
    if (direction == PINDIR_INPUT) {
        _compatibleMediaTypes.clear();
        _availableOutputMediaTypes.clear();
        _mediaTypeReconnectionWatermark = 0;
    }

    return S_OK;
}

auto CSynthFilter::CheckInputType(const CMediaType *mtIn) -> HRESULT {
    bool result = false;

    if (const Format::PixelFormat *optInputPixelFormat = MediaTypeToPixelFormat(mtIn); optInputPixelFormat) {
        if (!IsActive()) {
            result = std::ranges::any_of(_compatibleMediaTypes, [optInputPixelFormat](const MediaTypePair &pair) -> bool {
                return optInputPixelFormat == pair.inputPixelFormat;
            });
            Environment::GetInstance().Log(L"Pre pin connection CheckInputType(): input %5ls result %d", optInputPixelFormat->name, result);
        } else {
            result = std::ranges::any_of(InputToOutputMediaType(mtIn), [this](const CMediaType &newOutputMediaType) -> bool {
                return m_pOutput->GetConnected()->QueryAccept(&newOutputMediaType) == S_OK;
            });
            Environment::GetInstance().Log(L"Post pin connection QueryAccept downstream in CheckInputType(): input %5ls result %d", optInputPixelFormat->name, result);
        }
    } else {
        Environment::GetInstance().Log(L"Unknown input media type in CheckInputType()");
    }

    return result ? S_OK : VFW_E_TYPE_NOT_ACCEPTED;
}

auto CSynthFilter::GetMediaType(int iPosition, CMediaType *pMediaType) -> HRESULT {
    if (iPosition < 0) {
        return E_INVALIDARG;
    }

    if (!m_pInput->IsConnected()) {
        return E_UNEXPECTED;
    }

    if (iPosition >= static_cast<int>(_availableOutputMediaTypes.size())) {
        return VFW_S_NO_MORE_ITEMS;
    }

    *pMediaType = _availableOutputMediaTypes[iPosition];
    Environment::GetInstance().Log(L"GetMediaType() offers media type %2d with %5ls", iPosition, MediaTypeToPixelFormat(pMediaType)->name);

    return S_OK;
}

auto CSynthFilter::CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut) -> HRESULT {
    return S_OK;
}

auto CSynthFilter::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProperties) -> HRESULT {
    HRESULT hr;

    pProperties->cBuffers = std::max(pProperties->cBuffers, 2L);

    const long newMediaSampleSize = Format::GetStrideAlignedMediaSampleSize(m_pOutput->CurrentMediaType(), Format::OUTPUT_MEDIA_SAMPLE_STRIDE_ALIGNMENT);
    pProperties->cbBuffer = std::max(newMediaSampleSize, pProperties->cbBuffer);

    ALLOCATOR_PROPERTIES actual;
    CheckHr(pAlloc->SetProperties(pProperties, &actual));

    if (pProperties->cBuffers > actual.cBuffers || pProperties->cbBuffer > actual.cbBuffer) {
        return E_FAIL;
    }

    return S_OK;
}

auto CSynthFilter::CompleteConnect(PIN_DIRECTION direction, IPin *pReceivePin) -> HRESULT {
    /*
     * The media type negotiation logic
     *
     * Suppose the upstream filter outputs N1 media types (a set). The script could convert those N1 input media types to N2 output media types.
     * The downstream filter accepts N3 media types.
     *
     * If the intersection of N2 and N3 is empty, this won't work. The graph manager will keep trying different downstream filters to find one
     * where the intersection is not empty, or remove AVSF out of the graph.
     *
     * Once the intersection is not empty, it is up to the downstream to choose which to use.
     *
     * The problem is that the graph manager may not query all N1 media types during CheckInputType(). It may stop querying once found a valid transform
     * input/output pair, where the output media type is not acceptable with the downstream.
     *
     * The solution is to enumerate all N1 media types from upstream, generate the whole N2 by calling AuxFrameServer::GenerateMediaType().
     * During GetMediaType() and CheckTransform(), we offer and accept any output media type that is in N2. This allows downstream to choose the best media type it wants to use.
     *
     * Once both input and output pins are connected, we check if the pins' media types are valid transform. If yes, we are lucky and the pin connection completes.
     * If not, we keep the connection to the downstream, reverse lookup the compatible input media type and reconnect input pin with that type.
     * Because the reconnect media type is selected from upstream's enumerated media type, the connection should always succeed at the second time.
     *
     * Since there could be multiple compatible input/output pair, if first input media type fails to reconnect, we raise water mark and try the next candidate
     * input media types, until either a successful reconnection happens, or we exhaust all candidates.
     */

    HRESULT hr;

    if (m_pInput->IsConnected()) {
        if (!m_pOutput->IsConnected()) {
            /*
             * Before output pin is connected, we have a chance to check if input pin's current media type is correctly stride aligned.
             * If not, reconnect with an aligned media type.
             * By doing this, we can save the verbose negotiation via media sample's media type.
             */

            const int strideAlignment = std::max(MEDIA_SAMPLE_STRIDE_ALGINMENT, Format::INPUT_MEDIA_SAMPLE_STRIDE_ALIGNMENT);

            BITMAPINFOHEADER *bmi = Format::GetBitmapInfo(m_pInput->CurrentMediaType());
            const int alignedStride = FFALIGN(bmi->biWidth, strideAlignment);
            if (bmi->biWidth != alignedStride) {
                AM_MEDIA_TYPE alignedMediaType;
                CheckHr(m_pInput->GetConnected()->ConnectionMediaType(&alignedMediaType));

                bmi = Format::GetBitmapInfo(alignedMediaType);
                bmi->biWidth = alignedStride;
                bmi->biSizeImage = GetBitmapSize(bmi);

                if (m_pInput->QueryAccept(&alignedMediaType) == S_OK) {
                    hr = reinterpret_cast<IFilterGraph2 *>(m_pGraph)->ReconnectEx(m_pInput->GetConnected(), &alignedMediaType);
                }

                FreeMediaType(alignedMediaType);
                CheckHr(hr);
            }
        } else {
            const Format::PixelFormat *optConnectionInputPixelFormat = MediaTypeToPixelFormat(&m_pInput->CurrentMediaType());
            const Format::PixelFormat *optConnectionOutputPixelFormat = MediaTypeToPixelFormat(&m_pOutput->CurrentMediaType());
            if (!optConnectionInputPixelFormat || !optConnectionOutputPixelFormat) {
                Environment::GetInstance().Log(L"Unexpected input or output format");
                return E_UNEXPECTED;
            }
            Environment::GetInstance().Log(L"Pins are connected with media types: %5ls -> %5ls", optConnectionInputPixelFormat->name, optConnectionOutputPixelFormat->name);

            bool isMediaTypesCompatible = false;
            int mediaTypeReconnectionIndex = 0;
            const CMediaType *reconnectInputMediaType = nullptr;

            for (const auto &[inputMediaType, inputPixelFormat, outputMediaType, outputPixelFormat] : _compatibleMediaTypes) {
                if (optConnectionOutputPixelFormat == outputPixelFormat) {
                    if (optConnectionInputPixelFormat == inputPixelFormat) {
                        Environment::GetInstance().Log(L"Pin connections are settled");
                        isMediaTypesCompatible = true;
                        TraverseFiltersInGraph();
                        frameHandler->StartWorker();

                        break;
                    }

                    if (mediaTypeReconnectionIndex >= _mediaTypeReconnectionWatermark) {
                        reconnectInputMediaType = static_cast<const CMediaType *>(inputMediaType.get());
                        _mediaTypeReconnectionWatermark += 1;
                        break;
                    }

                    mediaTypeReconnectionIndex += 1;
                }
            }

            if (!isMediaTypesCompatible) {
                if (reconnectInputMediaType == nullptr) {
                    Environment::GetInstance().Log(L"Failed to reconnect with any of the %d candidate input media types", _mediaTypeReconnectionWatermark);
                    return E_UNEXPECTED;
                }

                Environment::GetInstance().Log(L"Attempt to reconnect input pin with media type %5ls", MediaTypeToPixelFormat(reconnectInputMediaType)->name);
                CheckHr(ReconnectPin(m_pInput, reconnectInputMediaType));
            }
        }
    }

    return __super::CompleteConnect(direction, pReceivePin);
}

auto CSynthFilter::StartStreaming() -> HRESULT {
    AuxFrameServer::GetInstance().ReloadScript(m_pInput->CurrentMediaType(), true);
    _inputVideoFormat = Format::GetVideoFormat(m_pInput->CurrentMediaType(), &AuxFrameServer::GetInstance());
    _outputVideoFormat = Format::GetVideoFormat(m_pOutput->CurrentMediaType(), &AuxFrameServer::GetInstance());

    if (Environment::GetInstance().IsRemoteControlEnabled()) {
        // remote control should start after the input video format is initialized
        _remoteControl->Start();
    }

    // the paired BeginFlush() is in StopStreaming()
    frameHandler->EndFlush();

    return __super::StartStreaming();
}

auto CSynthFilter::Receive(IMediaSample *pSample) -> HRESULT {
    HRESULT hr;

    if (m_pInput->SampleProps()->dwStreamId != AM_STREAM_MEDIA) {
        return m_pOutput->Deliver(pSample);
    }

    AM_MEDIA_TYPE *pmt;
    pSample->GetMediaType(&pmt);
    if (pmt != nullptr && pmt->pbFormat != nullptr) {
        m_pInput->CurrentMediaType() = *pmt;
        _inputVideoFormat = Format::GetVideoFormat(*pmt, &MainFrameServer::GetInstance());
        DeleteMediaType(pmt);
        _isInputMediaTypeChanged = true;
    }

    if (ShouldSkipFrame(pSample)) {
        m_bSampleSkipped = TRUE;
        return S_OK;
    }

    m_bSampleSkipped = FALSE;

    if (pSample->IsDiscontinuity() == S_OK) {
        m_nWaitForKey = 30;
    }

    m_tDecodeStart = timeGetTime();

    hr = frameHandler->AddInputSample(pSample);

    m_tDecodeStart = timeGetTime() - m_tDecodeStart;
    m_itrAvgDecode = m_tDecodeStart * (10000 / 16) + 15 * (m_itrAvgDecode / 16);

    if (m_nWaitForKey) {
        m_nWaitForKey--;
    }
    if (m_nWaitForKey && pSample->IsSyncPoint() == S_OK) {
        m_nWaitForKey = 0;
    }

    if (m_nWaitForKey && hr == S_OK) {
        DbgLog((LOG_TRACE, 3, TEXT("still waiting for a keyframe")));
        hr = S_FALSE;
    }

    if (S_FALSE == hr) {
        m_bSampleSkipped = TRUE;
        if (!m_bQualityChanged) {
            m_bQualityChanged = TRUE;
            NotifyEvent(EC_QUALITY_CHANGE, 0, 0);
        }
        return S_OK;
    }

    return hr;
}

auto CSynthFilter::BeginFlush() -> HRESULT {
    if (IsActive()) {
        frameHandler->BeginFlush();
    }

    return __super::BeginFlush();
}

auto CSynthFilter::EndFlush() -> HRESULT {
    if (IsActive()) {
        frameHandler->WaitForWorkerLatch();
        MainFrameServer::GetInstance().StopScript();
        frameHandler->EndFlush();
    }

    return __super::EndFlush();
}

auto CSynthFilter::StopStreaming() -> HRESULT {
    frameHandler->BeginFlush();
    frameHandler->WaitForWorkerLatch();
    MainFrameServer::GetInstance().StopScript();

    // keep flushing until start streaming

    return __super::StopStreaming();
}

auto STDMETHODCALLTYPE CSynthFilter::GetPages(__RPC__out CAUUID *pPages) -> HRESULT {
    CheckPointer(pPages, E_POINTER);

    pPages->pElems = static_cast<GUID *>(CoTaskMemAlloc(2 * sizeof(GUID)));
    if (pPages->pElems == nullptr) {
        return E_OUTOFMEMORY;
    }

    pPages->cElems = 1;
    pPages->pElems[0] = __uuidof(CSynthFilterPropSettings);

    if (m_State != State_Stopped) {
        pPages->cElems += 1;
        pPages->pElems[1] = __uuidof(CSynthFilterPropStatus);
    }

    return S_OK;
}

auto CSynthFilter::ReloadScript(const std::filesystem::path &scriptPath) -> void {
    FrameServerCommon::GetInstance().SetScriptPath(scriptPath);
    _needReloadScript = true;
}

auto CSynthFilter::GetFrameServerState() const -> AvsState {
    if (MainFrameServer::GetInstance().GetErrorString()) {
        return AvsState::Error;
    }

    switch (m_State) {
    case State_Running:
        return AvsState::Running;
    case State_Stopped:
        return AvsState::Stopped;
    default:
        return AvsState::Paused;
    }
}

/**
 * Check if the media type has valid VideoInfo data.
 */
auto CSynthFilter::MediaTypeToPixelFormat(const AM_MEDIA_TYPE *mediaType) -> const Format::PixelFormat * {
    if (mediaType->majortype != MEDIATYPE_Video) {
        return nullptr;
    }

    if (FAILED(CheckVideoInfoType(mediaType)) && FAILED(CheckVideoInfo2Type(mediaType))) {
        return nullptr;
    }

    return Format::LookupMediaSubtype(mediaType->subtype);
}

auto CSynthFilter::GetInputPixelFormat(const AM_MEDIA_TYPE *mediaType) -> const Format::PixelFormat * {
    if (const Format::PixelFormat *optInputPixelFormat = MediaTypeToPixelFormat(mediaType)) {
        if (Environment::GetInstance().IsInputFormatEnabled(optInputPixelFormat->name)) {
            return optInputPixelFormat;
        }

        Environment::GetInstance().Log(L"Reject input format due to settings: %ls", optInputPixelFormat->name);
    }

    return nullptr;
}

auto CSynthFilter::FindFirstVideoOutputPin(IBaseFilter *pFilter) -> std::optional<IPin *> {
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

auto CSynthFilter::TraverseFiltersInGraph() -> void {
    _videoSourcePath.clear();
    _videoFilterNames.clear();

    ATL::CComPtr<IEnumFilters> enumFilters;
    if (FAILED(m_pGraph->EnumFilters(&enumFilters))) {
        return;
    }

    IBaseFilter *currFilter;
    while (true) {
        if (const HRESULT hr = enumFilters->Next(1, &currFilter, nullptr); hr == S_OK) {
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
            Environment::GetInstance().Log(L"Filter in graph: %ls", filterInfo.achName);
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
