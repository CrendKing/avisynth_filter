// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "allocator.h"

#include "media_sample.h"


namespace SynthFilter {

CSynthFilterAllocator::CSynthFilterAllocator(HRESULT *phr, CSynthFilterInputPin &inputPin)
    : CMemAllocator(NAME("CSynthFilterAllocator"), nullptr, phr)
    , _inputPin(inputPin) {}

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

    if (lAlignedSize < m_lSize) {
        return E_OUTOFMEMORY;
    }

    if (m_lAlignment > 1) {
        if (const LONG lRemainder = lAlignedSize % m_lAlignment; lRemainder != 0) {
            const LONG lNewSize = lAlignedSize + m_lAlignment - lRemainder;
            if (lNewSize < lAlignedSize) {
                return E_OUTOFMEMORY;
            }
            lAlignedSize = lNewSize;
        }
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

auto STDMETHODCALLTYPE CSynthFilterAllocator::SetProperties(__in ALLOCATOR_PROPERTIES *pRequest, __out ALLOCATOR_PROPERTIES *pActual) -> HRESULT {
    const BITMAPINFOHEADER *bmi = Format::GetBitmapInfo(_inputPin.CurrentMediaType());
    pRequest->cbBuffer = max(static_cast<long>(bmi->biSizeImage + Format::INPUT_MEDIA_SAMPLE_BUFFER_PADDING), pRequest->cbBuffer);
    return __super::SetProperties(pRequest, pActual);
}

}
