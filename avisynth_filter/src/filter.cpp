#include "pch.h"
#include "filter.h"
#include "format.h"
#include "constants.h"
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

#ifdef _DEBUG
#include <client/windows/handler/exception_handler.h>
static google_breakpad::ExceptionHandler *g_exHandler;
#endif

#ifdef LOGGING
FILE *g_loggingStream;
#endif

static void CALLBACK InitRoutine(BOOL bLoading, const CLSID *rclsid) {
    if (bLoading == TRUE) {
#ifdef _DEBUG
        g_exHandler = new google_breakpad::ExceptionHandler(L".", nullptr, nullptr, nullptr, google_breakpad::ExceptionHandler::HANDLER_EXCEPTION, MiniDumpWithIndirectlyReferencedMemory, L"", nullptr);
#endif

#ifdef LOGGING
        freopen_s(&g_loggingStream, "C:\\avs.log", "w", stdout);
#endif
    } else {
#ifdef LOGGING
        fclose(g_loggingStream);
#endif

#ifdef _DEBUG
        delete g_exHandler;
#endif
    }
}

static constexpr REGPINTYPES PIN_TYPE_REG[] = {
    { &MEDIATYPE_Video, &MEDIASUBTYPE_NV12 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_I420 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_IYUV },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_YV12 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_P010 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_P016 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_YUY2 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_UYVY },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_RGB24 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_RGB32 },
};
static constexpr UINT PIN_TYPE_COUNT = sizeof(PIN_TYPE_REG) / sizeof(PIN_TYPE_REG[0]);

static constexpr REGFILTERPINS PIN_REG[] = {
    { nullptr              // pin name (obsolete)
    , FALSE                // is pin rendered?
    , FALSE                // is this output pin?
    , FALSE                // Can the filter create zero instances?
    , FALSE                // Does the filter create multiple instances?
    , &CLSID_NULL          // filter CLSID the pin connects to (obsolete)
    , nullptr              // pin name the pin connects to (obsolete)
    , PIN_TYPE_COUNT    // pin media type count
    , PIN_TYPE_REG },   // pin media types

    { nullptr              // pin name (obsolete)
    , FALSE                // is pin rendered?
    , TRUE                 // is this output pin?
    , FALSE                // Can the filter create zero instances?
    , FALSE                // Does the filter create multiple instances?
    , &CLSID_NULL          // filter CLSID the pin connects to (obsolete)
    , nullptr              // pin name the pin connects to (obsolete)
    , PIN_TYPE_COUNT   // pin media type count
    , PIN_TYPE_REG },  // pin media types
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

auto __cdecl CreateAvsFilterSource(AVSValue args, void *user_data, IScriptEnvironment *env) -> AVSValue {
    return reinterpret_cast<SourceClip *>(user_data);
}

auto __cdecl CreateAvsFilterDisconnect(AVSValue args, void *user_data, IScriptEnvironment *env) -> AVSValue {
    // the void type is internal in AviSynth and cannot be instantiated by user script, ideal for disconnect heuristic
    return AVSValue();
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
    , _avsEnv(nullptr)
    , _avsScriptClip(nullptr)
    , _inBitmapInfo(nullptr)
    , _outBitmapInfo(nullptr)
    , _threadShutdown(false)
    , _rejectConnection(false)
    , _reloadAvsFile(false)
    , _avsFile(_registry.ReadValue()) {
}

CAviSynthFilter::~CAviSynthFilter() {
    if (_deliveryThread.joinable()) {
        _threadShutdown = true;
        _threadCondition.notify_one();
        _bufferHandler.StartDraining();
        _deliveryThread.join();
        _bufferHandler.StopDraining();
    }

    if (_avsScriptClip != nullptr) {
        _avsScriptClip = nullptr;
    }

    if (_avsEnv != nullptr) {
        _avsEnv->DeleteScriptEnvironment();
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

    /*
     * create a media type based on the current input media type while changing its subtype
     * also change fields in format if necessary
     *
     * for example, when the original subtype has 8-bit samples and new subtype has 16-bit,
     * all "size" and FourCC values need to be adjusted.
     */

    *pMediaType = m_pInput->CurrentMediaType();

    const int outFormatIndex = _upstreamTypes[iPosition].formatIndex;
    const Format::PixelFormat outFormat = Format::FORMATS[outFormatIndex];
    pMediaType->subtype = outFormat.output;

    VIDEOINFOHEADER *vih = reinterpret_cast<VIDEOINFOHEADER *>(pMediaType->pbFormat);
    vih->AvgTimePerFrame = _timePerFrame;

    BITMAPINFOHEADER *bmi = GetBitmapInfo(*pMediaType);
    bmi->biWidth = _avsScriptVideoInfo.width;
    bmi->biHeight = _avsScriptVideoInfo.height;
    bmi->biBitCount = outFormat.bitsPerPixel;
    bmi->biCompression = FOURCCMap(&outFormat.output).GetFOURCC();
    bmi->biSizeImage = bmi->biWidth * bmi->biHeight * bmi->biBitCount / 8;

    return S_OK;
}

auto CAviSynthFilter::CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut) -> HRESULT {
    HRESULT hr;

    CheckHr(ValidateMediaType(mtOut));

    const int formatIndex = Format::LookupOutput(mtOut->subtype);
    if (formatIndex == -1) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    // the actual check for compatible subtype is in CompleteConnect(). we want to allow all video media types for the output pin

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

    // set to true when avs script returns avsfilter_disconnect()
    if (_rejectConnection) {
        return E_ABORT;
    }

    /*
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

        if (_avsEnv == nullptr) {
            _avsEnv = CreateScriptEnvironment2();
            _avsEnv->AddFunction("avsfilter_source", "", CreateAvsFilterSource, new SourceClip(_avsSourceVideoInfo, _bufferHandler));
            _avsEnv->AddFunction("avsfilter_disconnect", "", CreateAvsFilterDisconnect, nullptr);
        }

        if (!CreateScriptClip()) {
            _rejectConnection = true;
            return E_ABORT;
        }
    }

    if (m_pInput->IsConnected() && m_pOutput->IsConnected()) {
        const int formatIndex = Format::LookupOutput(m_pOutput->CurrentMediaType().subtype);
        if (formatIndex == -1) {
            return E_UNEXPECTED;
        }

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

            _threadPaused = true;
            _deliveryFrameNb = DELIVER_FRAME_NB_RESET;
            _deliveryThread = std::thread(&CAviSynthFilter::DeliveryThreadProc, this);
        }
    }

    return S_OK;
}

auto CAviSynthFilter::Transform(IMediaSample *pIn, IMediaSample *pOut) -> HRESULT {
    HRESULT hr;

    const REFERENCE_TIME streamTime = GetStreamTime();

    REFERENCE_TIME inStartTime, inStopTime;
    CheckHr(pIn->GetTime(&inStartTime, &inStopTime));

    AM_MEDIA_TYPE *inMediaType;
    if (pIn->GetMediaType(&inMediaType) == S_OK) {
        DeleteMediaType(inMediaType);
        _inBitmapInfo = GetBitmapInfo(m_pInput->CurrentMediaType());
        _reloadAvsFile = true;
    }

    AM_MEDIA_TYPE *outMediaType;
    if (pOut->GetMediaType(&outMediaType) == S_OK) {
        DeleteMediaType(outMediaType);
        _outBitmapInfo = GetBitmapInfo(m_pOutput->CurrentMediaType());
    }

    BYTE *inBuffer;
    CheckHr(pIn->GetPointer(&inBuffer));
    _bufferHandler.CreateFrame(inStartTime, inBuffer, _inBitmapInfo->biWidth, _inBitmapInfo->biHeight, _avsEnv);

    if (_reloadAvsFile) {
        _reloadAvsFile = false;

        StopDelivery();

        if (!CreateScriptClip()) {
            return E_UNEXPECTED;
        }
    }

    _inSampleFrameNb = inStartTime / _timePerFrame;

    if (_deliveryFrameNb == DELIVER_FRAME_NB_RESET) {
        _deliveryFrameNb.exchange(_inSampleFrameNb);
        _threadPaused = false;
    }

    _threadCondition.notify_one();

#ifdef LOGGING
    printf("late: %10i timePerFrame: %lli streamTime: %10lli streamFrameNb: %4lli sampleTime: %10lli sampleFrameNb: %4lli\n",
           m_itrLate, _timePerFrame, streamTime, streamTime / _timePerFrame, inStartTime, inStartTime / _timePerFrame);
    fflush(stdout);
#endif

    /*
     * returning S_OK will cause pOut to be delivered by CVideoTransformFilter::Receive()
     * returning S_FALSE will not deliver pOut, AND send notification about quality change
     * we only want pOut not deliver, thus returning a success code that's neither S_OK or S_FALSE
     */
    return S_FALSE + 1;
}

auto CAviSynthFilter::EndFlush() -> HRESULT {
    StopDelivery();
    return CVideoTransformFilter::EndFlush();
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
    _reloadAvsFile = true;
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

auto CAviSynthFilter::DeliveryThreadProc() -> void {
    IMediaSample *pOut = nullptr;

    while (true) {
        std::unique_lock<std::mutex> lock(_threadMutex);

        _threadCondition.wait(lock, [&] {
            if (_threadShutdown) {
                return true;
            }

            if (_threadPaused) {
                return false;
            }

            return _deliveryFrameNb <= _inSampleFrameNb;
        });

        if (_threadShutdown) {
            break;
        }

        if (FAILED(InitializeOutputSample(nullptr, &pOut))) {
            break;
        }

        REFERENCE_TIME outStartTime = _deliveryFrameNb * _timePerFrame;
        REFERENCE_TIME outStopTime = outStartTime + _timePerFrame;
        if (FAILED(pOut->SetTime(&outStartTime, &outStopTime))) {
            break;
        }

        BYTE *outBuffer;
        if (FAILED(pOut->GetPointer(&outBuffer))) {
            break;
        }

        const PVideoFrame clipFrame = _avsScriptClip->GetFrame(_deliveryFrameNb, _avsEnv);
        _bufferHandler.WriteSample(clipFrame, outBuffer, _outBitmapInfo->biWidth, _outBitmapInfo->biHeight, _avsEnv);

        if (FAILED(m_pOutput->Deliver(pOut))) {
            break;
        }

        pOut->Release();
        pOut = nullptr;

        _bufferHandler.GarbageCollect(GetStreamTime());

#ifdef LOGGING
        printf("Delivery frameNb: %4i at %10lli inSampleFrameNb: %4i\n", _deliveryFrameNb.load(), outStartTime, _inSampleFrameNb.load());
#endif

        _deliveryFrameNb += 1;
    }

    if (pOut != nullptr) {
        pOut->Release();
    }
}

auto CAviSynthFilter::StopDelivery() -> void {
    _threadPaused = true;
    _bufferHandler.StartDraining();

    {
        std::lock_guard<std::mutex> lock(_threadMutex);
        _bufferHandler.StopDraining();
        _deliveryFrameNb = DELIVER_FRAME_NB_RESET;
    }
}

/**
 * Create new AviSynth script clip with current input media type.
 */
auto CAviSynthFilter::CreateScriptClip() -> bool {
    if (_avsScriptClip != nullptr) {
        _avsScriptClip = nullptr;
    }

    UpdateSourceVideoInfo();

    AVSValue invokeResult;
    std::string errorScript;
    try {
        if (_avsFile.empty()) {
            AVSValue evalArgs[] = { AVSValue("return avsfilter_source()")
                                  , AVSValue(EVAL_FILENAME) };
            invokeResult = _avsEnv->Invoke("Eval", AVSValue(evalArgs, 2), nullptr);
        } else {
            invokeResult = _avsEnv->Invoke("Import", AVSValue(_avsFile.c_str()), nullptr);
        }

        if (!invokeResult.Defined()) {
            return false;
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
    _timePerFrame = _avsScriptVideoInfo.fps_denominator * UNITS / _avsScriptVideoInfo.fps_numerator;
    _bufferHandler.Reset(m_pInput->CurrentMediaType().subtype, &_avsScriptVideoInfo);

    return true;
}

/**
 * Update AVS video info from current input media type.
 */
auto CAviSynthFilter::UpdateSourceVideoInfo() -> void {
    CMediaType &mediaType = m_pInput->CurrentMediaType();
    BITMAPINFOHEADER *bitmapInfo = GetBitmapInfo(mediaType);
    const VIDEOINFOHEADER *vih = reinterpret_cast<VIDEOINFOHEADER *>(mediaType.pbFormat);
    const REFERENCE_TIME frameTime = vih->AvgTimePerFrame > 0 ? vih->AvgTimePerFrame : DEFAULT_AVG_TIME_PER_FRAME;

    _avsSourceVideoInfo = {};
    _avsSourceVideoInfo.width = bitmapInfo->biWidth;
    _avsSourceVideoInfo.height = abs(bitmapInfo->biHeight);
    _avsSourceVideoInfo.fps_numerator = UNITS;
    _avsSourceVideoInfo.fps_denominator = frameTime;
    _avsSourceVideoInfo.num_frames = NUM_FRAMES_FOR_INFINITE_STREAM;

    const int formatIndex = Format::LookupInput(mediaType.subtype);
    _avsSourceVideoInfo.pixel_type = Format::FORMATS[formatIndex].avs;
}

auto CAviSynthFilter::GetStreamTime() -> REFERENCE_TIME {
    CRefTime streamTime;
    if (FAILED(StreamTime(streamTime))) {
        return -1;
    }
    return min(streamTime, m_tStart);
}