#pragma once

#include "pch.h"
#include "buffer_handler.h"
#include "settings.h"


class CAviSynthFilter
    : public CVideoTransformFilter
    , public ISpecifyPropertyPages {
public:
    static auto CALLBACK CreateInstance(LPUNKNOWN pUnk, HRESULT *phr) -> CUnknown *;

    CAviSynthFilter(LPUNKNOWN pUnk, HRESULT *phr);
    ~CAviSynthFilter();

    DECLARE_IUNKNOWN

    auto STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid, void **ppv) -> HRESULT override;
    auto STDMETHODCALLTYPE GetPages(CAUUID *pPages)->HRESULT override;

    auto CheckInputType(const CMediaType *mtIn) -> HRESULT override;
    auto GetMediaType(int iPosition, CMediaType *pMediaType) -> HRESULT override;
    auto CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut) -> HRESULT override;
    auto DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProperties) -> HRESULT override;
    auto CompleteConnect(PIN_DIRECTION dir, IPin *pReceivePin) -> HRESULT override;
    auto Transform(IMediaSample *pIn, IMediaSample *pOut) -> HRESULT override;
    auto BeginFlush() -> HRESULT override;

private:
    struct MediaTypeFormat {
        AM_MEDIA_TYPE *mediaType;
        int formatIndex;
    };

    static auto GetBitmapInfo(AM_MEDIA_TYPE &mediaType) -> BITMAPINFOHEADER *;

    auto ValidateMediaType(const AM_MEDIA_TYPE *mediaType, PIN_DIRECTION dir) const -> HRESULT;
    auto CreateScriptClip() -> bool;
    auto UpdateSourceVideoInfo() -> void;

    CAvsFilterSettings _settings;
    BufferHandler _bufferHandler;

    IScriptEnvironment2 *_avsEnv;
    PClip _avsScriptClip;

    VideoInfo _avsSourceVideoInfo;
    VideoInfo _avsScriptVideoInfo;

    std::vector<MediaTypeFormat> _upstreamTypes;
    bool _rejectConnection;

    const BITMAPINFOHEADER *_inBitmapInfo;
    const BITMAPINFOHEADER *_outBitmapInfo;

    REFERENCE_TIME _timePerFrame;

    int _inSampleFrameNb;
    int _deliveryFrameNb;
};
