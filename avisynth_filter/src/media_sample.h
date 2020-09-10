#pragma once

#include "pch.h"
#include "IMediaSideData.h"

class CAviSynthFilterMediaSample
    : public CMediaSample
    , public IMediaSideData
{
public:
    CAviSynthFilterMediaSample(LPCTSTR pName, CBaseAllocator* pAllocator, HRESULT* phr,
        LPBYTE pBuffer = nullptr, LONG length = 0);

    ~CAviSynthFilterMediaSample();

    //IUnknown
    auto STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) -> HRESULT;
    auto STDMETHODCALLTYPE AddRef() -> ULONG;
    auto STDMETHODCALLTYPE Release() -> ULONG;

    //IMediaSideData
    auto STDMETHODCALLTYPE SetSideData(GUID guidType, const BYTE* pData, size_t size) -> HRESULT;
    auto STDMETHODCALLTYPE GetSideData(GUID guidType, const BYTE** pData, size_t* pSize) -> HRESULT;

private:
    std::map<std::wstring, std::vector<BYTE>> _sideData;
};

//The exact copy of CMemAllocator
class CAviSynthFilterAllocator : public CBaseAllocator {
public:
    CAviSynthFilterAllocator(HRESULT* phr);
    ~CAviSynthFilterAllocator();

    auto STDMETHODCALLTYPE SetProperties(ALLOCATOR_PROPERTIES* pRequest, ALLOCATOR_PROPERTIES* pActual) -> HRESULT;

private:
    auto Alloc()->HRESULT override;

    auto Free() -> void override;
    auto ReallyFree() -> void;

private:
    LPBYTE m_pBuffer;   // combined memory for all buffers
};
