#pragma once

#include "pch.h"
#include "format.h"
#include "interfaces.h"
#include "registry.h"
#include "remote_control.h"
#include "source_clip.h"


namespace AvsFilter {

class CAviSynthFilter
    : public CVideoTransformFilter
    , public ISpecifyPropertyPages
    , public IAvsFilterSettings
    , public IAvsFilterStatus {
    friend class CAviSynthFilterInputPin;

public:
    CAviSynthFilter(LPUNKNOWN pUnk, HRESULT *phr);
    virtual ~CAviSynthFilter();

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

    // ISpecifyPropertyPages
    auto STDMETHODCALLTYPE GetPages(CAUUID *pPages) -> HRESULT override;

    // IAvsFilterSettings
    auto STDMETHODCALLTYPE SaveSettings() const -> void override;
    auto STDMETHODCALLTYPE GetAvsSourceFile() const -> std::wstring override;
    auto STDMETHODCALLTYPE SetAvsSourceFile(const std::wstring &avsSourceFile) -> void override;
    auto STDMETHODCALLTYPE ReloadAvsSource() -> void override;
    auto STDMETHODCALLTYPE GetInputFormats() const -> DWORD override;
    auto STDMETHODCALLTYPE SetInputFormats(DWORD formatBits) -> void override;

    // IAvsFilterStatus
    auto STDMETHODCALLTYPE GetBufferSize() -> int override;
    auto STDMETHODCALLTYPE GetCurrentPrefetch() const -> int override;
    auto STDMETHODCALLTYPE GetInitialPrefetch() const -> int override;
    auto STDMETHODCALLTYPE GetSourceSampleNumber() const -> int override;
    auto STDMETHODCALLTYPE GetDeliveryFrameNumber() const -> int override;
    auto STDMETHODCALLTYPE GetInputFrameRate() const -> int override;
    auto STDMETHODCALLTYPE GetOutputFrameRate() const -> int override;
    auto STDMETHODCALLTYPE GetVideoSourcePath() const -> std::wstring override;
    auto STDMETHODCALLTYPE GetInputMediaInfo() const -> Format::VideoFormat override;

    auto STDMETHODCALLTYPE GetVideoFilterNames() const -> std::vector<std::wstring> override;
    auto STDMETHODCALLTYPE GetAvsState() const -> AvsState override;
    auto STDMETHODCALLTYPE GetAvsError() const -> std::optional<std::string> override;

private:
    struct DefinitionPair {
        int input;
        int output;
    };

    static auto MediaTypeToDefinition(const AM_MEDIA_TYPE *mediaType) -> std::optional<int>;

    auto TransformAndDeliver(IMediaSample *inSample) -> HRESULT;
    auto UpdateOutputFormat() -> HRESULT;
    auto HandleOutputFormatChange(const AM_MEDIA_TYPE *pmtOut) -> HRESULT;
    auto RefreshFrameRates(REFERENCE_TIME currentSampleStartTime, int currentSampleNb) -> void;

    auto Reset(bool recreateAvsEnv) -> void;
    auto TraverseFiltersInGraph() -> void;
    auto LoadSettings() -> void;
    auto GetInputDefinition(const AM_MEDIA_TYPE *mediaType) const -> std::optional<int>;
    auto GenerateMediaType(int definition, const AM_MEDIA_TYPE *templateMediaType) const -> AM_MEDIA_TYPE *;
    auto DeletePinTypes() -> void;
    auto CreateAviSynth() -> bool;
    auto ReloadAviSynth(const AM_MEDIA_TYPE &mediaType, bool recreateAvsEnv) -> bool;
    auto DeleteAviSynth() -> void;

    auto IsInputUniqueByAvsType(int inputDefinition) const -> bool;
    auto FindCompatibleInputByOutput(int outputDefinition) const -> std::optional<int>;

    IScriptEnvironment2 *_avsEnv;
    SourceClip *_sourceClip;
    PClip _avsScriptClip;

    VideoInfo _avsSourceVideoInfo;
    VideoInfo _avsScriptVideoInfo;
    double _frameTimeScaling;

    std::vector<AM_MEDIA_TYPE *> _acceptableInputTypes;
    std::vector<AM_MEDIA_TYPE *> _acceptableOutputTypes;
    std::vector<DefinitionPair> _compatibleDefinitions;

    Format::VideoFormat _inputFormat;
    Format::VideoFormat _outputFormat;

    std::wstring _avsSourceFile;
    bool _reloadAvsSource;
    RemoteControl *_remoteControl;

    REFERENCE_TIME _deliveryFrameStartTime;
    int _deliveryFrameNb;
    int _deliverySourceSampleNb;
    bool _confirmNewOutputFormat;

    int _currentPrefetch;
    int _initialPrefetch;

    REFERENCE_TIME _frameRateCheckpointInSampleStartTime;
    int _frameRateCheckpointInSampleNb;
    REFERENCE_TIME _frameRateCheckpointOutFrameStartTime;
    int _frameRateCheckpointOutFrameNb;
    int _inputFrameRate;
    int _outputFrameRate;

    std::wstring _videoSourcePath;
    std::vector<std::wstring> _videoFilterNames;
    std::string _avsError;

    Registry _registry;
    DWORD _inputFormatBits;
};

}