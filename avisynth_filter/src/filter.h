#pragma once

#include "pch.h"
#include "format.h"
#include "interfaces.h"
#include "remote_control.h"
#include "frame_handler.h"


namespace AvsFilter {

class CAviSynthFilter
    : public CVideoTransformFilter
    , public ISpecifyPropertyPages
    , public IAvsFilterSettings
    , public IAvsFilterStatus {
    friend class CAviSynthFilterInputPin;
    friend class FrameHandler;

public:
    CAviSynthFilter(LPUNKNOWN pUnk, HRESULT *phr);
    virtual ~CAviSynthFilter();

    DECLARE_IUNKNOWN

    auto STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid, __deref_out void **ppv) -> HRESULT override;

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
    auto STDMETHODCALLTYPE GetPages(__RPC__out CAUUID *pPages) -> HRESULT override;

    // IAvsFilterSettings
    auto STDMETHODCALLTYPE GetEffectiveAvsFile() const -> std::wstring override;
    auto STDMETHODCALLTYPE SetEffectiveAvsFile(const std::wstring &avsFile) -> void override;
    auto STDMETHODCALLTYPE ReloadAvsSource() -> void override;
    auto STDMETHODCALLTYPE GetAvsVersionString() -> const char * override;

    // IAvsFilterStatus
    auto STDMETHODCALLTYPE GetInputBufferSize() const -> int override;
    auto STDMETHODCALLTYPE GetOutputBufferSize() const -> int override;
    auto STDMETHODCALLTYPE GetSourceSampleNumber() const -> int override;
    auto STDMETHODCALLTYPE GetOutputSampleNumber() const -> int override;
    auto STDMETHODCALLTYPE GetDeliveryFrameNumber() const -> int override;
    auto STDMETHODCALLTYPE GetCurrentInputFrameRate() const -> int override;
    auto STDMETHODCALLTYPE GetCurrentOutputFrameRate() const -> int override;
    auto STDMETHODCALLTYPE GetOutputWorkerThreadCount() const -> int override;

    auto STDMETHODCALLTYPE GetVideoSourcePath() const -> std::wstring override;
    auto STDMETHODCALLTYPE GetInputMediaInfo() const -> Format::VideoFormat override;

    auto STDMETHODCALLTYPE GetVideoFilterNames() const -> std::vector<std::wstring> override;
    auto STDMETHODCALLTYPE GetSourceAvgFrameRate() const -> int override;
    auto STDMETHODCALLTYPE GetAvsState() const -> AvsState override;
    auto STDMETHODCALLTYPE GetAvsError() const -> std::optional<std::string> override;

private:
    struct DefinitionPair {
        int input;
        int output;
    };

    static auto MediaTypeToDefinition(const AM_MEDIA_TYPE *mediaType) -> std::optional<int>;
    static auto GetInputDefinition(const AM_MEDIA_TYPE *mediaType) -> std::optional<int>;

    auto UpdateOutputFormat(const AM_MEDIA_TYPE &inputMediaType) -> HRESULT;
    auto HandleOutputFormatChange(const AM_MEDIA_TYPE *pmtOut) -> HRESULT;

    auto TraverseFiltersInGraph() -> void;
    auto GenerateMediaType(int definition, const AM_MEDIA_TYPE *templateMediaType) const -> AM_MEDIA_TYPE *;
    auto DeletePinTypes() -> void;
    auto CreateAviSynth() -> bool;
    auto ReloadAviSynthScript(const AM_MEDIA_TYPE &mediaType) -> bool;
    auto StopAviSynthScript() -> void;
    auto DeleteAviSynth() -> void;

    auto IsInputUniqueByAvsType(int inputDefinition) const -> bool;
    auto FindCompatibleInputByOutput(int outputDefinition) const -> std::optional<int>;

    std::wstring _effectiveAvsFile;
    std::optional<RemoteControl> _remoteControl;

    FrameHandler _frameHandler;

    bool _disconnectFilter;
    std::vector<AM_MEDIA_TYPE *> _acceptableInputTypes;
    std::vector<AM_MEDIA_TYPE *> _acceptableOutputTypes;
    std::vector<DefinitionPair> _compatibleDefinitions;

    IScriptEnvironment2 *_avsEnv;
    PClip _avsSourceClip;
    PClip _avsScriptClip;
    const char *_avsVersionString;

    bool _reloadAvsSource;
    VideoInfo _avsSourceVideoInfo;
    VideoInfo _avsScriptVideoInfo;
    int _sourceAvgFrameRate;
    REFERENCE_TIME _sourceAvgFrameTime;
    REFERENCE_TIME _scriptAvgFrameTime;

    Format::VideoFormat _inputFormat;
    Format::VideoFormat _outputFormat;
    bool _confirmNewOutputFormat;

    std::wstring _videoSourcePath;
    std::vector<std::wstring> _videoFilterNames;
    std::string _avsError;
};

}
