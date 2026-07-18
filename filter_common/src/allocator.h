// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE.txt

#pragma once

#include "input_pin.h"


namespace SynthFilter {

class CSynthFilterAllocator : public CMemAllocator {
public:
    explicit CSynthFilterAllocator(HRESULT *phr);

    DISABLE_COPYING(CSynthFilterAllocator)

protected:
    auto Alloc() -> HRESULT override;
};

}
