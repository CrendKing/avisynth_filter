// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "side_data.h"


namespace SynthFilter {

class HDRSideData {
public:
    CTOR_WITHOUT_COPYING(HDRSideData)

    auto STDMETHODCALLTYPE StoreSideData(GUID guidType, const BYTE *pData, size_t size) -> HRESULT;
    auto STDMETHODCALLTYPE RetrieveSideData(GUID guidType, const BYTE **pData, size_t *pSize) const -> HRESULT;

    auto ReadFrom(IMediaSideData *from) -> void;
    auto WriteTo(IMediaSideData *to) const -> void;

    auto GetHDRData() const -> std::optional<const BYTE *>;
    auto GetHDRContentLightLevelData() const -> std::optional<const BYTE *>;
    auto GetHDR10PlusData() const -> std::optional<const BYTE *>;
    auto GetHDR3DOffsetData() const -> std::optional<const BYTE *>;

private:
    // not using std::optional here because std::optional<T &> is not available in C++17
    auto GetDataByGUID(GUID guidType) -> std::vector<BYTE> *;

    std::vector<BYTE> _hdrData;
    std::vector<BYTE> _hdrContentLightLevelData;
    std::vector<BYTE> _hdr10PlusData;
    std::vector<BYTE> _hdr3DOffsetData;
};

}
