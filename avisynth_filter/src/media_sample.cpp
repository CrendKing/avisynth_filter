#include "pch.h"
#include "media_sample.h"


namespace AvsFilter {

CAviSynthFilterMediaSample::CAviSynthFilterMediaSample(LPCTSTR pName, CBaseAllocator* pAllocator, HRESULT* phr, LPBYTE pBuffer, LONG length)
    : CMediaSample(pName, pAllocator, phr, pBuffer, length) {
}

auto STDMETHODCALLTYPE CAviSynthFilterMediaSample::QueryInterface(REFIID riid, __deref_out void **ppv) -> HRESULT {
    if (riid == __uuidof(IMediaSideData)) {
        return GetInterface(static_cast<IMediaSideData *>(this), ppv);
    }

    return __super::QueryInterface(riid, ppv);
}

auto STDMETHODCALLTYPE CAviSynthFilterMediaSample::AddRef() -> ULONG {
    return __super::AddRef();
}

auto STDMETHODCALLTYPE CAviSynthFilterMediaSample::Release() -> ULONG {
    return __super::Release();
}

auto STDMETHODCALLTYPE CAviSynthFilterMediaSample::SetSideData(GUID guidType, const BYTE *pData, size_t size) -> HRESULT {
    return _hdr.StoreSideData(guidType, pData, size);
}

auto STDMETHODCALLTYPE CAviSynthFilterMediaSample::GetSideData(GUID guidType, const BYTE **pData, size_t *pSize) -> HRESULT {
    return _hdr.RetrieveSideData(guidType, pData, pSize);
}

}
