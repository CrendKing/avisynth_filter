// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"
#include "api.h"
#include "format.h"
#include "frame_handler.h"
#include "frameserver.h"
#include "remote_control.h"


namespace SynthFilter {

class
#ifdef AVSF_AVISYNTH
__declspec(uuid("e5e2c1a6-c90f-4247-8bf5-604fb180a932"))
#else
__declspec(uuid("3ab7506b-fc4a-4144-8ee3-a97fab4f9cb3"))
#endif
    CSynthFilter
    : public CVideoTransformFilter
    , public ISpecifyPropertyPages {

    friend class CSynthFilterInputPin;
    friend class FrameHandler;

public:
    CSynthFilter(LPUNKNOWN pUnk, HRESULT *phr);
    ~CSynthFilter() override;

    DECLARE_IUNKNOWN

    DISABLE_COPYING(CSynthFilter)

    auto STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid, __deref_out void **ppv) -> HRESULT override;

    // CVideoTransformFilter
    auto GetPin(int n) -> CBasePin * override;
    auto CheckConnect(PIN_DIRECTION direction, IPin *pPin) -> HRESULT override;
    auto BreakConnect(PIN_DIRECTION direction) -> HRESULT override;
    auto CheckInputType(const CMediaType *mtIn) -> HRESULT override;
    auto GetMediaType(int iPosition, CMediaType *pMediaType) -> HRESULT override;
    auto CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut) -> HRESULT override;
    auto DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProperties) -> HRESULT override;
    auto CompleteConnect(PIN_DIRECTION direction, IPin *pReceivePin) -> HRESULT override;
    auto Receive(IMediaSample *pSample) -> HRESULT override;
    auto BeginFlush() -> HRESULT override;
    auto EndFlush() -> HRESULT override;

    // ISpecifyPropertyPages
    auto STDMETHODCALLTYPE GetPages(__RPC__out CAUUID *pPages) -> HRESULT override;

    constexpr auto GetInputFormat() const -> Format::VideoFormat { return _inputVideoFormat; }
    constexpr auto GetOutputFormat() const -> Format::VideoFormat { return _outputVideoFormat; }
    auto ReloadScript(const std::filesystem::path &scriptPath) -> void;
    constexpr auto GetVideoSourcePath() const -> const std::filesystem::path & { return _videoSourcePath; }
    constexpr auto GetVideoFilterNames() const -> const std::vector<std::wstring> & { return _videoFilterNames; }
    auto GetFrameServerState() const -> AvsState;

    std::unique_ptr<FrameHandler> frameHandler = std::make_unique<FrameHandler>(*this);

private:
    struct MediaTypePair {
        const std::shared_ptr<AM_MEDIA_TYPE> inputMediaType;
        const Format::PixelFormat *inputPixelFormat;

        CMediaType outputMediaType;
        const Format::PixelFormat *outputPixelFormat;
    };

    static auto InputToOutputMediaType(const AM_MEDIA_TYPE *mtIn) {
        AuxFrameServer::GetInstance().ReloadScript(*mtIn, true);
        return Format::LookupFsFormatId(AuxFrameServer::GetInstance().GetScriptPixelType()) | std::views::transform([mtIn](const Format::PixelFormat &pixelFormat) -> CMediaType {
            return AuxFrameServer::GetInstance().GenerateMediaType(pixelFormat, mtIn);
        });
    }

    static auto MediaTypeToPixelFormat(const AM_MEDIA_TYPE *mediaType) -> const Format::PixelFormat *;
    static auto GetInputPixelFormat(const AM_MEDIA_TYPE *mediaType) -> const Format::PixelFormat *;
    static auto FindFirstVideoOutputPin(IBaseFilter *pFilter) -> std::optional<IPin *>;

    // not thread-safe, but filter instances should be only created by the Graph thread
    static inline int _numFilterInstances = 0;

    auto TraverseFiltersInGraph() -> void;

    std::unique_ptr<RemoteControl> _remoteControl = std::make_unique<RemoteControl>(*this);

    bool _disconnectFilter = false;
    std::vector<MediaTypePair> _compatibleMediaTypes;
    std::vector<CMediaType> _availableOutputMediaTypes;
    int _mediaTypeReconnectionWatermark = 0;

    Format::VideoFormat _inputVideoFormat;
    Format::VideoFormat _outputVideoFormat;

    bool _changeOutputMediaType = false;
    bool _reloadScript = false;

    std::filesystem::path _videoSourcePath;
    std::vector<std::wstring> _videoFilterNames;
};

}
