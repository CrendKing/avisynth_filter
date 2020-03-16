#pragma once

#include "pch.h"
#include "buffer_handler.h"
#include "registry.h"
#include "avs_file.h"


class CAviSynthFilter
    : public CVideoTransformFilter
    , public IAvsFile
    , public ISpecifyPropertyPages {
public:
    static auto CALLBACK CreateInstance(LPUNKNOWN pUnk, HRESULT *phr) -> CUnknown *;

    CAviSynthFilter(LPUNKNOWN pUnk, HRESULT *phr);
    ~CAviSynthFilter();

    DECLARE_IUNKNOWN

    auto STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid, void **ppv) -> HRESULT override;

    auto CheckInputType(const CMediaType *mtIn) -> HRESULT override;
    auto GetMediaType(int iPosition, CMediaType *pMediaType) -> HRESULT override;
    auto CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut) -> HRESULT override;
    auto DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProperties) -> HRESULT override;
    auto CompleteConnect(PIN_DIRECTION dir, IPin *pReceivePin) -> HRESULT override;
    auto NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate) -> HRESULT override;
    auto Transform(IMediaSample *pIn, IMediaSample *pOut) -> HRESULT override;

    auto STDMETHODCALLTYPE GetPages(CAUUID *pPages) -> HRESULT override;
    auto STDMETHODCALLTYPE GetAvsFile(std::string &avsFile) const -> HRESULT override;
    auto STDMETHODCALLTYPE UpdateAvsFile(const std::string &avsFile) -> HRESULT override;
    auto STDMETHODCALLTYPE ReloadAvsFile() -> HRESULT override;

private:
    struct MediaTypeFormat {
        AM_MEDIA_TYPE *mediaType;
        int formatIndex;
    };

    static auto ValidateMediaType(const AM_MEDIA_TYPE *mediaType) -> HRESULT;
    static auto GetBitmapInfo(AM_MEDIA_TYPE &mediaType) -> BITMAPINFOHEADER *;
    auto CreateScriptClip() -> void;
    auto UpdateAvsVideoInfo() -> void;

    AVS_ScriptEnvironment *_avsEnv;
    AVS_Clip *_scriptClip;
    const AVS_VideoInfo *_avsVideoInfo;

    std::vector<MediaTypeFormat> _upstreamTypes;

    REFERENCE_TIME _segmentDuration;
    REFERENCE_TIME _timePerFrame;
    REFERENCE_TIME _avsRefTime;

    const BITMAPINFOHEADER *_inBitmapInfo;
    const BITMAPINFOHEADER *_outBitmapInfo;

    BufferHandler _bufferHandler;
    AVS_FilterInfo _avsFilter;

    Registry _registry;
    std::string _avsFile;
};