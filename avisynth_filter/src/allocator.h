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
