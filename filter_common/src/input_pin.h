// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "filter.h"


namespace SynthFilter {

class CSynthFilterInputPin : public CTransformInputPin {
public:
    CSynthFilterInputPin(__in_opt LPCTSTR pObjectName, __inout CTransformFilter *pTransformFilter, __inout HRESULT *phr, __in_opt LPCWSTR pName);

    DISABLE_COPYING(CSynthFilterInputPin)

    auto STDMETHODCALLTYPE ReceiveConnection(IPin *pConnector, const AM_MEDIA_TYPE *pmt) -> HRESULT override;
    auto STDMETHODCALLTYPE GetAllocator(__deref_out IMemAllocator **ppAllocator) -> HRESULT override;
    auto Active() -> HRESULT override;
    auto Inactive() -> HRESULT override;

private:
    CSynthFilter &_filter = reinterpret_cast<CSynthFilter &>(*m_pFilter);
};

}
