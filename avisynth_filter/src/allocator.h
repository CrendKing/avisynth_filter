// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"


namespace AvsFilter {

class CAviSynthFilterAllocator : public CMemAllocator {
public:
    explicit CAviSynthFilterAllocator(HRESULT *phr);

protected:
    auto Alloc() -> HRESULT override;
    auto STDMETHODCALLTYPE SetProperties(__in ALLOCATOR_PROPERTIES *pRequest, __out ALLOCATOR_PROPERTIES *pActual) -> HRESULT override;
};

}
