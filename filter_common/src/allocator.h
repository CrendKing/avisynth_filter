// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "input_pin.h"


namespace SynthFilter {

class CSynthFilterAllocator : public CMemAllocator {
public:
    explicit CSynthFilterAllocator(HRESULT *phr, CSynthFilterInputPin &inputPin);

    DISABLE_COPYING(CSynthFilterAllocator)

protected:
    auto Alloc() -> HRESULT override;
    auto STDMETHODCALLTYPE SetProperties(__in ALLOCATOR_PROPERTIES *pRequest, __out ALLOCATOR_PROPERTIES *pActual) -> HRESULT override;

private:
    CSynthFilterInputPin &_inputPin;
};

}
