#pragma once

#include "pch.h"
#include "buffer_handler.h"
#include "settings.h"


class CAviSynthFilter
    : public CVideoTransformFilter
    , public ISpecifyPropertyPages {
    friend class CAvsFilterSettings;

public:
    static auto CALLBACK CreateInstance(LPUNKNOWN pUnk, HRESULT *phr) -> CUnknown *;

    CAviSynthFilter(LPUNKNOWN pUnk, HRESULT *phr);

    DECLARE_IUNKNOWN

    auto STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid, void **ppv) -> HRESULT override;

    auto CheckInputType(const CMediaType *mtIn) -> HRESULT override;
    auto GetMediaType(int iPosition, CMediaType *pMediaType) -> HRESULT override;
    auto CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut) -> HRESULT override;
    auto DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProperties) -> HRESULT override;
    auto CompleteConnect(PIN_DIRECTION dir, IPin *pReceivePin) -> HRESULT override;
    auto Transform(IMediaSample *pIn, IMediaSample *pOut) -> HRESULT override;
    auto BeginFlush()->HRESULT override;
    auto STDMETHODCALLTYPE Stop() -> HRESULT override;

    auto STDMETHODCALLTYPE GetPages(CAUUID *pPages) -> HRESULT override;

private:
    struct MediaTypeFormat {
        AM_MEDIA_TYPE *mediaType;
        int formatIndex;
    };

    static auto ValidateMediaType(const AM_MEDIA_TYPE *mediaType) -> HRESULT;
    static auto GetBitmapInfo(AM_MEDIA_TYPE &mediaType) -> BITMAPINFOHEADER *;

    auto DeliveryThreadProc() -> void;
    auto StopDelivery() -> void;
    auto CreateScriptClip() -> bool;
    auto UpdateSourceVideoInfo() -> void;

    CAvsFilterSettings _settings;
    BufferHandler _bufferHandler;

    IScriptEnvironment2 *_avsEnv;
    PClip _avsScriptClip;

    VideoInfo _avsSourceVideoInfo;
    VideoInfo _avsScriptVideoInfo;

    std::vector<MediaTypeFormat> _upstreamTypes;

    const BITMAPINFOHEADER *_inBitmapInfo;
    const BITMAPINFOHEADER *_outBitmapInfo;

    REFERENCE_TIME _timePerFrame;

    std::mutex _transformMutex;
    std::condition_variable _transformCondition;

    std::thread _deliveryThread;
    std::mutex _threadMutex;
    std::condition_variable _threadCondition;

    std::atomic<bool> _shutdown;
    std::atomic<bool> _deliveryPaused;
    std::atomic<int> _deliveryFrameNb;
    std::atomic<int> _inSampleFrameNb;

    bool _rejectConnection;
    bool _reloadAvsFile;
};
