#pragma once

#include "pch.h"
#include "api.h"
#include "format.h"
#include "frame_handler.h"
#include "remote_control.h"


namespace AvsFilter {

class CAviSynthFilter
    : public CVideoTransformFilter
    , public ISpecifyPropertyPages {

    friend class CAviSynthFilterInputPin;
    friend class FrameHandler;

public:
    CAviSynthFilter(LPUNKNOWN pUnk, HRESULT *phr);

    DECLARE_IUNKNOWN

    auto STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid, __deref_out void **ppv) -> HRESULT override;
    auto STDMETHODCALLTYPE NonDelegatingRelease() -> ULONG override;

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

    auto GetInputFormat() const -> Format::VideoFormat;
    auto GetOutputFormat() const -> Format::VideoFormat;
    auto GetEffectiveAvsFile() const -> std::wstring;
    auto ReloadAvsFile(const std::wstring &avsFile) -> void;
    auto GetVideoSourcePath() const -> std::wstring;
    auto GetVideoFilterNames() const -> std::vector<std::wstring>;
    auto GetAvsState() const -> AvsState;

    FrameHandler frameHandler;
    
private:
    struct MediaTypeDeleter {
        auto operator()(AM_MEDIA_TYPE *mediaType) const -> void {
            DeleteMediaType(mediaType);
        }
    };
    using UniqueMediaTypePtr = std::unique_ptr<AM_MEDIA_TYPE, MediaTypeDeleter>;

    struct DefinitionPair {
        int input;
        int output;
    };

    static auto MediaTypeToDefinition(const AM_MEDIA_TYPE *mediaType) -> std::optional<int>;
    static auto GetInputDefinition(const AM_MEDIA_TYPE *mediaType) -> std::optional<int>;

    auto UpdateOutputFormat(const AM_MEDIA_TYPE &inputMediaType) -> HRESULT;
    auto HandleOutputFormatChange(const AM_MEDIA_TYPE *pmtOut) -> HRESULT;

    auto TraverseFiltersInGraph() -> void;

    auto IsInputUniqueByAvsType(int inputDefinition) const -> bool;
    auto FindCompatibleInputByOutput(int outputDefinition) const -> std::optional<int>;

    std::optional<RemoteControl> _remoteControl;

    bool _disconnectFilter;
    std::vector<UniqueMediaTypePtr> _acceptableInputTypes;
    std::vector<UniqueMediaTypePtr> _acceptableOutputTypes;
    std::vector<DefinitionPair> _compatibleDefinitions;

    Format::VideoFormat _inputFormat;
    Format::VideoFormat _outputFormat;
    bool _confirmNewOutputFormat;

    std::wstring _effectiveAvsFile;
    bool _reloadAvsSource;

    std::wstring _videoSourcePath;
    std::vector<std::wstring> _videoFilterNames;
};

}
