// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"
#include "api.h"
#include "avs_handler.h"
#include "format.h"
#include "frame_handler.h"
#include "remote_control.h"
#include "util.h"


namespace AvsFilter {

class CAviSynthFilter
    : public CVideoTransformFilter
    , public ISpecifyPropertyPages {

    friend class CAviSynthFilterInputPin;
    friend class FrameHandler;

public:
    CAviSynthFilter(LPUNKNOWN pUnk, HRESULT *phr);
    ~CAviSynthFilter() override;

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

    constexpr auto GetInputFormat() const -> Format::VideoFormat { return _inputVideoFormat; }
    constexpr auto GetOutputFormat() const -> Format::VideoFormat { return _outputVideoFormat; }
    auto ReloadAvsFile(const std::filesystem::path &avsPath) -> void;
    constexpr auto GetVideoSourcePath() const -> const std::filesystem::path & { return _videoSourcePath; }
    constexpr auto GetVideoFilterNames() const -> const std::vector<std::wstring> & { return _videoFilterNames; }
    auto GetAvsState() const -> AvsState;

    FrameHandler frameHandler;

private:
    struct MediaTypePair {
        const SharedMediaTypePtr &inputType;
        const Format::PixelFormat *inputFormat;

        CMediaType outputType;
        const Format::PixelFormat *outputFormat;
    };

    static auto InputToOutputMediaType(const AM_MEDIA_TYPE *mtIn) {
        g_avs->GetCheckingScriptInstance().ReloadScript(*mtIn, true);
        return Format::LookupAvsType(g_avs->GetCheckingScriptInstance().GetScriptPixelType()) | std::views::transform([mtIn](const Format::PixelFormat &pixelFormat) -> CMediaType {
            return g_avs->GetCheckingScriptInstance().GenerateMediaType(pixelFormat, mtIn);
        });
    }

    static auto MediaTypeToPixelFormat(const AM_MEDIA_TYPE *mediaType) -> const Format::PixelFormat *;
    static auto GetInputPixelFormat(const AM_MEDIA_TYPE *mediaType) -> const Format::PixelFormat *;
    static auto FindFirstVideoOutputPin(IBaseFilter *pFilter) -> std::optional<IPin *>;

    auto UpdateOutputFormat(const AM_MEDIA_TYPE &inputMediaType) -> HRESULT;
    auto HandleOutputFormatChange(const AM_MEDIA_TYPE &outputMediaType) -> HRESULT;
    auto TraverseFiltersInGraph() -> void;

    RemoteControl _remoteControl;

    bool _disconnectFilter = false;
    std::vector<MediaTypePair> _compatibleMediaTypes;
    int _mediaTypeReconnectionWatermark = 0;

    Format::VideoFormat _inputVideoFormat;
    Format::VideoFormat _outputVideoFormat;

    std::atomic<bool> _reloadAvsSource;

    std::filesystem::path _videoSourcePath;
    std::vector<std::wstring> _videoFilterNames;
};

}
