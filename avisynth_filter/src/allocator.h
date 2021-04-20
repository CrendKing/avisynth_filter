// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "input_pin.h"


namespace AvsFilter {

class CAviSynthFilterAllocator : public CMemAllocator {
public:
    explicit CAviSynthFilterAllocator(HRESULT *phr, CAviSynthFilterInputPin &inputPin);

    DISABLE_COPYING(CAviSynthFilterAllocator)

protected:
    auto Alloc() -> HRESULT override;
    auto STDMETHODCALLTYPE SetProperties(__in ALLOCATOR_PROPERTIES *pRequest, __out ALLOCATOR_PROPERTIES *pActual) -> HRESULT override;

private:
    CAviSynthFilterInputPin &_inputPin;
};

}
