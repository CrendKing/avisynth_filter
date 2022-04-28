// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "allocator.h"

#include "media_sample.h"


namespace SynthFilter {

CSynthFilterAllocator::CSynthFilterAllocator(HRESULT *phr)
    : CMemAllocator(NAME("CSynthFilterAllocator"), nullptr, phr) {}

// allocate CSynthFilterMediaSample instead of CMediaSample
auto CSynthFilterAllocator::Alloc() -> HRESULT {
    const std::unique_lock lock(*this);

    HRESULT hr = CBaseAllocator::Alloc();
    if (FAILED(hr)) {
        return hr;
    }

    if (hr == S_FALSE) {
        ASSERT(m_pBuffer);
        return NOERROR;
    }
    ASSERT(hr == S_OK);

    if (m_pBuffer) {
        ReallyFree();
    }

    if (m_lSize < 0 || m_lPrefix < 0 || m_lCount < 0) {
        return E_OUTOFMEMORY;
    }

    LONG lAlignedSize = m_lSize + m_lPrefix;
    // check overflow
    if (lAlignedSize < m_lSize) {
        return E_OUTOFMEMORY;
    }

    if (m_lAlignment > 1) {
        const LONG lNewSize = FFALIGN(lAlignedSize, m_lAlignment);
        if (lNewSize < lAlignedSize) {
            return E_OUTOFMEMORY;
        }
        lAlignedSize = lNewSize;
    }

    ASSERT(lAlignedSize % m_lAlignment == 0);

    const SIZE_T lToAllocate = m_lCount * static_cast<SIZE_T>(lAlignedSize);
    if (lToAllocate > MAXLONG) {
        return E_OUTOFMEMORY;
    }

    m_pBuffer = static_cast<PBYTE>(VirtualAlloc(nullptr, lToAllocate, MEM_COMMIT, PAGE_READWRITE));

    if (m_pBuffer == nullptr) {
        return E_OUTOFMEMORY;
    }

    ASSERT(m_lAllocated == 0);

    LPBYTE pNext = m_pBuffer;
    for (CMediaSample *pSample; m_lAllocated < m_lCount; m_lAllocated++, pNext += lAlignedSize) {
        pSample = new CSynthFilterMediaSample(NAME("CSynthFilter memory media sample"), this, &hr, pNext + m_lPrefix, m_lSize);

        ASSERT(SUCCEEDED(hr));
        if (pSample == nullptr) {
            return E_OUTOFMEMORY;
        }

        m_lFree.Add(pSample);
    }

    m_bChanged = FALSE;
    return NOERROR;
}

}
