#pragma once

#include "pch.h"
#include "buffer_handler.h"
#include "format.h"
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
    auto CompleteConnect(PIN_DIRECTION direction, IPin *pReceivePin) -> HRESULT override;
    auto StartStreaming() -> HRESULT override;
    auto Transform(IMediaSample *pIn, IMediaSample *pOut) -> HRESULT override;
    auto BeginFlush() -> HRESULT override;

    // ISpecifyPropertyPages
    auto STDMETHODCALLTYPE GetPages(CAUUID *pPages) -> HRESULT override;

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
    auto STDMETHODCALLTYPE GetInputFormats() const ->DWORD override;
    auto STDMETHODCALLTYPE SetInputFormats(DWORD formatBits) -> void override;

private:
    struct IndexedMediaType {
        int formatIndex;
        AM_MEDIA_TYPE *mediaType;
    };

    static auto IsTypeExistForIndex(const std::vector<IndexedMediaType> &container, int formatIndex) -> bool;
    static auto FindFirstMatchingType(const std::vector<IndexedMediaType> &container, int formatIndex) -> const IndexedMediaType *;

    auto LoadSettings() -> void;
    auto ValidateMediaType(PIN_DIRECTION direction, const AM_MEDIA_TYPE *mediaType) const -> HRESULT;
    auto GenerateMediaType(int formatIndex, const AM_MEDIA_TYPE *templateMediaType) const -> AM_MEDIA_TYPE *;
    auto DeleteIndexedMediaTypes() -> void;
    auto DeleteAviSynth() -> void;
    auto ReloadAviSynth() -> void;
    auto ReloadAviSynth(int forceFormatIndex) -> void;

    // filter related variables

    BufferHandler _bufferHandler;

    IScriptEnvironment2 *_avsEnv;
    PClip _avsScriptClip;

    VideoInfo _avsSourceVideoInfo;
    VideoInfo _avsScriptVideoInfo;

    bool _isConnectingPins;
    std::vector<IndexedMediaType> _indexedInputTypes;
    std::vector<IndexedMediaType> _acceptableOutputTypes;

    Format::MediaTypeInfo _inputMediaTypeInfo;
    Format::MediaTypeInfo _outputMediaTypeInfo;

    REFERENCE_TIME _timePerFrame;

    int _inSampleFrameNb;
    int _deliveryFrameNb;

    std::mutex _avsMutex;

    // settings related variables

    Registry _registry;

    std::string _avsFile;
    int _bufferBack;
    int _bufferAhead;
    DWORD _inputFormatBits;

    bool _reloadAvsFile;
};
