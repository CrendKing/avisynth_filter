// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"
#include "hdr.h"


namespace AvsFilter {

class CAviSynthFilterMediaSample
    : public CMediaSample
    , public IMediaSideData {
public:
    CAviSynthFilterMediaSample(LPCTSTR pName, CBaseAllocator *pAllocator, HRESULT *phr, LPBYTE pBuffer, LONG length);

    DISABLE_COPYING(CAviSynthFilterMediaSample)

    // IUnknown
    auto STDMETHODCALLTYPE QueryInterface(REFIID riid, __deref_out void **ppv) -> HRESULT override;
    auto STDMETHODCALLTYPE AddRef() -> ULONG override;
    auto STDMETHODCALLTYPE Release() -> ULONG override;

    // IMediaSideData
    auto STDMETHODCALLTYPE SetSideData(GUID guidType, const BYTE *pData, size_t size) -> HRESULT override;
    auto STDMETHODCALLTYPE GetSideData(GUID guidType, const BYTE **pData, size_t *pSize) -> HRESULT override;

private:
    HDRSideData _hdr;
};

}
