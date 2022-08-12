// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "input_pin.h"

#include "allocator.h"
#include "constants.h"
#include "format.h"
#include "macros.h"


namespace SynthFilter {

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

}
