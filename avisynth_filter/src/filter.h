#pragma once

#include "pch.h"

#include <atomic>

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
    auto GetMediaType(int iPosition, CMediaType *pMediaType)->HRESULT override;
    auto CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut) -> HRESULT override;
    auto DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProperties) -> HRESULT override;
    auto CompleteConnect(PIN_DIRECTION dir, IPin *pReceivePin) -> HRESULT override;
    auto Transform(IMediaSample *pIn, IMediaSample *pOut)->HRESULT override;
    auto EndFlush()->HRESULT override;

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

    auto DeliveryThreadProc() -> void;
    auto StopDelivery() -> void;
    auto CreateScriptClip() -> bool;
    auto UpdateSourceVideoInfo() -> void;
    auto GetStreamTime() -> REFERENCE_TIME;

    BufferHandler _bufferHandler;

    IScriptEnvironment2 *_avsEnv;
    PClip _avsScriptClip;

    VideoInfo _avsSourceVideoInfo;
    VideoInfo _avsScriptVideoInfo;

    std::vector<MediaTypeFormat> _upstreamTypes;

    const BITMAPINFOHEADER *_inBitmapInfo;
    const BITMAPINFOHEADER *_outBitmapInfo;

    REFERENCE_TIME _timePerFrame;

    std::thread _deliveryThread;
    std::mutex _threadMutex;
    std::condition_variable _threadCondition;

    std::atomic<bool> _threadPaused;
    std::atomic<bool> _threadShutdown;
    std::atomic<int> _deliveryFrameNb;
    std::atomic<int> _inSampleFrameNb;

    bool _rejectConnection;
    bool _reloadAvsFile;

    Registry _registry;
    std::string _avsFile;
};
