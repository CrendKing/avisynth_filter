// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "input_pin.h"

#include "allocator.h"
#include "constants.h"
#include "format.h"


namespace SynthFilter {

#define CheckHr(expr)      \
    {                      \
        hr = (expr);       \
        if (FAILED(hr)) {  \
            return hr;     \
        }                  \
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
            CheckHr(m_pAllocator->GetProperties(&props));

            const int strideAlignment = std::max(MEDIA_SAMPLE_STRIDE_ALGINMENT, Format::INPUT_MEDIA_SAMPLE_STRIDE_ALIGNMENT);
            const long newMediaSampleSize = Format::GetStrideAlignedMediaSampleSize(*pmt, strideAlignment);

            // if the new media sample size is larger than current, we need to re-allocate buffers with larger sample size
            if (props.cbBuffer < newMediaSampleSize) {
                props.cbBuffer = newMediaSampleSize;

                CheckHr(m_pAllocator->Decommit());
                CheckHr(m_pAllocator->SetProperties(&props, &actual));
                CheckHr(m_pAllocator->Commit());

                if (props.cBuffers > actual.cBuffers || props.cbBuffer > actual.cbBuffer) {
                    return E_FAIL;
                }
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
        m_pAllocator = new CSynthFilterAllocator(&hr);
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
    AuxFrameServer::GetInstance().ReloadScript(CurrentMediaType(), true);
    _filter._inputVideoFormat = Format::GetVideoFormat(CurrentMediaType(), &AuxFrameServer::GetInstance());
    _filter._outputVideoFormat = Format::GetVideoFormat(_filter.m_pOutput->CurrentMediaType(), &AuxFrameServer::GetInstance());

    if (Environment::GetInstance().IsRemoteControlEnabled()) {
        _filter._remoteControl->Start();
    }

    return S_OK;
}

auto CSynthFilterInputPin::Inactive() -> HRESULT {
    _filter.frameHandler->BeginFlush();
    _filter.frameHandler->EndFlush([]() -> void {
        MainFrameServer::GetInstance().StopScript();
    });

    return __super::Inactive();
}

}
