#pragma once

#include "pch.h"
#include "frame_handler.h"
#include "format.h"
#include "interfaces.h"
#include "registry.h"

class RemoteControl;

class CAviSynthFilterInputPin : public CTransformInputPin {
    friend class CAviSynthFilter;

public:
    CAviSynthFilterInputPin(__in_opt LPCTSTR pObjectName,
                            __inout CTransformFilter *pTransformFilter,
                            __inout HRESULT *phr,
                            __in_opt LPCWSTR pName);
    auto STDMETHODCALLTYPE ReceiveConnection(IPin *pConnector, const AM_MEDIA_TYPE *pmt) -> HRESULT override;
};

class CAviSynthFilter
    : public CVideoTransformFilter
    , public ISpecifyPropertyPages
    , public IAvsFilterSettings
    , public IAvsFilterStatus {
    friend class CAviSynthFilterInputPin;

public:
    CAviSynthFilter(LPUNKNOWN pUnk, HRESULT *phr);
    ~CAviSynthFilter();

    DECLARE_IUNKNOWN

    auto STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid, void **ppv) -> HRESULT override;

    // CVideoTransformFilter
    auto GetPin(int n) -> CBasePin * override;
    auto CheckConnect(PIN_DIRECTION direction, IPin *pPin) -> HRESULT override;
    auto CheckInputType(const CMediaType *mtIn) -> HRESULT override;
    auto GetMediaType(int iPosition, CMediaType *pMediaType) -> HRESULT override;
    auto CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut) -> HRESULT override;
    auto DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProperties) -> HRESULT override;
    auto CompleteConnect(PIN_DIRECTION direction, IPin *pReceivePin) -> HRESULT override;
    auto Receive(IMediaSample *pSample) -> HRESULT override;
    auto EndFlush() -> HRESULT override;

    auto STDMETHODCALLTYPE Pause() -> HRESULT override;

    // ISpecifyPropertyPages
    auto STDMETHODCALLTYPE GetPages(CAUUID *pPages) -> HRESULT override;

    // IAvsFilterSettings
    auto STDMETHODCALLTYPE SaveSettings() const -> void override;
    auto STDMETHODCALLTYPE GetAvsFile() const -> const std::wstring & override;
    auto STDMETHODCALLTYPE SetAvsFile(const std::wstring &avsFile) -> void override;
    auto STDMETHODCALLTYPE ReloadAvsFile() -> void override;
    auto STDMETHODCALLTYPE IsRemoteControlled() -> bool override;
    auto STDMETHODCALLTYPE GetInputFormats() const -> DWORD override;
    auto STDMETHODCALLTYPE SetInputFormats(DWORD formatBits) -> void override;

    // IAvsFilterStatus
    auto STDMETHODCALLTYPE GetBufferSize() -> int override;
    auto STDMETHODCALLTYPE GetBufferUnderflowAhead() const -> int override;
    auto STDMETHODCALLTYPE GetBufferUnderflowBack() const -> int override;
    auto STDMETHODCALLTYPE GetSampleTimeOffset() const -> int override;
    auto STDMETHODCALLTYPE GetFrameNumbers() const -> std::pair<int, int> override;
    auto STDMETHODCALLTYPE GetSourcePath() const -> std::wstring override;
    auto STDMETHODCALLTYPE GetInputFrameRate() const -> double override;
    auto STDMETHODCALLTYPE GetOutputFrameRate() const -> double override;
    auto STDMETHODCALLTYPE GetMediaInfo() const -> const Format::VideoFormat * override;

private:
    struct DefinitionPair {
        int input;
        int output;
    };

    struct SampleTimeInfo {
        REFERENCE_TIME startTime;
        REFERENCE_TIME stopTime;
    };

    static auto MediaTypeToDefinition(const AM_MEDIA_TYPE *mediaType) -> int;
    static auto RetrieveSourcePath(IFilterGraph *graph) -> std::wstring;

    auto TransformAndDeliver(IMediaSample *pIn, bool reloadedAvsForFormatChange, bool confirmNewOutputFormat) -> HRESULT;
    auto HandleInputFormatChange(const AM_MEDIA_TYPE *pmt, bool force = false) -> HRESULT;
    auto HandleOutputFormatChange(const AM_MEDIA_TYPE *pmtOut) -> HRESULT;

    auto Reset() -> void;
    auto LoadSettings() -> void;
    auto GetInputDefinition(const AM_MEDIA_TYPE *mediaType) const -> int;
    auto GenerateMediaType(int definition, const AM_MEDIA_TYPE *templateMediaType) const -> AM_MEDIA_TYPE *;
    auto DeletePinTypes() -> void;
    auto CreateAviSynth() -> void;
    auto ReloadAviSynth(const AM_MEDIA_TYPE &mediaType, bool recreateEnv) -> bool;
    auto DeleteAviSynth() -> void;

    auto IsInputUniqueByAvsType(int inputDefinition) const -> bool;
    auto FindCompatibleInputByOutput(int outputDefinition) const -> int;

    FrameHandler _frameHandler;
    std::map<int, SampleTimeInfo> _sampleTimes;

    IScriptEnvironment2 *_avsEnv;
    PClip _avsScriptClip;

    VideoInfo _avsSourceVideoInfo;
    VideoInfo _avsScriptVideoInfo;
    double _frameTimeScaling;
    REFERENCE_TIME _timePerFrame;

    std::unordered_map<int, AM_MEDIA_TYPE *> _acceptableInputTypes;
    std::unordered_map<int, AM_MEDIA_TYPE *> _acceptableOuputTypes;
    std::vector<DefinitionPair> _compatibleDefinitions;

    Format::VideoFormat _inputFormat;
    Format::VideoFormat _outputFormat;

    bool _reloadAvsEnvFlag;
    bool _reloadAvsFileFlag;
    std::wstring _sourcePath;

    int _inputSampleNb;
    int _deliveryFrameNb;
    REFERENCE_TIME _deliveryFrameStartTime;
    int _sampleTimeOffset;

    int _bufferUnderflowAhead;
    int _bufferUnderflowBack;
    int _maxBufferUnderflowAhead;

    REFERENCE_TIME _statsStreamTime;
    double _inputFrameRate;
    double _outputFrameRate;
    int _prevInputSampleNb;
    int _prevDeliveryFrameNb;

    // settings related variables

    Registry _registry;

    std::wstring _avsFile;
    DWORD _inputFormatBits;

    RemoteControl* _remoteControl;
};