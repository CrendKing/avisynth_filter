#pragma once

#include "pch.h"
#include "buffer_handler.h"
#include "settings.h"
#include "registry.h"


class CAviSynthFilter
    : public CVideoTransformFilter
    , public ISpecifyPropertyPages
    , public IAvsFilterSettings {
public:
    CAviSynthFilter(LPUNKNOWN pUnk, HRESULT *phr);
    ~CAviSynthFilter();

    DECLARE_IUNKNOWN

    auto STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid, void **ppv) -> HRESULT override;

    // CVideoTransformFilter
    auto CheckInputType(const CMediaType *mtIn) -> HRESULT override;
    auto GetMediaType(int iPosition, CMediaType *pMediaType) -> HRESULT override;
    auto CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut) -> HRESULT override;
    auto DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProperties) -> HRESULT override;
    auto CompleteConnect(PIN_DIRECTION dir, IPin *pReceivePin) -> HRESULT override;
    auto Transform(IMediaSample *pIn, IMediaSample *pOut) -> HRESULT override;
    auto BeginFlush() -> HRESULT override;

    // ISpecifyPropertyPages
    auto STDMETHODCALLTYPE GetPages(CAUUID *pPages)->HRESULT override;

    // IAvsFilterSettings
    auto STDMETHODCALLTYPE SaveSettings() const -> void override;
    auto STDMETHODCALLTYPE GetAvsFile() const -> const std::string & override;
    auto STDMETHODCALLTYPE SetAvsFile(const std::string &avsFile) -> void override;
    auto STDMETHODCALLTYPE GetReloadAvsFile() const -> bool override;
    auto STDMETHODCALLTYPE SetReloadAvsFile(bool reload) -> void override;
    auto STDMETHODCALLTYPE GetBufferBack() const -> int override;
    auto STDMETHODCALLTYPE SetBufferBack(int bufferBack) -> void override;
    auto STDMETHODCALLTYPE GetBufferAhead() const -> int override;
    auto STDMETHODCALLTYPE SetBufferAhead(int bufferAhead) -> void override;
    auto STDMETHODCALLTYPE GetSupportedFormats() const -> const std::unordered_set<int> & override;
    auto STDMETHODCALLTYPE SetSupportedFormats(const std::unordered_set<int> &formatIndices) -> void override;

private:
    struct MediaTypeFormat {
        AM_MEDIA_TYPE *mediaType;
        int formatIndex;
    };

    static auto GetBitmapInfo(AM_MEDIA_TYPE &mediaType) -> BITMAPINFOHEADER *;

    auto LoadSettings() -> void;
    auto ValidateMediaType(const AM_MEDIA_TYPE *mediaType, PIN_DIRECTION dir) const -> HRESULT;
    auto DeleteAviSynth() -> void;
    auto ReloadAviSynth() -> void;
    auto UpdateSourceVideoInfo() -> void;

    // filter related variables

    BufferHandler _bufferHandler;

    IScriptEnvironment2 *_avsEnv;
    PClip _avsScriptClip;

    VideoInfo _avsSourceVideoInfo;
    VideoInfo _avsScriptVideoInfo;

    std::vector<MediaTypeFormat> _upstreamTypes;
    bool _isConnectingPins;

    const BITMAPINFOHEADER *_inBitmapInfo;
    const BITMAPINFOHEADER *_outBitmapInfo;

    REFERENCE_TIME _timePerFrame;

    int _inSampleFrameNb;
    int _deliveryFrameNb;

    std::mutex _avsMutex;

    // settings related variables

    Registry _registry;

    std::string _avsFile;
    int _bufferBack;
    int _bufferAhead;
    std::unordered_set<int> _supportedFormats;

    bool _reloadAvsFile;
};
