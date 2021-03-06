// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "hdr.h"


namespace SynthFilter {

class CSynthFilterMediaSample
    : public CMediaSample
    , public IMediaSideData {
public:
    CSynthFilterMediaSample(LPCTSTR pName, CBaseAllocator *pAllocator, HRESULT *phr, LPBYTE pBuffer, LONG length);

    DISABLE_COPYING(CSynthFilterMediaSample)

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
