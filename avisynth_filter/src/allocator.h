// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"


namespace AvsFilter {

class CAviSynthFilterAllocator : public CMemAllocator {
public:
    explicit CAviSynthFilterAllocator(HRESULT *phr);

protected:
    auto Alloc() -> HRESULT override;
};

}
