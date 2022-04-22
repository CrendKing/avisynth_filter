// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "input_pin.h"

#include "allocator.h"


namespace SynthFilter {

#define CheckHr(expr)     \
    {                     \
        hr = (expr);      \
        if (FAILED(hr)) { \
            return hr;    \
        }                 \
    }

CSynthFilterInputPin::CSynthFilterInputPin(__in_opt LPCTSTR pObjectName, __inout CTransformFilter *pTransformFilter, __inout HRESULT *phr, __in_opt LPCWSTR pName)
    : CTransformInputPin(pObjectName, pTransformFilter, phr, pName) {}

auto STDMETHODCALLTYPE CSynthFilterInputPin::ReceiveConnection(IPin *pConnector, const AM_MEDIA_TYPE *pmt) -> HRESULT {
    HRESULT hr;

    const HRESULT receiveConnectionHr = __super::ReceiveConnection(pConnector, pmt);

    if (receiveConnectionHr == VFW_E_ALREADY_CONNECTED) {
        ASSERT(m_pAllocator != nullptr);

        if (CheckMediaType(static_cast<const CMediaType *>(pmt)) == S_OK) {
            const std::unique_lock lock(*m_pLock);

            ALLOCATOR_PROPERTIES props, actual;

            CheckHr(m_pAllocator->Decommit());
            CheckHr(m_pAllocator->GetProperties(&props));

            const BITMAPINFOHEADER *bmi = Format::GetBitmapInfo(*pmt);
            props.cbBuffer = std::max(static_cast<long>(bmi->biSizeImage + Format::INPUT_MEDIA_SAMPLE_BUFFER_PADDING), props.cbBuffer);

            CheckHr(m_pAllocator->SetProperties(&props, &actual));
            CheckHr(m_pAllocator->Commit());

            if (props.cBuffers > actual.cBuffers || props.cbBuffer > actual.cbBuffer) {
                return E_FAIL;
            }
        }
    }

    return receiveConnectionHr;
}

/**
 * overridden to return our custom CSynthFilterAllocator instead of CMemAllocator,
 * which allocates media sample with IMediaSideData attached
 */
auto STDMETHODCALLTYPE CSynthFilterInputPin::GetAllocator(__deref_out IMemAllocator **ppAllocator) -> HRESULT {
    CheckPointer(ppAllocator, E_POINTER);
    ValidateReadWritePtr(ppAllocator, sizeof(IMemAllocator *));
    const std::unique_lock lock(*m_pLock);

    if (m_pAllocator == nullptr) {
        HRESULT hr = S_OK;
        m_pAllocator = new CSynthFilterAllocator(&hr, *this);
        if (FAILED(hr)) {
            return hr;
        }
        m_pAllocator->AddRef();
    }

    ASSERT(m_pAllocator != nullptr);
    *ppAllocator = m_pAllocator;
    m_pAllocator->AddRef();

    return S_OK;
}

auto CSynthFilterInputPin::Active() -> HRESULT {
    MainFrameServer::GetInstance().ReloadScript(_filter.m_pInput->CurrentMediaType(), true);

    _filter._inputVideoFormat = Format::GetVideoFormat(_filter.m_pInput->CurrentMediaType(), &MainFrameServer::GetInstance());
    _filter._outputVideoFormat = Format::GetVideoFormat(_filter.m_pOutput->CurrentMediaType(), &MainFrameServer::GetInstance());

    if (Environment::GetInstance().IsRemoteControlEnabled()) {
        _filter._remoteControl->Start();
    }

    MainFrameServer::GetInstance().LinkFrameHandler(_filter.frameHandler.get());
    return S_OK;
}

auto CSynthFilterInputPin::Inactive() -> HRESULT {
    MainFrameServer::GetInstance().LinkFrameHandler(nullptr);

    _filter.frameHandler->BeginFlush();
    _filter.frameHandler->EndFlush([this]() -> void {
        MainFrameServer::GetInstance().StopScript();
    });

    return __super::Inactive();
}

}
