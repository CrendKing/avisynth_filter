// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"
#include "filter.h"


namespace AvsFilter {

class CAviSynthFilterInputPin : public CTransformInputPin {
public:
    CAviSynthFilterInputPin(__in_opt LPCTSTR pObjectName,
                            __inout CTransformFilter *pTransformFilter,
                            __inout HRESULT *phr,
                            __in_opt LPCWSTR pName);

    auto STDMETHODCALLTYPE ReceiveConnection(IPin *pConnector, const AM_MEDIA_TYPE *pmt) -> HRESULT override;
    auto STDMETHODCALLTYPE GetAllocator(__deref_out IMemAllocator **ppAllocator) -> HRESULT override;
    auto Active() -> HRESULT override;
    auto Inactive() -> HRESULT override;

private:
    CAviSynthFilter &_filter;
};

}
