#include "pch.h"
#include "filter.h"
#include "g_constants.h"
#include "g_variables.h"
#include "source_clip.h"
#include "filter_prop.h"


#ifdef _DEBUG
#pragma comment(lib, "strmbasd.lib")
#else
#pragma comment(lib, "strmbase.lib")
#endif
#pragma comment(lib, "winmm.lib")

#pragma comment(lib, "AviSynth.lib")

#define CheckHr(expr) { hr = (expr); if (FAILED(hr)) { return hr; } }

static AVS_Clip *g_scriptClip = nullptr;

static void CALLBACK InitRoutine(BOOL bLoading, const CLSID *rclsid) {
    if (bLoading == FALSE) {
        if (g_scriptClip != nullptr) {
            avs_release_clip(g_scriptClip);
        }
        if (g_env != nullptr) {
            avs_delete_script_environment(g_env);
        }
    }
}

static constexpr REGPINTYPES IN_PIN_TYPE_REG[] = {
    { &MEDIATYPE_Video, &MEDIASUBTYPE_NV12 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_YV12 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_P010 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_P016 },
};
static constexpr UINT IN_PIN_TYPE_COUNT = sizeof(IN_PIN_TYPE_REG) / sizeof(IN_PIN_TYPE_REG[0]);

static constexpr REGPINTYPES OUT_PIN_TYPE_REG[] = {
    { &MEDIATYPE_Video, &MEDIASUBTYPE_NV12 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_P010 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_P016 },
};
static constexpr UINT OUT_PIN_TYPE_COUNT = sizeof(OUT_PIN_TYPE_REG) / sizeof(OUT_PIN_TYPE_REG[0]);

static constexpr REGFILTERPINS PIN_REG[] = {
    { nullptr              // pin name (obsolete)
    , FALSE                // is pin rendered?
    , FALSE                // is this output pin?
    , FALSE                // Can the filter create zero instances?
    , FALSE                // Does the filter create multiple instances?
    , &CLSID_NULL          // filter CLSID the pin connects to (obsolete)
    , nullptr              // pin name the pin connects to (obsolete)
    , IN_PIN_TYPE_COUNT    // pin media type count
    , IN_PIN_TYPE_REG },   // pin media types

    { nullptr              // pin name (obsolete)
    , FALSE                // is pin rendered?
    , TRUE                 // is this output pin?
    , FALSE                // Can the filter create zero instances?
    , FALSE                // Does the filter create multiple instances?
    , &CLSID_NULL          // filter CLSID the pin connects to (obsolete)
    , nullptr              // pin name the pin connects to (obsolete)
    , OUT_PIN_TYPE_COUNT   // pin media type count
    , OUT_PIN_TYPE_REG },  // pin media types
};

static constexpr ULONG PIN_COUNT = sizeof(PIN_REG) / sizeof(PIN_REG[0]);

static constexpr AMOVIESETUP_FILTER FILTER_REG = {
    &CLSID_AviSynthFilter,  // filter CLSID
    FILTER_NAME_WIDE,       // filter name
    MERIT_DO_NOT_USE,       // filter merit
    PIN_COUNT,              // pin count
    PIN_REG                 // pin information
};

CFactoryTemplate g_Templates[] = {
    { FILTER_NAME_WIDE
    , &CLSID_AviSynthFilter
    , CAviSynthFilter::CreateInstance
    , InitRoutine
    , &FILTER_REG },

    { PROPERTY_PAGE_NAME_WIDE
    , &CLSID_PropertyPage
    , CAviSynthFilterProp::CreateInstance
    , nullptr
    , nullptr },
};
int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

STDAPI DllRegisterServer() {
    return AMovieDllRegisterServer2(TRUE);
}

STDAPI DllUnregisterServer() {
    return AMovieDllRegisterServer2(FALSE);
}

extern "C" DECLSPEC_NOINLINE BOOL WINAPI DllEntryPoint(HINSTANCE hInstance, ULONG ulReason, __inout_opt LPVOID pv);

auto APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) -> BOOL {
    return DllEntryPoint(hModule, ul_reason_for_call, lpReserved);
}

static auto AVSC_CC CreateAvsFilterSource(AVS_ScriptEnvironment *env, AVS_Value args, void *user_data) -> AVS_Value {
    AVS_FilterInfo *sourceFilter;
    AVS_Clip *sourceClip = avs_new_c_filter(env, &sourceFilter, args, 0);

    *sourceFilter = *reinterpret_cast<AVS_FilterInfo *>(user_data);

    const AVS_Value ret = avs_new_value_clip(sourceClip);
    avs_release_clip(sourceClip);

    return ret;
}

auto WINAPI CAviSynthFilter::CreateInstance(LPUNKNOWN pUnk, HRESULT *phr) -> CUnknown * {
    auto newFilter = new CAviSynthFilter(pUnk, phr);

    if (newFilter == nullptr && phr != nullptr) {
        *phr = E_OUTOFMEMORY;
    }

    return newFilter;
}

CAviSynthFilter::CAviSynthFilter(LPUNKNOWN pUnk, HRESULT *phr)
    : CVideoTransformFilter(FILTER_NAME, pUnk, CLSID_AviSynthFilter)
    , _avsRefTime(-1)
    , _segmentDuration(-1)
    , _inBitmapInfo(nullptr)
    , _outBitmapInfo(nullptr)
    , _avsFile(_registry.ReadValue()) {
    if (g_env == nullptr) {
        g_env = avs_create_script_environment(AVISYNTH_INTERFACE_VERSION);
    }
}

auto CAviSynthFilter::NonDelegatingQueryInterface(REFIID riid, void **ppv) -> HRESULT {
    CheckPointer(ppv, E_POINTER);

    if (riid == IID_IAvsFile) {
        return GetInterface(static_cast<IAvsFile *>(this), ppv);
    }
    if (riid == IID_ISpecifyPropertyPages) {
        return GetInterface(static_cast<ISpecifyPropertyPages *>(this), ppv);
    }

    return CVideoTransformFilter::NonDelegatingQueryInterface(riid, ppv);
}

auto CAviSynthFilter::CheckInputType(const CMediaType *mtIn) -> HRESULT {
    HRESULT hr;

    CheckHr(ValidateMediaType(mtIn));

    const int formatIndex = Format::LookupInput(mtIn->subtype);
    if (formatIndex == -1) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    return S_OK;
}

auto CAviSynthFilter::GetMediaType(int iPosition, CMediaType *pMediaType) -> HRESULT {
    if (iPosition < 0) {
        return E_INVALIDARG;
    }

    if (m_pInput->IsConnected() == FALSE) {
        return E_UNEXPECTED;
    }

    if (iPosition >= _upstreamTypes.size()) {
        return VFW_S_NO_MORE_ITEMS;
    }

    /**
     * create a media type based on the current input media type while changing its subtype
     * also change fields in format if necessary
     *
     * for example, when the original subtype has 8-bit samples and new subtype has 16-bit,
     * all "size" and FourCC values need to be adjusted.
     */

    *pMediaType = m_pInput->CurrentMediaType();

    const int inFormatIndex = Format::LookupInput(pMediaType->subtype);
    const int outFormatIndex = _upstreamTypes[iPosition].formatIndex;

    const Format::PixelFormat inFormat = Format::FORMATS[inFormatIndex];
    const Format::PixelFormat outFormat = Format::FORMATS[outFormatIndex];

    if (inFormat.unitSize != outFormat.unitSize) {
        BITMAPINFOHEADER *bmi = GetBitmapInfo(*pMediaType);
        pMediaType->subtype = outFormat.output;
        pMediaType->lSampleSize = pMediaType->lSampleSize * outFormat.unitSize / inFormat.unitSize;
        bmi->biBitCount = bmi->biBitCount * outFormat.unitSize / inFormat.unitSize;
        bmi->biCompression = FOURCCMap(&outFormat.output).GetFOURCC();
        bmi->biSizeImage = bmi->biSizeImage * outFormat.unitSize / inFormat.unitSize;
    }

    return S_OK;
}

auto CAviSynthFilter::CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut) -> HRESULT {
    // the actual check is in CompleteConnect(). we want to allow all media types for the output pin
    return S_OK;
}

auto CAviSynthFilter::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProperties) -> HRESULT {
    HRESULT hr;

    // we need at least 2 buffers so that we can hold pOut while preparing another extra sample
    pProperties->cBuffers = max(2, pProperties->cBuffers);
    pProperties->cbBuffer = max(m_pInput->CurrentMediaType().lSampleSize, pProperties->cbBuffer);

    ALLOCATOR_PROPERTIES actual;
    CheckHr(pAlloc->SetProperties(pProperties, &actual));

    if (pProperties->cBuffers > actual.cBuffers ||
        pProperties->cbBuffer > actual.cbBuffer) {
        return E_FAIL;
    }

    return S_OK;
}

auto CAviSynthFilter::CompleteConnect(PIN_DIRECTION dir, IPin *pReceivePin) -> HRESULT {
    HRESULT hr;

    /**
     * Our media type negotiation logic
     *
     * The upstream's output pin supports N media types. The downstream's input pin supports M media types.
     * Our transform filter has an array of (in_type -> out_type) mappings. What we need to do is to find mappings
     * that are acceptable by both sides.
     *
     * The filter graph connects upstream's output pin to our input pin first. At this stage, we just accept everything.
     * We enumerate the input pin for its media types (intersect with ours), and  save the list for later use (_upstreamTypes) upon CompleteConnect().
     *
     * During downstream connection, we also accept everything that's valid output from our filter. Once both input and output
     * pins are connected, we check both side's current media type. If we are lucky that they are already valid tranform, we just proceed.
     *
     * Most of the time they are not valid. So we look up the saved list for its output type, and reconnect the input pin
     * using the corresponding upstream media type.
     *
     * Once the input pin is reconnected, since we are sending the same types in GetMediaType(), the output pin should still
     * have the same media type as before. This time both sides should be compatible.
     */

    if (dir == PINDIR_INPUT) {
        if (_upstreamTypes.empty()) {
            IEnumMediaTypes *enumTypes;
            CheckHr(pReceivePin->EnumMediaTypes(&enumTypes));

            AM_MEDIA_TYPE *nextType;
            while (true) {
                hr = enumTypes->Next(1, &nextType, nullptr);
                if (hr == S_OK) {
                    const int formatIndex = Format::LookupInput(nextType->subtype);
                    if (formatIndex == -1) {
                        DeleteMediaType(nextType);
                    } else {
                        _upstreamTypes.emplace_back(MediaTypeFormat { nextType, formatIndex });
                    }
                } else if (hr == VFW_E_ENUM_OUT_OF_SYNC) {
                    CheckHr(enumTypes->Reset());

                    for (const MediaTypeFormat &elem : _upstreamTypes) {
                        DeleteMediaType(elem.mediaType);
                    }
                    _upstreamTypes.clear();
                } else {
                    break;
                }
            }

            enumTypes->Release();
        }
    }

    if (m_pInput->IsConnected() && m_pOutput->IsConnected()) {
        const int formatIndex = Format::LookupOutput(m_pOutput->CurrentMediaType().subtype);
        if (Format::FORMATS[formatIndex].input != m_pInput->CurrentMediaType().subtype) {
            for (const MediaTypeFormat &elem : _upstreamTypes) {
                if (elem.formatIndex == formatIndex) {
                    CheckHr(ReconnectPin(m_pInput, elem.mediaType));
                    break;
                }
            }
        } else {
            for (const MediaTypeFormat &elem : _upstreamTypes) {
                DeleteMediaType(elem.mediaType);
            }

            _inBitmapInfo = GetBitmapInfo(m_pInput->CurrentMediaType());
            _outBitmapInfo = GetBitmapInfo(m_pOutput->CurrentMediaType());

            _avsFilter = {};
            _avsFilter.env = g_env;
            _avsFilter.user_data = &_bufferHandler;
            _avsFilter.get_frame = filter_get_frame;
            _avsFilter.get_parity = filter_get_parity;

            avs_add_function(g_env, "avsfilter_source", "", CreateAvsFilterSource, &_avsFilter);
        }
    }

    return CVideoTransformFilter::CompleteConnect(dir, pReceivePin);
}

auto CAviSynthFilter::NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate) -> HRESULT {
    _segmentDuration = tStop - tStart;
    _avsRefTime = -1;
    return CVideoTransformFilter::NewSegment(tStart, tStop, dRate);
}

auto CAviSynthFilter::Transform(IMediaSample *pIn, IMediaSample *pOut) -> HRESULT {
    HRESULT hr;

    CRefTime streamTime;
    CheckHr(StreamTime(streamTime));
    streamTime = min(streamTime, m_tStart);

    IMediaSample2 *pIn2;
    CheckHr(pIn->QueryInterface(&pIn2));
    AM_SAMPLE2_PROPERTIES inProps;
    CheckHr(pIn2->GetProperties(FIELD_OFFSET(AM_SAMPLE2_PROPERTIES, cbBuffer), reinterpret_cast<BYTE *>(&inProps)));
    pIn2->Release();

    IMediaSample2 *pOut2;
    CheckHr(pOut->QueryInterface(&pOut2));
    AM_SAMPLE2_PROPERTIES outProps;
    CheckHr(pOut2->GetProperties(FIELD_OFFSET(AM_SAMPLE2_PROPERTIES, cbBuffer), reinterpret_cast<BYTE *>(&outProps)));

    bool resetClip = _avsRefTime < 0;
    if (inProps.dwSampleFlags & AM_SAMPLE_TYPECHANGED) {
        _inBitmapInfo = GetBitmapInfo(m_pInput->CurrentMediaType());
        resetClip = true;
    }
    if (outProps.dwSampleFlags & AM_SAMPLE_TYPECHANGED) {
        _outBitmapInfo = GetBitmapInfo(m_pOutput->CurrentMediaType());
        resetClip = true;
    }

    if (resetClip) {
        if (g_scriptClip != nullptr) {
            avs_release_clip(g_scriptClip);
        }

        CreateAvsVideoInfo(_avsFilter.vi);

        AVS_Value importResult;
        if (_avsFile.empty()) {
            AVS_Value evalArgs[] = { avs_new_value_string("return avsfilter_source()")
                                   , avs_new_value_string(EVAL_FILENAME) };
            importResult = avs_invoke(g_env, "Eval", avs_new_value_array(evalArgs, 2), nullptr);
        } else {
            importResult = avs_invoke(g_env, "Import", avs_new_value_string(_avsFile.c_str()), nullptr);
        }

        if (!avs_is_clip(importResult)) {
            std::string errorScript;
            if (avs_is_error(importResult)) {
                errorScript = avs_as_error(importResult);
                std::replace(errorScript.begin(), errorScript.end(), '"', '\'');
                std::replace(errorScript.begin(), errorScript.end(), '\n', ' ');
            } else {
                errorScript = "Unrecognized return value from script. Invalid script.";
            }

            errorScript.insert(0, "return avsfilter_source().Subtitle(\"");
            errorScript.append("\")");
            AVS_Value evalArgs[] = { avs_new_value_string(errorScript.c_str())
                                   , avs_new_value_string(EVAL_FILENAME) };
            importResult = avs_invoke(g_env, "Eval", avs_new_value_array(evalArgs, 2), nullptr);
        }

        g_scriptClip = avs_take_clip(importResult, g_env);
        avs_release_value(importResult);
    }

    const AVS_VideoInfo *avsVideoInfo = avs_get_video_info(g_scriptClip);
    if (resetClip) {
        _bufferHandler.Reset(m_pInput->CurrentMediaType().subtype, avsVideoInfo);
    }
    const REFERENCE_TIME timePerFrame = avsVideoInfo->fps_denominator * UNITS / avsVideoInfo->fps_numerator;

    // when render is starving for frames, we should skip the extra samples
    if (_avsRefTime < 0 || m_itrLate > 0) {
        _avsRefTime = inProps.tStart;
    }

    /*
    std::cout << "streamTime: " << streamTime << " ";
    std::cout << "sampleTime: " << inProps.tStart << " ";
    std::cout << "avsRefTime: " << _avsRefTime << " ";
    std::cout << "timePerFrame: " << timePerFrame << " ";
    std::cout << "streamFrame: " << streamTime / timePerFrame << " ";
    std::cout << "sampleFrame: " << inProps.tStart / timePerFrame << " ";
    std::cout << "avsFrame: " << _avsRefTime / timePerFrame << " ";
    */

    _bufferHandler.CreateFrame(inProps.tStart, inProps.pbBuffer, _inBitmapInfo->biWidth);

    AVS_VideoFrame *clipFrame;
    while (true) {
        const int frameNb = _avsRefTime / timePerFrame;
        clipFrame = avs_get_frame(g_scriptClip, frameNb);

        outProps.tStart = _avsRefTime;
        outProps.tStop = _avsRefTime + timePerFrame;
        _avsRefTime += timePerFrame;

        if (outProps.tStop > inProps.tStart) {
            break;
        }

        IMediaSample *extraSample;
        CheckHr(m_pOutput->GetDeliveryBuffer(&extraSample, &outProps.tStart, &outProps.tStop, 0));

        IMediaSample2 *extraSample2;
        CheckHr(extraSample->QueryInterface(&extraSample2));
        CheckHr(extraSample2->SetProperties(FIELD_OFFSET(AM_SAMPLE2_PROPERTIES, pbBuffer), reinterpret_cast<BYTE *>(&outProps)));
        extraSample2->Release();

        BYTE *destBuf;
        CheckHr(extraSample->GetPointer(&destBuf));
        _bufferHandler.WriteSample(clipFrame, destBuf, _outBitmapInfo->biWidth);
        avs_release_frame(clipFrame);

        CheckHr(m_pOutput->Deliver(extraSample));
        extraSample->Release();
    }

    CheckHr(pOut2->SetProperties(FIELD_OFFSET(AM_SAMPLE2_PROPERTIES, pbBuffer), reinterpret_cast<BYTE *>(&outProps)));
    pOut2->Release();

    _bufferHandler.WriteSample(clipFrame, outProps.pbBuffer, _outBitmapInfo->biWidth);
    avs_release_frame(clipFrame);

    _bufferHandler.GarbageCollect(streamTime);

    return S_OK;
}

auto CAviSynthFilter::GetPages(CAUUID *pPages) -> HRESULT {
    if (pPages == nullptr) {
        return E_POINTER;
    }

    pPages->cElems = 1;
    pPages->pElems = static_cast<GUID *>(CoTaskMemAlloc(sizeof(GUID)));
    if (pPages->pElems == nullptr) {
        return E_OUTOFMEMORY;
    }

    pPages->pElems[0] = CLSID_PropertyPage;
    return S_OK;
}

auto CAviSynthFilter::GetAvsFile(std::string &avsFile) const -> HRESULT {
    avsFile = _avsFile;
    return S_OK;
}

auto CAviSynthFilter::UpdateAvsFile(const std::string &avsFile) -> HRESULT {
    _avsFile = avsFile;
    _registry.WriteValue(avsFile);
    return S_OK;
}

auto CAviSynthFilter::ReloadAvsFile() -> HRESULT {
    _avsRefTime = -1;
    return S_OK;
}

/**
 * Check if the media type has valid VideoInfo * format block.
 */
auto CAviSynthFilter::ValidateMediaType(const AM_MEDIA_TYPE *mediaType) -> HRESULT {
    if (mediaType->majortype != MEDIATYPE_Video) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    if (FAILED(CheckVideoInfoType(mediaType)) && FAILED(CheckVideoInfo2Type(mediaType))) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    return S_OK;
}

/**
 * Extract BITMAPINFOHEADER from a media type.
 */
auto CAviSynthFilter::GetBitmapInfo(AM_MEDIA_TYPE &mediaType) -> BITMAPINFOHEADER * {
    BITMAPINFOHEADER *bitmapInfo = nullptr;

    if (SUCCEEDED(CheckVideoInfoType(&mediaType))) {
        bitmapInfo = &reinterpret_cast<VIDEOINFOHEADER *>(mediaType.pbFormat)->bmiHeader;
    } else if (SUCCEEDED(CheckVideoInfo2Type(&mediaType))) {
        bitmapInfo = &reinterpret_cast<VIDEOINFOHEADER2 *>(mediaType.pbFormat)->bmiHeader;
    }

    return bitmapInfo;
}

/**
 * Create AVS video info from current input media type.
 */
auto CAviSynthFilter::CreateAvsVideoInfo(AVS_VideoInfo &videoInfo) const -> void {
    const CMediaType &mediaType = m_pInput->CurrentMediaType();
    const VIDEOINFOHEADER *vih = reinterpret_cast<VIDEOINFOHEADER *>(mediaType.pbFormat);

    videoInfo = {};
    videoInfo.width = vih->rcSource.right;
    videoInfo.height = vih->rcSource.bottom;
    videoInfo.fps_numerator = UNITS;
    videoInfo.fps_denominator = static_cast<unsigned int>(vih->AvgTimePerFrame);
    videoInfo.num_frames = _segmentDuration < 0 ? INFINITE_NUM_FRAMES : _segmentDuration / vih->AvgTimePerFrame;

    const int formatIndex = Format::LookupInput(mediaType.subtype);
    videoInfo.pixel_type = Format::FORMATS[formatIndex].avs;
}
