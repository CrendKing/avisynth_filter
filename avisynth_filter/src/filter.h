// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

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

    auto GetInputFormat() const -> Format::VideoFormat;
    auto GetOutputFormat() const -> Format::VideoFormat;
    auto ReloadAvsFile(const std::filesystem::path &avsPath) -> void;
    auto GetVideoSourcePath() const -> const std::filesystem::path &;
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

    struct MediaTypePair {
        CMediaType input;
        CMediaType output;
    };

    static auto MediaTypeToFormatName(const AM_MEDIA_TYPE *mediaType) -> std::optional<std::wstring>;
    static auto GetInputFormatName(const AM_MEDIA_TYPE *mediaType) -> std::optional<std::wstring>;
    static auto FindFirstVideoOutputPin(IBaseFilter *pFilter) -> std::optional<IPin *>;

    auto UpdateOutputFormat(const AM_MEDIA_TYPE &inputMediaType) -> HRESULT;
    auto HandleOutputFormatChange(const AM_MEDIA_TYPE &outputMediaType) -> HRESULT;
    auto TraverseFiltersInGraph() -> void;

    std::optional<RemoteControl> _remoteControl;

    bool _disconnectFilter;
    std::vector<MediaTypePair> _compatibleMediaTypes;
    int _mediaTypeReconnectionWatermark;

    Format::VideoFormat _inputFormat;
    Format::VideoFormat _outputFormat;
    bool _sendOutputFormatInNextSample;

    bool _reloadAvsSource;

    std::filesystem::path _videoSourcePath;
    std::vector<std::wstring> _videoFilterNames;
};

}
