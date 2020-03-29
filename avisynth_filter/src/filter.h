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
    auto CheckConnect(PIN_DIRECTION direction, IPin *pPin) -> HRESULT override;
    auto BreakConnect(PIN_DIRECTION direction) -> HRESULT override;
    auto CheckInputType(const CMediaType *mtIn) -> HRESULT override;
    auto GetMediaType(int iPosition, CMediaType *pMediaType) -> HRESULT override;
    auto CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut) -> HRESULT override;
    auto DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProperties) -> HRESULT override;
    auto CompleteConnect(PIN_DIRECTION direction, IPin *pReceivePin) -> HRESULT override;
    auto StartStreaming() -> HRESULT override;
    auto Transform(IMediaSample *pIn, IMediaSample *pOut) -> HRESULT override;
    auto BeginFlush() -> HRESULT override;
    
    auto STDMETHODCALLTYPE Pause() -> HRESULT override;

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
    struct DefinitionPair {
        int input;
        int output;
    };

    static auto MediaTypeToDefinition(const AM_MEDIA_TYPE *mediaType) -> int;

    auto LoadSettings() -> void;
    auto GetInputDefinition(const AM_MEDIA_TYPE *mediaType) const -> int;
    auto GenerateMediaType(int definition, const AM_MEDIA_TYPE *templateMediaType) const -> AM_MEDIA_TYPE *;
    auto DeletePinTypes() -> void;
    auto ReloadAviSynth() -> void;
    auto ReloadAviSynth(const AM_MEDIA_TYPE *mediaType, bool allowDisconnect) -> bool;
    auto DeleteAviSynth() -> void;

    auto IsInputUniqueByAvsType(int inputDefinition) const -> bool;
    auto FindCompatibleInputByOutput(int outputDefinition) const -> int;

    // filter related variables

    BufferHandler _bufferHandler;

    IScriptEnvironment2 *_avsEnv;
    PClip _avsScriptClip;

    VideoInfo _avsSourceVideoInfo;
    VideoInfo _avsScriptVideoInfo;

    std::unordered_map<int, AM_MEDIA_TYPE *> _acceptableInputTypes;
    std::unordered_map<int, AM_MEDIA_TYPE *> _acceptableOuputTypes;
    std::vector<DefinitionPair> _compatibleDefinitions;

    Format::VideoFormat _inputFormat;
    Format::VideoFormat _outputFormat;

    REFERENCE_TIME _timePerFrame;
    int _deliveryFrameNb;
    bool _reloadAvsFile;

    // settings related variables

    Registry _registry;

    std::string _avsFile;
    int _bufferBack;
    int _bufferAhead;
    DWORD _inputFormatBits;
};
