// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "input_pin.h"
#include "allocator.h"
#include "avs_handler.h"
#include "format.h"


namespace AvsFilter {

#define CheckHr(expr) { hr = (expr); if (FAILED(hr)) { return hr; } }

CAviSynthFilterInputPin::CAviSynthFilterInputPin(__in_opt LPCTSTR pObjectName,
                                                 __inout CTransformFilter *pTransformFilter,
                                                 __inout HRESULT *phr,
                                                 __in_opt LPCWSTR pName)
    : CTransformInputPin(pObjectName, pTransformFilter, phr, pName)
    , _filter(reinterpret_cast<CAviSynthFilter &>(*m_pFilter)) {
}

auto STDMETHODCALLTYPE CAviSynthFilterInputPin::ReceiveConnection(IPin *pConnector, const AM_MEDIA_TYPE *pmt) -> HRESULT {
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
            props.cbBuffer = max(static_cast<long>(bmi->biSizeImage + Format::INPUT_MEDIA_SAMPLE_BUFFER_PADDING), props.cbBuffer);

            CheckHr(m_pAllocator->SetProperties(&props, &actual));
            CheckHr(m_pAllocator->Commit());

            if (props.cBuffers > actual.cBuffers || props.cbBuffer > actual.cbBuffer) {
                return E_FAIL;
            }

            CheckHr(_filter.UpdateOutputFormat(*pmt));
        }
    }

    return receiveConnectionHr;
}

/**
 * overridden to return our custom CAviSynthFilterAllocator instead of CMemAllocator,
 * which allocates media sample with IMediaSideData attached
 */
auto STDMETHODCALLTYPE CAviSynthFilterInputPin::GetAllocator(__deref_out IMemAllocator **ppAllocator) -> HRESULT {
    CheckPointer(ppAllocator, E_POINTER);
    ValidateReadWritePtr(ppAllocator, sizeof(IMemAllocator *));
    const std::unique_lock lock(*m_pLock);

    if (m_pAllocator == nullptr) {
        HRESULT hr = S_OK;
        m_pAllocator = new CAviSynthFilterAllocator(&hr, *this);
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

auto CAviSynthFilterInputPin::Active() -> HRESULT {
    _filter._inputVideoFormat = Format::GetVideoFormat(CurrentMediaType());
    _filter._outputVideoFormat = Format::GetVideoFormat(_filter.m_pOutput->CurrentMediaType());

    _filter.TraverseFiltersInGraph();

    if (_filter._remoteControl) {
        _filter._remoteControl->Start();
    }

    // need reload here instead of CompleteConnect() so that switching video works
    g_avs->GetMainScriptInstance().ReloadScript(_filter.m_pInput->CurrentMediaType(), true);
    g_avs->LinkFrameHandler(&_filter.frameHandler);
    _filter.frameHandler.StartWorkerThread();

    return __super::Active();
}

auto CAviSynthFilterInputPin::Inactive() -> HRESULT {
    _filter.frameHandler.StopWorkerThreads();

    return __super::Inactive();
}

}
